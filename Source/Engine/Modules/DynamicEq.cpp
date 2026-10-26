#include "DynamicEq.h"

namespace burnish
{
DynamicEq::DynamicEq()
    : Module ({ "deq", "DYNAMIC EQ", "DYN EQ", 0xff5ce0d0u, true, true })
{
    pOn     = addBool ("on", "On", true);
    pChMode = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side", "Left/Right" }, Stereo);
    pChanOn[0] = addBool ("a.on", "A On", true);
    pChanOn[1] = addBool ("b.on", "B On", true);
    const float defFreq[kBands] { 80.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f };
    const int defShape[kBands] { ShapeBaxBass, ShapePropQ, ShapePropQ, ShapePropQ, ShapePropQ, ShapeBaxTreble };
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".b" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            auto& bi = band[s][b];
            bi.on        = addBool (p + "on", n + "On", b < 2);
            bi.shape     = addChoice (p + "shape", n + "Shape", { "Baxandall Bass", "Baxandall Treble", "Band Shelf", "Peak Bell", "Proportional Q" }, defShape[b]);
            bi.freq      = addHz (p + "freq", n + "Freq", 20.0f, 20000.0f, defFreq[b]);
            bi.gain      = addDb (p + "gain", n + "Gain", -30.0f, 15.0f, -3.0f);
            bi.q         = addFloat (p + "q", n + "Q", 0.1f, 40.0f, 1.0f, "", 2, 0.0f);
            bi.threshold = addDb (p + "threshold", n + "Threshold", -60.0f, 0.0f, -24.0f);
            bi.direction = addChoice (p + "direction", n + "Direction", { "Up", "Down" }, 1);
            bi.autoScale = addBool (p + "autoscale", n + "Auto Scale", true);
            bi.attack    = addMs (p + "attack", n + "Attack", 0.5f, 500.0f, 20.0f);
            bi.release   = addMs (p + "release", n + "Release", 5.0f, 3000.0f, 200.0f);
            bi.offset    = addDb (p + "offset", n + "Offset", -30.0f, 15.0f, 0.0f);
        }
}

int DynamicEq::toFilterShape (int shape)
{
    switch (shape)
    {
        case ShapeBaxBass:   return LowShelfBaxandall;
        case ShapeBaxTreble: return HighShelfBaxandall;
        case ShapeBandShelf: return BandShelf;
        case ShapeBell:      return Bell;
        default:             return ProportionalQ;
    }
}

void DynamicEq::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    for (auto& row : st) for (auto& b : row) { b.det.prepare ((float) sr); b.det.mode = DetectEnv; }
    reset();
}

void DynamicEq::reset()
{
    for (auto& row : st)
        for (auto& b : row)
        {
            b.filter.reset(); b.detect.reset(); b.det.reset(); b.solo.reset();
            b.amount = 0.0f; b.lastGain = 1e9f; b.lastFreq = -1.0f; b.lastQ = -1.0f; b.lastShape = -1;
        }
    for (auto& row : levelDb) for (auto& v : row) v = -90.0f;
    for (auto& row : gainNowDb) for (auto& v : row) v = 0.0f;
}

float DynamicEq::bandGainDb (int set, int b, bool staticOnly) const
{
    const auto& bi = band[set][b];
    const float g = value (bi.gain);
    const bool up = (int) value (bi.direction) == 0;
    // Static and triggered values of the dynamic part.
    float s, t;
    if (up)  { if (g >= 0.0f) { s = 0.0f; t = g; } else { s = g; t = 0.0f; } }
    else     { if (g >= 0.0f) { s = g; t = 0.0f; } else { s = 0.0f; t = g; } }
    const float amt = staticOnly ? 0.0f : st[set][b].amount;   // display uses channel 0/1 = set in non-stereo
    return value (bi.offset) + s + (t - s) * amt;
}

