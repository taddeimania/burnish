// BassControl.h — everything below a CUTOFF: BALANCE (gain), PUNCH (gain
// on the transients of the low band), SUSTAIN POWER (upward compression
// below a threshold, DIRTY to CLEAN) and PEAK CONTROL (a limiter at a
// threshold, DIRTY clipping to CLEAN limiting). Meters report the bass
// balance against the rest, and the low band's crest factor.
#pragma once

#include "../Dynamics.h"
#include "../Limiter.h"
#include "../Module.h"

#include <atomic>

namespace burnish
{
class BassControl final : public Module
{
public:
    BassControl();

    int pOn, pCutoff, pZoom, pBalanceOn, pBalance, pPunchOn, pPunch,
        pSustainOn, pSustainThresh, pSustainChar, pSustainAmount,
        pPeakOn, pPeakThresh, pPeakChar;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return limiter.latency(); }

    std::atomic<bool> bassSolo { false };

    // Telemetry.
    float balanceDb = 0.0f;     // low band energy against the rest, dB
    float crestDb = 10.0f;      // peak over RMS of the low band, dB
    float sustainDb = 0.0f;     // boost now
    float peakGrDb = 0.0f;      // limiting now
    // The low band (mono, after processing) for the oscilloscope: a ring.
    static constexpr int kScopeSize = 8192;
    float scope[kScopeSize] {};
    std::atomic<int> scopeWrite { 0 };

private:
    static constexpr int kStep = 32;
    struct Chan
    {
        LR4Split split;
        TransientDetector trans;
        Delay restDelay;
        float dc = 0.0f;
        void reset() { split.reset(); trans.reset(); restDelay.reset(); dc = 0.0f; }
    };
    Chan ch[2];
    float lastCutoff = -1.0f;
    Detector susDet;
    float susGainDb = 0.0f, susAtt = 0.0f, susRel = 0.0f;
    StereoLimiter limiter;
    Smoother balSm, punchSm;
    float lowMs = 0.0f, restMs = 0.0f, lowPeak = 0.0f;
    HalfBand os1[2], os2[2];
    Delay cleanDelay[2];
};
} // namespace burnish
