#include "Analysis.h"

namespace burnish
{
// Typical loudness, width and crest per genre target, to go with the
// tonal balance curves in Spectral.cpp. Width is side/mid energy in dB
// per band (lows, low mids, highs, air); crest is the fast-peak over
// slow-RMS of each band in dB (higher = more dynamic).
namespace
{
struct GenreExtras { float lufs; float width[4]; float crest[4]; };
const GenreExtras kExtras[TonalTargets::kNumTargets] {
    { -11.0f, { -14.0f, -6.0f, -4.0f, -3.0f }, { 9.0f, 10.0f, 11.0f, 12.0f } },    // All-purpose
    { -9.5f,  { -15.0f, -6.0f, -3.5f, -2.5f }, { 8.0f,  9.5f, 11.0f, 12.0f } },    // Pop
    { -9.0f,  { -14.0f, -5.0f, -3.0f, -2.5f }, { 8.5f, 10.0f, 11.0f, 12.0f } },    // Rock
    { -9.0f,  { -18.0f, -7.0f, -4.0f, -3.0f }, { 9.5f, 10.5f, 11.5f, 12.5f } },    // Hip-Hop
    { -8.0f,  { -16.0f, -6.0f, -3.0f, -2.0f }, { 8.0f,  9.0f, 10.5f, 11.5f } },    // Electronic
    { -10.0f, { -16.0f, -6.5f, -4.0f, -3.0f }, { 9.0f, 10.0f, 11.0f, 12.0f } },    // R&B
    { -10.5f, { -13.0f, -5.0f, -3.5f, -2.5f }, { 9.0f, 10.5f, 11.5f, 12.5f } },    // Indie
    { -13.0f, { -12.0f, -5.0f, -4.0f, -3.5f }, { 11.0f, 12.0f, 13.0f, 14.0f } },   // Folk
    { -14.0f, { -10.0f, -5.0f, -4.0f, -3.5f }, { 12.0f, 13.0f, 14.0f, 15.0f } },   // Jazz
    { -18.0f, { -8.0f,  -4.0f, -3.5f, -3.0f }, { 13.0f, 14.0f, 15.0f, 16.0f } },   // Classical
    { -16.0f, { -10.0f, -4.0f, -3.0f, -2.5f }, { 12.0f, 13.0f, 14.0f, 15.0f } },   // Cinematic
};
}

Target Target::genre (int index)
{
    index = std::min (TonalTargets::kNumTargets - 1, std::max (0, index));
    Target t;
    t.name = TonalTargets::name (index);
    for (int b = 0; b < AnalysisResult::kBands; ++b)
        t.spectrumDb[(size_t) b] = TonalTargets::curveDb (index, b);
    t.lufs = kExtras[index].lufs;
    for (int i = 0; i < 4; ++i) { t.widthDb[(size_t) i] = kExtras[index].width[i]; t.crestDb[(size_t) i] = kExtras[index].crest[i]; }
    return t;
}

Target Target::fromAnalysis (const std::string& name, const AnalysisResult& a)
{
    Target t;
    t.name = name;
    t.custom = true;
    t.spectrumDb = a.spectrumDb;
    t.lufs = a.integratedLufs;
    t.widthDb = a.widthDb;
    t.crestDb = a.crestDb;
    return t;
}

// ---------------------------------------------------------------------------
void Analyzer::prepare (double sampleRate)
{
    sr = sampleRate;
    fft.prepare (kFrame);
    bands.prepare (TonalTargets::kBandsPerOctave, kFrame, sr);
    window.resize (kFrame);
    for (int i = 0; i < kFrame; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * kPif * (float) i / (float) kFrame));
    frameBuf.assign (kFrame, 0.0f);
    bins.assign (kFrame / 2 + 1, Fft::cpx (0.0f, 0.0f));
    acc.assign ((size_t) std::max (bands.count, AnalysisResult::kBands), 0.0);
    frameEnergies.assign ((size_t) std::max (bands.count, AnalysisResult::kBands), 0.0f);
    ring.assign (kFrame, 0.0f);
    meter.prepare (sr);
    meter.setTruePeak (false);
    xoM.prepare (sr); xoS.prepare (sr);
    xoM.set (4, xoFreqs); xoS.set (4, xoFreqs);
    for (int b = 0; b < 4; ++b) { fastEnv[b].set (1.0f, 40.0f, (float) sr); slowEnv[b].set (300.0f, 300.0f, (float) sr); }
    reset();
}

