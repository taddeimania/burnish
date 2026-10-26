// LowEndFocus.h — CONTRAST between loud and quiet moments in a low
// action region (20..300 Hz): positive sharpens transients and clears
// what sits under them, negative blurs like saturation would. PUNCHY and
// SMOOTH set the speed; GAIN is make-up on the region.
#pragma once

#include "../Channels.h"
#include "../Module.h"

#include <atomic>

namespace burnish
{
class LowEndFocus final : public Module
{
public:
    LowEndFocus();

    int pOn, pChMode, pMode, pContrast[2], pGain[2], pLow, pHigh;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<bool> regionSolo { false };
    float regionGainDb[2] {};   // telemetry: the dynamic gain on the region now

private:
    static constexpr int kStep = 32;
    struct Chan
    {
        LR4Split lo, hi;
        LR4AllPass apLow;   // keeps the part below the region aligned with the split above
        float fast = 0.0f, slow = 0.0f, gDb = 0.0f;
        void reset() { lo.reset(); hi.reset(); apLow.reset(); fast = slow = gDb = 0.0f; }
    };
    Chan ch[2];
    float lastLo = -1.0f, lastHi = -1.0f;
    float fastCoef = 0.0f, slowCoef = 0.0f, gainCoef = 0.0f;
    Smoother gainSm[2];
};
} // namespace burnish
