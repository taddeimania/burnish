// VintageTape.h — the frequency response and saturation of a well-kept
// two-track machine: SPEED sets the head bump and the top-end reach, INPUT
// DRIVE pushes into a rounded saturation (level compensated), BIAS shifts
// the distortion toward the highs or toward compression, HARMONICS adds
// even-order colour, LOW and HIGH EMPHASIS shape the bump and the air.
#pragma once

#include "../Module.h"

namespace burnish
{
class VintageTape final : public Module
{
public:
    VintageTape();

    int pOn, pSpeed, pDrive, pBias, pHarmonics, pLowEmph, pHighEmph;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return 15; }

    void curveDb (const float* hz, float* outDb, int count) const;   // the linear part
    float satDb = 0.0f;   // telemetry: peak rounding now

private:
    static constexpr int kStep = 32;
    struct Chan
    {
        Biquad bump, hfRoll, hfEmph, preTilt, postTilt, dcBlock;
        HalfBand os1, os2;
        float dc = 0.0f;
        void reset() { bump.reset(); hfRoll.reset(); hfEmph.reset(); preTilt.reset(); postTilt.reset(); dcBlock.reset(); os1.reset(); os2.reset(); dc = 0.0f; }
    };
    Chan ch[2];
    Delay offDelay[2];
    float last[6] { -1, -1, -1, -1, -1, -1 };
    Smoother driveSm;
    float peakIn = 0.0f, peakOut = 0.0f;
    void design();
};
} // namespace burnish