void Analyzer::reset()
{
    samples = 0;
    std::fill (acc.begin(), acc.end(), 0.0);
    frames = 0; inPos = 0; sinceHop = 0;
    std::fill (ring.begin(), ring.end(), 0.0f);
    meter.reset();
    stMax = -70.0f;
    xoM.reset(); xoS.reset();
    for (int b = 0; b < 4; ++b) { midE[b] = sideE[b] = 0.0; crestAcc[b] = 0.0; fastEnv[b].reset(); slowEnv[b].reset(); }
    crestCount = 0;
}

void Analyzer::push (const float* l, const float* r, int n)
{
    meter.process (l, r, n);
    stMax = std::max (stMax, meter.shortTerm());
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
        ring[(size_t) inPos] = m;
        inPos = (inPos + 1) % kFrame;
        if (++sinceHop >= kHop)
        {
            sinceHop = 0;
            for (int k = 0; k < kFrame; ++k)
                frameBuf[(size_t) k] = ring[(size_t) ((inPos + k) % kFrame)] * window[(size_t) k];
            fft.forwardReal (frameBuf.data(), bins.data());
            bands.energies (bins.data(), frameEnergies.data());
            for (int b = 0; b < bands.count && b < AnalysisResult::kBands; ++b)
                acc[(size_t) b] += frameEnergies[(size_t) b];
            ++frames;
        }
        float mb[4], sb[4];
        xoM.tick (m, mb);
        xoS.tick (s, sb);
        for (int b = 0; b < 4; ++b)
        {
            midE[b] += (double) mb[b] * mb[b];
            sideE[b] += (double) sb[b] * sb[b];
            const float a = std::fabs (mb[b]);
            const float f = fastEnv[b].next (a);
            const float sl = slowEnv[b].next (a * a);
            if (sl > 1e-8f)
                crestAcc[b] += gainToDb (f) - 10.0 * std::log10 (sl);
        }
        ++crestCount;
    }
    samples += n;
}

void Analyzer::normalise (std::array<float, AnalysisResult::kBands>& b)
{
    // Relative to the mean over 100 Hz .. 8 kHz, and expressed as deviation
    // from pink (per-bin energy falls 3 dB/oct).
    double sum = 0; int cnt = 0;
    for (int i = 0; i < AnalysisResult::kBands; ++i)
    {
        b[(size_t) i] += 3.0f * std::log2 (TonalTargets::centreHz (i) / 1000.0f);
        const float hz = TonalTargets::centreHz (i);
        if (hz >= 100.0f && hz <= 8000.0f) { sum += b[(size_t) i]; ++cnt; }
    }
    const float mean = cnt > 0 ? (float) (sum / cnt) : 0.0f;
    for (auto& v : b) v -= mean;
}

AnalysisResult Analyzer::result() const
{
    AnalysisResult r;
    r.seconds = seconds();
    if (frames == 0 || samples < (long) sr)
        return r;
    for (int b = 0; b < AnalysisResult::kBands; ++b)
        r.spectrumDb[(size_t) b] = 10.0f * std::log10 ((float) (acc[(size_t) b] / frames) + 1e-12f);
    normalise (r.spectrumDb);
    r.integratedLufs = meter.hasIntegrated() ? meter.integrated() : meter.shortTerm();
    r.shortTermMaxLufs = stMax;
    for (int b = 0; b < 4; ++b)
    {
        r.widthDb[(size_t) b] = (midE[b] > 1e-12 && sideE[b] > 1e-12) ? (float) (10.0 * std::log10 (sideE[b] / midE[b])) : -30.0f;
        r.crestDb[(size_t) b] = crestCount > 0 ? (float) (crestAcc[b] / (double) crestCount) : 10.0f;
    }
    r.valid = true;
    return r;
}
} // namespace burnish
