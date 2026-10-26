// Dynamics.h — up to four bands, each with a compressor and a limiter
// stage on one detector (PEAK, ENV or RMS), soft knee, parallel mix and
// band gain; auto gain, adaptive release and a detection filter (high-pass
// or tilt). STEREO (linked detection) or MID/SIDE.
#pragma once

#include "../Channels.h"
#include "../Dynamics.h"
#include "../Module.h"
#include "../Multiband.h"

#include <atomic>

namespace burnish
{
class Dynamics final : public Module
{
public:
    static constexpr int kBands = 4;
    enum DetFilter { DetNone = 0, DetHighpass = 1, DetTilt = 2 };

    Dynamics();

    int pOn, pChMode, pDetect, pAutoGain, pAdaptive, pLink, pDetFilter, pDetFreq, pDetSlope, pDetTilt, pGlobalGain;
    MultibandParams mb;
    struct BandIdx { int compThresh, compRatio, limThresh, limRatio, attack, release, knee, parallel, gain; };
    BandIdx band[2][kBands];   // [set][band]

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    std::atomic<bool> detSolo { false };

    // Telemetry (written on the audio thread, read for display).
    struct BandTelemetry { float inDb = -90.0f, gainDb = 0.0f, autoDb = 0.0f; };
    BandTelemetry tm[2][kBands];   // [channel][band]

    // Static curve of a band's parameters, for the curve meter.
    float staticGainDb (int set, int b, float levelDb) const;

private:
    static constexpr int kStep = 32;

    MultibandSplit split;
    struct BandState
    {
        Detector det;
        Ballistics bal;
        AutoGain ag;
        float crest = 0.0f;         // for adaptive release
        EnvFollower fastEnv, slowEnv;
    };
    BandState st[2][kBands];        // [channel][band]
    Biquad detHp[2][2], detTiltLo[2], detTiltHi[2];
    float lastDet[4] { -1, -1, -1, -1 };
    Smoother gainSm[2][kBands], globalSm;
    float lastGlobal = 0.0f;
    std::vector<float> scratch;
};
} // namespace burnish
