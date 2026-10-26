// VintageCompressor.h — a feedback compressor (the detector listens to the
// output) with a detection filter in the loop, three characters and a
// release that slows down for sustained compression.
#pragma once

#include "../Channels.h"
#include "../Dynamics.h"
#include "../Module.h"

#include <atomic>

namespace burnish
{
class VintageCompressor final : public Module
{
public:
    VintageCompressor();

    int pOn, pChMode, pMode, pAutoGain, pDetType, pDetFreq, pDetGain;
    struct SetIdx { int threshold, ratio, attack, release, gain; };
    SetIdx set[2];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<bool> detSolo { false };

    struct ChannelState
    {
        Detector det;
        Ballistics bal;
        AutoGain ag;
        EnvFollower fast, slow;
        Biquad detF1, detF2;
        float lastOut = 0.0f, sustain = 0.0f;
        float inDb = -90.0f, grDb = 0.0f;   // telemetry
    };
    ChannelState ch[2];

private:
    static constexpr int kStep = 32;
    Smoother gainSm[2];
    float lastDet[3] { -1.0f, -1.0f, -1.0f };
};
} // namespace burnish
