// Spectral.h — the three adaptive spectral modules, all on one STFT
// (2048-point frames, 512 hop) with gains computed per sixth-octave band
// and interpolated across the bins:
//
//   CLARITY         contours the spectrum toward tilted noise, within +-6 dB
//   STABILIZER      corrects toward a genre tonal-balance target (SHAPE),
//                   or only cuts resonances above it (CUT), within 9 dB
//   SPECTRAL SHAPER attenuates narrow peaks that stick out of the spectral
//                   envelope, with attack and release
#pragma once

#include "../Channels.h"
#include "../Module.h"
#include "../Stft.h"

#include <array>
#include <atomic>

namespace burnish
{
// Tonal balance targets, as dB deviations from pink noise at sixth-octave
// centres from 20 Hz to 20 kHz (61 bands). Shared with the assistant.
struct TonalTargets
{
    static constexpr int kBandsPerOctave = 6;
    static constexpr int kNumBands = 61;
    static constexpr int kNumTargets = 11;
    static const char* name (int t);
    // Deviation from pink (band-energy flat) in dB at band b.
    static float curveDb (int target, int band);
    static float centreHz (int band) { return 20.0f * std::pow (2.0f, (float) band / (float) kBandsPerOctave); }
};

class SpectralBase : public Module
{
public:
    static constexpr int kFrame = 2048, kHop = 512;
    static constexpr int kBands = TonalTargets::kNumBands;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return kFrame; }

    // The gains currently applied, dB per band, per channel: for the
    // difference meter.
    float gainDb[2][kBands] {};
    // The smoothed input spectrum (dB, relative) per channel, for display.
    float specDb[2][kBands] {};
    static float bandHz (int b) { return TonalTargets::centreHz (b); }

    int pOn = -1, pChMode = -1;

protected:
    SpectralBase (ModuleInfo m) : Module (std::move (m)) {}
    // Subclasses: given the band energies (dB, this frame) and the
    // smoothed ones, fill gains (dB) for channel c.
    virtual void computeGains (int c, const float* frameDb, float* outGainDb) = 0;
    virtual void frameUpdate() {}   // once per frame, before computeGains
    float frameSeconds() const { return (float) kHop / (float) sr; }

    LogBands bands;
    std::vector<float> binGain[2];
    std::vector<float> energies;
    bool hasStereoAnalysis = true;

private:
    Stft stft;
    void onFrame (Stft::cpx** bins, int chans, int nb);
    int lastChMode = -1;
};

class Clarity final : public SpectralBase
{
public:
    Clarity();
    int pAmount, pTilt, pAttack, pRelease, pLow, pHigh;
protected:
    void computeGains (int c, const float* frameDb, float* outGainDb) override;
private:
    float smooth[2][kBands] {};
    bool primed[2] { false, false };
};

class Stabilizer final : public SpectralBase
{
public:
    Stabilizer();
    int pTarget, pMode, pAmount, pSpeed, pSmoothing, pSensitivity, pTame, pLow, pMid, pHigh;
    // A custom "Assistant" target, set by the assistant from a reference.
    std::array<float, kBands> assistantTarget {};
    std::atomic<bool> hasAssistantTarget { false };
    float targetDb (int band) const;
protected:
    void computeGains (int c, const float* frameDb, float* outGainDb) override;
private:
    float smooth[2][kBands] {};
    float lastLevel[2] {};
    bool primed[2] { false, false };
};

class SpectralShaper final : public SpectralBase
{
public:
    SpectralShaper();
    int pMode, pAmount, pTone, pAttack, pRelease, pLow, pHigh;
protected:
    void computeGains (int c, const float* frameDb, float* outGainDb) override;
private:
    float gr[2][kBands] {};
};
} // namespace burnish