void DynamicEq::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const int sBand = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    const bool chanOn[2] { value (pChanOn[0]) > 0.5f, value (pChanOn[1]) > 0.5f };

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        // Control rate: detectors, coefficients.
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            for (int b = 0; b < kBands; ++b)
            {
                auto& bs = st[c][b];
                const auto& bi = band[s][b];
                if (value (bi.on) < 0.5f) { bs.filter.kind = 0; bs.amount = 0.0f; continue; }
                const float freq = value (bi.freq), q = value (bi.q);
                const int shape = (int) value (bi.shape);
                if (freq != bs.lastFreq || q != bs.lastQ || shape != bs.lastShape)
                {
                    bs.lastFreq = freq; bs.lastQ = q; bs.lastShape = shape;
                    const int fs = toFilterShape (shape);
                    if (shapeIsLowShelf (fs)) bs.detect.lowPass (freq, 0.7071, sr);
                    else if (shapeIsHighShelf (fs)) bs.detect.highPass (freq, 0.7071, sr);
                    else bs.detect.bandPass (freq, std::max (0.3f, q), sr);
                    if (sBand == b) bs.solo.design (fs, freq, q, sr);
                }
                // The band's dynamic amount, from the level over the last step.
                const float over = levelDb[c][b] - value (bi.threshold);
                const float target = clampf (over / 10.0f, 0.0f, 1.0f);
                float att = value (bi.attack), rel = value (bi.release);
                if (value (bi.autoScale) > 0.5f)
                {
                    const float scale = clampf (std::sqrt (1000.0f / freq), 0.35f, 4.0f);   // slower down low
                    att *= scale; rel *= scale;
                }
                const float coef = tauCoef (target > bs.amount ? att : rel, (float) sr / kStep);
                bs.amount += coef * (target - bs.amount);
                // Design the filter with the gain of this moment. bandGainDb reads
                // st[set] for the amount, so use the channel's own state here.
                const float g = value (bi.gain);
                const bool up = (int) value (bi.direction) == 0;
                float sv, tv;
                if (up)  { if (g >= 0.0f) { sv = 0.0f; tv = g; } else { sv = g; tv = 0.0f; } }
                else     { if (g >= 0.0f) { sv = g; tv = 0.0f; } else { sv = 0.0f; tv = g; } }
                const float gainNow = clampf (value (bi.offset) + sv + (tv - sv) * bs.amount, -30.0f, 15.0f);
                gainNowDb[c][b] = gainNow;
                if (std::fabs (gainNow - bs.lastGain) > 0.01f)
                {
                    bs.lastGain = gainNow;
                    bs.filter.design (toFilterShape (shape), freq, gainNow, q, 3, sr);
                }
            }
        }

        float peak[2][kBands] {};
        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            for (int c = 0; c < 2; ++c)
            {
                const int s = ChannelModes::setFor (chMode, c);
                if (chMode != Stereo && ! chanOn[s]) continue;
                float v = x[c];
                for (int b = 0; b < kBands; ++b)
                {
                    auto& bs = st[c][b];
                    if (bs.filter.kind == 0) continue;
                    // Detect on the input to the band, in its own region.
                    const float d = bs.det.next (bs.detect.tick (v));
                    peak[c][b] = std::max (peak[c][b], d);
                    v = bs.filter.tick (v);
                }
                if (sBand >= 0 && (sSet < 0 || sSet == s || chMode == Stereo))
                    v = st[c][sBand].solo.tick (v);
                x[c] = v;
            }
            ChannelModes::join (chMode, x[0], x[1], l[i], r[i]);
        }
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < kBands; ++b)
                if (st[c][b].filter.kind != 0)
                    levelDb[c][b] = chMode == Stereo ? std::max (peak[0][b], peak[1][b]) : peak[c][b];
    }
}

void DynamicEq::bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool staticOnly) const
{
    const auto& bi = band[set][b];
    if (value (bi.on) < 0.5f) { for (int i = 0; i < count; ++i) outDb[i] = 0.0f; return; }
    FilterBand f;
    f.design (toFilterShape ((int) value (bi.shape)), value (bi.freq), clampf (bandGainDb (set, b, staticOnly), -30.0f, 15.0f), value (bi.q), 3, sr);
    for (int i = 0; i < count; ++i) outDb[i] = (float) f.responseDb (hz[i], sr);
}

void DynamicEq::curveDb (int set, const float* hz, float* outDb, int count, bool staticOnly) const
{
    for (int i = 0; i < count; ++i) outDb[i] = 0.0f;
    std::vector<float> tmp ((size_t) count);
    for (int b = 0; b < kBands; ++b)
    {
        bandCurveDb (set, b, hz, tmp.data(), count, staticOnly);
        for (int i = 0; i < count; ++i) outDb[i] += tmp[(size_t) i];
    }
}
} // namespace burnish
