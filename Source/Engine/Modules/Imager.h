// Imager.h — multiband stereo width (side gain per band), STEREOIZE for
// mono sources (two modes, mono compatible) and RECOVER SIDES, which
// returns what narrowing removed to the middle.
#pragma once

#include "../Module.h"
#include "../Multiband.h"

#include <atomic>

namespace burnish
{
class Imager final : public Module
{
public:
    static constexpr int kBands = 4;

    Imager();

    int pOn, pAmount, pLink, pStereoizeOn, pStereoizeAmount, pStereoizeMode, pRecoverOn, pRecoverGain;
    MultibandParams mb;
    int pWidth[kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    std::atomic<bool> recoverSolo { false };

    // Measured side/mid balance per band of the output, -1 (mono) .. +1 (all side).
    float bandWidth[kBands] {};
    float correlation = 1.0f;     // of the output, -1..1
    float balance = 0.0f;         // -1 left .. +1 right

private:
    static constexpr int kStep = 32;
    MultibandSplit split;
    Smoother widthSm[kBands], stereoizeSm, recoverSm;
    Delay haasM;
    Biquad apA, apB, apC;
    float midMs[kBands] {}, sideMs[kBands] {};
    float ll = 0.0f, rr = 0.0f, lr = 0.0f;
};
} // namespace burnish
