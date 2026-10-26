#include "Exciter.h"

namespace burnish
{
Exciter::Exciter()
    : Module ({ "exc", "EXCITER", "EXC", 0xffff7088u, true, false })
{
    pOn        = addBool ("on", "On", true);
    pChMode    = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pOversample = addChoice ("oversample", "Oversampling", { "Off", "2x", "4x" }, 1);
    pLink      = addBool ("link", "Link Bands", false);
    pPostFreq  = addHz ("postfreq", "Post Filter Freq", 1000.0f, 20000.0f, 12000.0f);
    pPostGain  = addDb ("postgain", "Post Filter Gain", -24.0f, 0.0f, 0.0f);
    mb.declare (*this, 150.0f, 1500.0f, 6000.0f);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            band[s][b].mode   = addChoice (p + "mode", n + "Mode", { "Analog", "Retro", "Tape", "Tube", "Warm", "Triode", "Dual Triode" }, Tape);
            band[s][b].amount = addPct (p + "amount", n + "Amount", 0.0f, 100.0f, 20.0f);
            band[s][b].mix    = addPct (p + "mix", n + "Mix", 0.0f, 100.0f, 100.0f);
        }
}

int Exciter::latency() const
{
    const int os = (int) value (pOversample);
    // Each half-band stage delays by 15 samples at its own rate, both ways.
    return os == 1 ? HalfBand::latency() : os == 2 ? HalfBand::latency() + HalfBand::latency() / 2 : 0;
}

void Exciter::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    for (auto& row : st) for (auto& b : row) b.trans.prepare ((float) sr);
    for (auto& row : amountSm) for (auto& s : row) s.setTime (20.0f, (float) sr, kStep);
    for (auto& row : mixSm) for (auto& s : row) s.setTime (20.0f, (float) sr, kStep);
    reset();
}

void Exciter::reset()
{
    split.reset();
    for (auto& row : st) for (auto& b : row) { b.os1.reset(); b.os2.reset(); b.dc = 0.0f; b.trans.reset(); b.harmMs = b.dryMs = 0.0f; }
    for (auto& f : post) f.reset();
    lastPost[0] = -1.0f;
}

// `drive` is 1..~12 from AMOUNT. Output stays near unity for small signals.
float Exciter::shape (int mode, float x, float drive, float& dc)
{
    const float d = x * drive;
    float y;
    switch (mode)
    {
        case Analog:     y = softTanh (d * 1.5f) / 1.5f; break;                                   // odd, driven
        case Retro:      { const float c = clampf (d, -1.5f, 1.5f); y = c - c * c * c / 6.75f; } break;   // cubic, few harmonics
        case Tape:       y = (2.0f / kPif) * std::atan (d * 1.3f) / 1.3f; break;                   // odd, brighter
        case Tube:       y = softTanh (d + 0.15f * d * d) ; break;                                 // asymmetric, see process()
        case Warm:       y = d + 0.35f * d * d; break;                                             // even only
        case Triode:     y = softTanh (d * 0.9f + 0.25f) - softTanh (0.25f); break;
        case DualTriode: { const float a = softTanh (d * 0.9f + 0.25f) - softTanh (0.25f); y = softTanh (a * 1.8f - 0.2f) + softTanh (0.2f); } break;
        default:         y = d; break;
    }
    y /= drive;
    // Block the DC the asymmetric modes make.
    if (mode == Tube || mode == Warm || mode == Triode || mode == DualTriode)
    {
        dc += 0.002f * (y - dc);
        y -= dc;
    }
    return y;
}

void Exciter::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const int nb = (int) value (mb.pNumBands) + 1;
    const int os = (int) value (pOversample);
    const int factor = os == 0 ? 1 : os == 1 ? 2 : 4;
    const int solo = soloBand.load (std::memory_order_relaxed);

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]), value (mb.pXover[2]));
        if (value (pPostFreq) != lastPost[0] || value (pPostGain) != lastPost[1])
        {
            lastPost[0] = value (pPostFreq); lastPost[1] = value (pPostGain);
            for (auto& f : post) f.highShelf (lastPost[0], lastPost[1], sr);
        }
        float drive[2][kBands], mix[2][kBands];
        int mode[2][kBands];
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            for (int b = 0; b < nb; ++b)
            {
                amountSm[c][b].target = value (band[s][b].amount) * 0.01f;
                mixSm[c][b].target = value (band[s][b].mix) * 0.01f;
                const float a = amountSm[c][b].next();
                drive[c][b] = 1.0f + a * a * 11.0f;
                mix[c][b] = mixSm[c][b].next() * clampf (a * 4.0f, 0.0f, 1.0f);   // amount 0 is silent
                mode[c][b] = (int) value (band[s][b].mode);
            }
        }

        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            float out[2] { 0.0f, 0.0f };
            for (int c = 0; c < 2; ++c)
            {
                float bands[kBands];
                split.split (c, x[c], bands);
                float harmSum = 0.0f;
                for (int b = 0; b < nb; ++b)
                {
                    const float dry = bands[b];
                    float harm = 0.0f;
                    if (value (mb.pBandOn[b]) > 0.5f && mix[c][b] > 0.0f)
                    {
                        auto& bs = st[c][b];
                        float dr = drive[c][b];
                        if (mode[c][b] == Tube)
                            dr *= 1.0f + 1.5f * bs.trans.next (std::fabs (dry));   // attacks drive it harder
                        const int m = mode[c][b];
                        float* dcp = &bs.dc;
                        const float wet = oversampled (factor, bs.os1, bs.os2, dry, [m, dr, dcp] (float v) { return shape (m, v, dr, *dcp); });
                        harm = (wet - dry) * mix[c][b];
                        bs.harmMs += 0.0005f * (harm * harm - bs.harmMs);
                        bs.dryMs += 0.0005f * (dry * dry - bs.dryMs);
                    }
                    if (solo < 0 || solo == b)
                    {
                        out[c] += dry;
                        harmSum += harm;
                    }
                }
                out[c] += post[c].tick (harmSum);
            }
            ChannelModes::join (chMode, out[0], out[1], l[i], r[i]);
        }
    }
    for (int b = 0; b < kBands; ++b)
    {
        const auto& bs = st[0][b];
        harmonicLevel[b] = bs.dryMs > 1e-9f ? clampf (std::sqrt (bs.harmMs / bs.dryMs) * 2.0f, 0.0f, 1.0f) : 0.0f;
    }
}
} // namespace burnish
