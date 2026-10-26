// Analysis.h — what the assistant listens for: the tonal balance (sixth-
// octave spectrum), loudness, stereo width in four bands and microdynamics
// (crest) in four bands. Fed sample blocks on the audio thread, or offline
// from a reference file; the result compared against a target decides the
// chain the assistant builds.
#pragma once

#include "Dsp.h"
#include "Loudness.h"
#include "Modules/Spectral.h"
#include "Stft.h"

#include <array>

namespace burnish
{
struct AnalysisResult
{
    static constexpr int kBands = TonalTargets::kNumBands;
    std::array<float, kBands> spectrumDb {};   // sixth-octave, relative (mean over 100 Hz..8 kHz = 0)
    float integratedLufs = -70.0f;
    float shortTermMaxLufs = -70.0f;
    std::array<float, 4> widthDb {};           // side/mid energy per band, dB
    std::array<float, 4> crestDb {};           // fast peak over slow RMS per band, dB
    float seconds = 0.0f;
    bool  valid = false;
};

// A profile the assistant aims at: a genre target, or one learned from a
// reference file.
struct Target
{
    std::string name;
    std::array<float, AnalysisResult::kBands> spectrumDb {};   // deviation from pink, like TonalTargets
    float lufs = -14.0f;
    std::array<float, 4> widthDb { -12.0f, -6.0f, -4.0f, -3.0f };
    std::array<float, 4> crestDb { 10.0f, 11.0f, 12.0f, 13.0f };
    bool custom = false;

    static Target genre (int index);   // from TonalTargets, with typical loudness/width/dynamics
    static Target fromAnalysis (const std::string& name, const AnalysisResult&);
};

class Analyzer
{
public:
    static constexpr int kFrame = 4096, kHop = 1024;

    void prepare (double sampleRate);
    void reset();
    void push (const float* l, const float* r, int n);   // accumulate
    AnalysisResult result() const;
    float seconds() const { return (float) samples / (float) sr; }

    // Convert a per-bin spectrum accumulation to the band curve used above.
    static void normalise (std::array<float, AnalysisResult::kBands>& bands);

private:
    double sr = 48000.0;
    long samples = 0;
    // Spectrum: average power per bin of the mono sum.
    Fft fft;
    LogBands bands;
    std::vector<float> window, frameBuf;
    std::vector<Fft::cpx> bins;
    std::vector<double> acc;
    std::vector<float> frameEnergies;
    int frames = 0, inPos = 0, sinceHop = 0;
    std::vector<float> ring;
    // Loudness.
    LoudnessMeter meter;
    float stMax = -70.0f;
    // Width and crest in four bands.
    Crossover4 xoM, xoS;
    double midE[4] {}, sideE[4] {};
    EnvFollower fastEnv[4], slowEnv[4];
    double crestAcc[4] {};
    long crestCount = 0;
    float xoFreqs[3] { 120.0f, 1000.0f, 6000.0f };
};
} // namespace burnish
