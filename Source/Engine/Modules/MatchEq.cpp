#include "MatchEq.h"

namespace burnish
{
MatchEq::MatchEq()
    : Module ({ "mtc", "MATCH EQ", "MATCH", 0xffd9b85cu, true, false })
{
    pOn        = addBool ("on", "On", true);
    pChMode    = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pSmoothing = addPct ("smoothing", "Smoothing", 0.0f, 100.0f, 50.0f);
    pAmount    = addPct ("amount", "Amount", 0.0f, 100.0f, 40.0f);
    pLow       = addHz ("low", "Low Cutoff", 20.0f, 20000.0f, 20.0f);
    pHigh      = addHz ("high", "High Cutoff", 20.0f, 20000.0f, 20000.0f);
    referenceDb.assign (kBins, -200.0f);
    applyDb.assign (kBins, -200.0f);
    curveDb.assign (kBins, 0.0f);
    refAcc.assign (kBins, 0.0);
    applyAcc.assign (kBins, 0.0);
    binGain.assign (kBins, 1.0f);
}

void MatchEq::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    stft.prepare (kFrame, kHop, 2);
    reset();
}

void MatchEq::reset()
{
    stft.reset();
    lastParams[0] = -1.0f;
}

void MatchEq::clearReference()
{
    std::fill (refAcc.begin(), refAcc.end(), 0.0);
    refFrames = 0;
    std::fill (referenceDb.begin(), referenceDb.end(), -200.0f);
    if (capture.load() == CaptureReference) capture.store (Idle);
}

void MatchEq::clearApply()
{
    std::fill (applyAcc.begin(), applyAcc.end(), 0.0);
    applyFrames = 0;
    std::fill (applyDb.begin(), applyDb.end(), -200.0f);
    if (capture.load() == CaptureApply) capture.store (Idle);
}

void MatchEq::setReferenceDb (const std::vector<float>& db)
{
    if ((int) db.size() != kBins) return;
    referenceDb = db;
    refFrames = 1;
    for (int k = 0; k < kBins; ++k) refAcc[(size_t) k] = std::pow (10.0, db[(size_t) k] * 0.1);
}

void MatchEq::setApplyDb (const std::vector<float>& db)
{
    if ((int) db.size() != kBins) return;
    applyDb = db;
    applyFrames = 1;
    for (int k = 0; k < kBins; ++k) applyAcc[(size_t) k] = std::pow (10.0, db[(size_t) k] * 0.1);
}

void MatchEq::rebuildCurve()
{
    const bool have = refFrames > 0 && applyFrames > 0;
    const float smoothing = value (pSmoothing) * 0.01f;
    const float amount = value (pAmount) * 0.01f;
    const float lo = value (pLow), hi = std::max (value (pLow) * 1.01f, value (pHigh));

    // Raw difference per bin, in dB.
    std::vector<float>& raw = curveDb;
    for (int k = 0; k < kBins; ++k)
    {
        raw[(size_t) k] = 0.0f;
        if (! have) continue;
        const float rdb = referenceDb[(size_t) k], adb = applyDb[(size_t) k];
        if (rdb > -150.0f && adb > -150.0f)
            raw[(size_t) k] = clampf (rdb - adb, -24.0f, 24.0f);
    }
    if (have)
    {
        // Level-match: remove the mean difference in the 100 Hz .. 8 kHz region.
        double sum = 0; int cnt = 0;
        for (int k = 1; k < kBins; ++k)
        {
            const float hz = binHz (k, sr);
            if (hz >= 100.0f && hz <= 8000.0f) { sum += raw[(size_t) k]; ++cnt; }
        }
        const float mean = cnt > 0 ? (float) (sum / cnt) : 0.0f;
        for (int k = 0; k < kBins; ++k) raw[(size_t) k] -= mean;

        // Smooth in log frequency: a window of +-(smoothing * 0.5) octaves.
        const float octaves = 0.02f + smoothing * 0.5f;
        std::vector<float> sm (kBins, 0.0f);
        for (int k = 1; k < kBins; ++k)
        {
            const float hz = binHz (k, sr);
            const int klo = std::max (1, (int) (hz / std::pow (2.0f, octaves) * kFrame / sr));
            const int khi = std::min (kBins - 1, (int) (hz * std::pow (2.0f, octaves) * kFrame / sr) + 1);
            double s = 0; int c = 0;
            for (int j = klo; j <= khi; ++j) { s += raw[(size_t) j]; ++c; }
            sm[(size_t) k] = c > 0 ? (float) (s / c) : raw[(size_t) k];
        }
        for (int k = 0; k < kBins; ++k)
        {
            const float hz = binHz (k, sr);
            const float edge = std::min (clampf (std::log2 (std::max (hz, 1.0f) / lo) * 2.0f + 1.0f, 0.0f, 1.0f),
                                         clampf (std::log2 (hi / std::max (hz, 1.0f)) * 2.0f + 1.0f, 0.0f, 1.0f));
            raw[(size_t) k] = sm[(size_t) k] * amount * edge;
        }
    }
    for (int k = 0; k < kBins; ++k)
        binGain[(size_t) k] = dbToGain (raw[(size_t) k]);
}

void MatchEq::process (float* l, float* r, int n)
{
    const int chMode = (int) value (pChMode);
    const bool on = value (pOn) > 0.5f;
    const int cap = capture.load (std::memory_order_relaxed);

    const float pv[4] { value (pSmoothing), value (pAmount), value (pLow), value (pHigh) };
    bool dirty = false;
    for (int i = 0; i < 4; ++i) if (pv[i] != lastParams[i]) { dirty = true; lastParams[i] = pv[i]; }
    if (refFrames != lastRef || applyFrames != lastApply) { dirty = true; lastRef = refFrames; lastApply = applyFrames; }
    sinceRebuild += n;
    if (dirty && (sinceRebuild >= 2048 || lastRef < 0)) { rebuildCurve(); sinceRebuild = 0; }

    for (int i = 0; i < n; ++i)
        ChannelModes::split (chMode, l[i], r[i], l[i], r[i]);
    float* d[2] { l, r };
    stft.process (d, n, [this, on, cap] (Stft::cpx** bins, int chans, int nb)
    {
        if (cap != Idle)
        {
            auto& acc = cap == CaptureReference ? refAcc : applyAcc;
            int& frames = cap == CaptureReference ? refFrames : applyFrames;
            auto& db = cap == CaptureReference ? referenceDb : applyDb;
            for (int k = 0; k < nb; ++k)
            {
                double e = 0;
                for (int c = 0; c < chans; ++c) e += std::norm (bins[c][k]);
                acc[(size_t) k] += e / chans;
            }
            ++frames;
            if ((frames & 7) == 0)
                for (int k = 0; k < nb; ++k)
                    db[(size_t) k] = 10.0f * std::log10 ((float) (acc[(size_t) k] / frames) + 1e-12f);
        }
        if (on)
            for (int c = 0; c < chans; ++c)
                for (int k = 0; k < nb; ++k)
                    bins[c][k] *= binGain[(size_t) k];
    });
    for (int i = 0; i < n; ++i)
        ChannelModes::join (chMode, l[i], r[i], l[i], r[i]);
}
} // namespace burnish
