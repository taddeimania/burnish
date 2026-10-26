// Exciter.h — up to four bands of saturation in seven flavours, with a
// post filter on the wet signal and optional 2x/4x oversampling.
#pragma once

#include "../Channels.h"
#include "../Module.h"
#include "../Multiband.h"

#include <atomic>

namespace burnish
{
class Exciter final : public Module
{
public:
    static constexpr int kBands = 4;
    enum Mode { Analog = 0, Retro, Tape, Tube, Warm, Triode, DualTriode, NumModes };

    Exciter();

    int pOn, pChMode, pOversample, pLink, pPostFreq, pPostGain;
    MultibandParams mb;
    struct BandIdx { int mode, amount, mix; };
    BandIdx band[2][kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int latency() const override;

    std::atomic<int> soloBand { -1 };

    // Harmonic energy added per band, 0..1-ish, for the view.
    float harmonicLevel[kBands] {};

    static float shape (int mode, float x, float drive, float& dcState);

private:
    static constexpr int kStep = 32;
    MultibandSplit split;
    struct BandState
    {
        HalfBand os1, os2;
        float dc = 0.0f;              // DC blocker state for the even modes
        TransientDetector trans;
        float harmMs = 0.0f, dryMs = 0.0f;
    };
    BandState st[2][kBands];
    Biquad post[2];
    float lastPost[2] { -1.0f, -1.0f };
    Smoother amountSm[2][kBands], mixSm[2][kBands];
};
} // namespace burnish
