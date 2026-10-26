// Impact.h — microdynamics per band: one control that expands (positive)
// or compresses (negative) the difference between a fast envelope and a
// slow one, with an ENVELOPE time (ms, or beat divisions when SYNC).
#pragma once

#include "../Channels.h"
#include "../Dynamics.h"
#include "../Module.h"
#include "../Multiband.h"

#include <atomic>

namespace burnish
{
class Impact final : public Module
{
public:
    static constexpr int kBands = 4;

    Impact();

    int pOn, pChMode, pAmount, pStereoLink, pLink, pAutoGain, pSync, pEnvelope, pEnvelopeSync;
    MultibandParams mb;
    int pImpact[2][kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    double hostBpm = 120.0;   // set by the processor

    float gainDb[2][kBands] {};   // [channel][band], latest

    static float syncDivisionSeconds (int idx, double bpm);
    static const char* syncDivisionName (int idx);

private:
    static constexpr int kStep = 32;
    MultibandSplit split;
    struct BandState
    {
        float fast = 0.0f, slow = 0.0f, gainDbSm = 0.0f;
        AutoGain ag;
    };
    BandState st[2][kBands];
    float fastCoef = 0.0f, slowCoef = 0.0f, envCoef = 0.0f;
    Smoother impactSm[2][kBands];
};
} // namespace burnish
