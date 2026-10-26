#include "Impact.h"

namespace burnish
{
namespace
{
const char* kDivNames[] { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar" };
const float kDivBeats[] { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
}

const char* Impact::syncDivisionName (int idx) { return kDivNames[std::min (5, std::max (0, idx))]; }
float Impact::syncDivisionSeconds (int idx, double bpm) { return (float) (kDivBeats[std::min (5, std::max (0, idx))] * 60.0 / std::max (20.0, bpm)); }

Impact::Impact()
    : Module ({ "imp", "IMPACT", "IMP", 0xff4ccaa0u, true, false })
{
    pOn         = addBool ("on", "On", true);
    pChMode     = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pAmount     = addPct ("amount", "Amount", 0.0f, 100.0f, 100.0f);
    pStereoLink = addBool ("stereolink", "Stereo Link", false);
    pLink       = addBool ("link", "Link Bands", false);
    pAutoGain   = addBool ("autogain", "Auto Gain", true);
    pSync       = addBool ("sync", "Sync", false);
    pEnvelope   = addMs ("envelope", "Envelope", 10.0f, 2000.0f, 120.0f);
    pEnvelopeSync = addChoice ("envsync", "Envelope Division", { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar" }, 2);
    mb.declare (*this, 120.0f, 1000.0f, 6000.0f);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
            pImpact[s][b] = addFloat (std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".impact",
                                      std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " Impact", -100.0f, 100.0f, 0.0f, "", 0);
}

void Impact::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    fastCoef = tauCoef (3.0f, (float) sr);
    for (auto& row : st) for (auto& b : row) b.ag.prepare ((float) sr);
    for (auto& row : impactSm) for (auto& s : row) s.setTime (40.0f, (float) sr, kStep);
    reset();
}

void Impact::reset()
{
    split.reset();
    for (auto& row : st) for (auto& b : row) { b.fast = b.slow = b.gainDbSm = 0.0f; b.ag.reset(); }
}

void Impact::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const int nb = (int) value (mb.pNumBands) + 1;
    const int solo = soloBand.load (std::memory_order_relaxed);
    const bool linked = value (pStereoLink) > 0.5f && chMode == Stereo;
    const bool autoGain = value (pAutoGain) > 0.5f;
    const float amount = value (pAmount) * 0.01f;
    const float envSec = value (pSync) > 0.5f ? syncDivisionSeconds ((int) value (pEnvelopeSync), hostBpm)
                                              : value (pEnvelope) * 0.001f;
    slowCoef = tauCoef (envSec * 1000.0f, (float) sr);
    envCoef = tauCoef (std::max (2.0f, envSec * 1000.0f * 0.15f), (float) sr);   // how fast the gain follows

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]), value (mb.pXover[2]));
        float imp[2][kBands];
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            for (int b = 0; b < nb; ++b)
            {
                impactSm[c][b].target = value (mb.pBandOn[b]) > 0.5f ? value (pImpact[s][b]) * 0.01f * amount : 0.0f;
                imp[c][b] = impactSm[c][b].next();
            }
        }

        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            float bands[2][kBands];
            for (int c = 0; c < 2; ++c) split.split (c, x[c], bands[c]);
            float out[2] { 0.0f, 0.0f };
            for (int b = 0; b < nb; ++b)
            {
                float g[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& bs = st[c][b];
                    const float ax = linked ? 0.5f * (std::fabs (bands[0][b]) + std::fabs (bands[1][b])) : std::fabs (bands[c][b]);
                    bs.fast += (ax > bs.fast ? fastCoef * 4.0f : fastCoef) * (ax - bs.fast);
                    bs.slow += slowCoef * (ax - bs.slow);
                    // Microdynamic deviation in dB, limited to a sane range.
                    const float dev = clampf (gainToDb (bs.fast + 1e-6f) - gainToDb (bs.slow + 1e-6f), -18.0f, 18.0f);
                    const float target = clampf (dev * imp[c][b] * 0.6f, -12.0f, 12.0f);
                    bs.gainDbSm += envCoef * (target - bs.gainDbSm);
                    g[c] = bs.gainDbSm;
                }
                if (linked) g[1] = g[0];
                for (int c = 0; c < 2; ++c)
                {
                    auto& bs = st[c][b];
                    const float dry = bands[c][b];
                    float wet = dry * dbToGain (g[c]);
                    if (autoGain) { bs.ag.push (dry, wet); wet *= dbToGain (bs.ag.gainDb); }
                    else bs.ag.gainDb = 0.0f;
                    if (solo < 0 || solo == b) out[c] += wet;
                    if (i == start) gainDb[c][b] = g[c] + bs.ag.gainDb;
                }
            }
            ChannelModes::join (chMode, out[0], out[1], l[i], r[i]);
        }
    }
}
} // namespace burnish
