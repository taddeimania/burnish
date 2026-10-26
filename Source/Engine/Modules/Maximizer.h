// Maximizer.h — the loudness stage: GAIN into a look-ahead true-peak
// limiter with six release behaviours (LOW LATENCY, I, II, III with four
// styles, IV with three styles, V multiband), an OUTPUT LEVEL ceiling,
// CHARACTER (speed), UPWARD COMPRESS, SOFT CLIP (4x oversampled, three
// knees), TRANSIENT EMPHASIS, STEREO INDEPENDENCE for transients and
// sustain separately, and LEARN to a target loudness.
#pragma once

#include "../Dynamics.h"
#include "../Limiter.h"
#include "../Loudness.h"
#include "../Module.h"

#include <atomic>

namespace burnish
{
class Maximizer final : public Module
{
public:
    enum Mode { LowLatency = 0, Irc1, Irc2, Irc3, Irc4, Irc5, NumModes };
    enum ClipMode { Light = 0, Moderate, Heavy };

    Maximizer();

    int pOn, pMode, pStyle3, pStyle4, pGain, pOutput, pLink, pTruePeak, pCharacter, pUpward,
        pClipOn, pClipAmount, pClipMode, pTransOn, pTransAmount, pStereoTrans, pStereoSustain, pStereoLink, pTargetLufs;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override;

    // LEARN: press (message thread) -> listens for five seconds -> sets GAIN.
    std::atomic<int> learnRequests { 0 };
    bool learning() const noexcept { return isLearning; }
    float learnProgress() const noexcept { return learnSeconds / kLearnSeconds; }
    // A gain the learn pass decided on; the processor applies it to the parameter.
    std::atomic<bool> learnedReady { false };
    std::atomic<float> learnedGain { 0.0f };

    // Telemetry.
    float grDb = 0.0f, grLDb = 0.0f, grRDb = 0.0f;   // limiter gain reduction (dB, positive numbers)
    float upwardDb = 0.0f;                         // boost from UPWARD COMPRESS now
    float clipDb = 0.0f;                           // peak reduction from SOFT CLIP now
    float outTruePeakDb = -100.0f;

    static constexpr float kLearnSeconds = 5.0f;
    static const char* characterText (float character);   // "CLIPPING" .. "VERY SLOW"

private:
    static constexpr int kStep = 32;
    int windowForMode (int mode) const;
    void updateTimes();

    StereoLimiter limiter;
    // Multiband pre-limiting for modes IV and V.
    Crossover4 mbSplit[2];
    StereoLimiter bandLimiter[4];
    int lastMode = -1;

    // Upward compression.
    Detector upDet[2];
    float upGainDb[2] {};
    float upAtt = 0.0f, upRel = 0.0f;

    // Soft clip.
    HalfBand clipOs1[2], clipOs2[2];
    Delay clipBypass[2];
    float clipPeakIn = 0.0f, clipPeakOut = 0.0f;

    // Transient emphasis.
    TransientDetector trans[2];
    float transGainSm[2] {};

    // Stereo independence.
    TransientDetector linkTrans;

    // Release behaviour.
    float relFast = 0.0f, relSlow = 0.0f;   // coefficients
    float sustainMeasure = 0.0f;

    Smoother gainSm, outSm, charSm;
    LoudnessMeter outMeter;
    bool isLearning = false;
    int seenLearn = 0;
    float learnSeconds = 0.0f;
    float learnGainAtStart = 0.0f;
};
} // namespace burnish
