// DynamicEq.h — six bands whose gain moves with the level in their region:
// UP or DOWN when the band's detector crosses the THRESHOLD, with attack
// and release (AUTO SCALE ties them to the frequency) and a static OFFSET.
#pragma once

#include "../Channels.h"
#include "../Dynamics.h"
#include "../Filters.h"
#include "../Module.h"

#include <atomic>

namespace burnish
{
class DynamicEq final : public Module
{
public:
    static constexpr int kBands = 6;
    enum Shape { ShapeBaxBass = 0, ShapeBaxTreble, ShapeBandShelf, ShapeBell, ShapePropQ, NumShapes };

    DynamicEq();

    int pOn, pChMode, pChanOn[2];
    struct BandIdx { int on, shape, freq, gain, q, threshold, direction, autoScale, attack, release, offset; };
    BandIdx band[2][kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 }, soloSet { -1 };

    // Telemetry per channel and band: detector level and the gain now.
    float levelDb[2][kBands] {};
    float gainNowDb[2][kBands] {};

    static int toFilterShape (int shape);
    // Response (dB) of a set at the given frequencies from the current
    // dynamic state (for the curve). `staticOnly` ignores the dynamic part.
    void curveDb (int set, const float* hz, float* outDb, int count, bool staticOnly) const;
    void bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool staticOnly) const;
    float bandGainDb (int set, int b, bool staticOnly) const;

private:
    static constexpr int kStep = 32;
    struct BandState
    {
        FilterBand filter;
        Biquad detect;
        Detector det;
        float amount = 0.0f;   // 0..1 how far toward the triggered gain
        float lastGain = 1e9f, lastFreq = -1.0f, lastQ = -1.0f;
        int lastShape = -1;
        SoloFilter solo;
    };
    BandState st[2][kBands];
};
} // namespace burnish
