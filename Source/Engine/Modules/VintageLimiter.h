// VintageLimiter.h — a program limiter in the manner of the classic
// variable-mu units: a soft-kneed, program-dependent gain stage with a
// touch of tube rounding, then a brickwall at the CEILING. THRESHOLD down
// means louder, as make-up brings the threshold up to the ceiling.
#pragma once

#include "../Dynamics.h"
#include "../Limiter.h"
#include "../Module.h"

namespace burnish
{
class VintageLimiter final : public Module
{
public:
    enum Mode { Analog = 0, Tube, Modern };
    VintageLimiter();

    int pOn, pMode, pThreshold, pCeiling, pLink, pTruePeak, pCharacter;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return brick.latency() + 15; }   // + the 2x oversampling path

    float inDb = -90.0f, grDb = 0.0f, brickDb = 0.0f;   // telemetry

private:
    static constexpr int kStep = 32;
    Detector det[2];
    Ballistics bal;
    EnvFollower fast, slow;
    float sustain = 0.0f;
    StereoLimiter brick;
    Smoother threshSm, ceilSm;
    HalfBand os1[2], os2[2];
    Delay offDelay[2];
};
} // namespace burnish
