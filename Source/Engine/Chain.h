// Chain.h — the whole signal path, without JUCE:
//
//   in -> INPUT GAIN -> [modules, in the chosen order] -> OUTPUT GAIN
//      -> GAIN MATCH -> MONO / SWAP -> DITHER -> out
//
// Every module exists once (the Equalizer twice); the chain `order` says
// which are in use and where. SOLO plays one module alone, DELTA plays the
// difference one module makes, BYPASS plays the input delayed to line up.
// The assistant's analyser and the panel's analyser taps hang off it.
#pragma once

#include "Analysis.h"
#include "Dither.h"
#include "Loudness.h"
#include "Module.h"
#include "Modules/BassControl.h"
#include "Modules/DynamicEq.h"
#include "Modules/Dynamics.h"
#include "Modules/Equalizer.h"
#include "Modules/Exciter.h"
#include "Modules/Imager.h"
#include "Modules/Impact.h"
#include "Modules/LowEndFocus.h"
#include "Modules/MatchEq.h"
#include "Modules/Maximizer.h"
#include "Modules/Spectral.h"
#include "Modules/VintageCompressor.h"
#include "Modules/VintageEq.h"
#include "Modules/VintageLimiter.h"
#include "Modules/VintageTape.h"

#include <array>
#include <atomic>
#include <memory>

namespace burnish
{
enum ModuleId
{
    ModBassControl = 0, ModClarity, ModDynamicEq, ModDynamics, ModEq1, ModEq2, ModExciter, ModImager, ModImpact,
    ModLowEndFocus, ModMatchEq, ModMaximizer, ModSpectralShaper, ModStabilizer, ModVintageCompressor,
    ModVintageEq, ModVintageLimiter, ModVintageTape, NumModules
};
constexpr int kMaxChain = NumModules;

struct ChainSettings
{
    float inGainDb[2] { 0.0f, 0.0f };
    float outGainDb[2] { 0.0f, 0.0f };
    bool bypass = false, gainMatch = false, mono = false, swap = false;
    bool dcFilter = false;
    Dither::Settings dither;
    int soloModule = -1;      // ModuleId or -1
    int deltaModule = -1;
    bool analysing = false;   // feed the assistant's analyser
    int meterType = 0;        // 0 peak, 1 RMS, 2 loudness (short-term)
    bool meterMidSide = false;
};

// Lock-free ring of stereo samples for the panel's analyser and scopes.
class Tap
{
public:
    static constexpr int kSize = 16384;
    void push (const float* l, const float* r, int n)
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) (w % kSize)] = l[i];
            right[(size_t) (w % kSize)] = r[i];
            ++w;
        }
        writePos.store (w, std::memory_order_release);
    }
    // Copies the latest `count` samples (oldest first).
    void latest (float* l, float* r, int count) const
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const int idx = ((w - count + i) % kSize + kSize) % kSize;
            l[i] = left[(size_t) idx];
            r[i] = right[(size_t) idx];
        }
    }
    int position() const { return writePos.load (std::memory_order_acquire); }
    void clear() { left.fill (0.0f); right.fill (0.0f); writePos.store (0); }

private:
    std::array<float, kSize> left {}, right {};
    std::atomic<int> writePos { 0 };
};

struct MeterReadings
{
    float peak[2] { 0.0f, 0.0f }, rms[2] { 0.0f, 0.0f };   // linear, since last read (peak) / averaged (rms)
    float lufsShort = -70.0f, lufsMomentary = -70.0f, lufsIntegrated = -70.0f, truePeakDb = -100.0f;
    bool clipped[2] { false, false };
};

class Chain
{
public:
    Chain();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* l, float* r, int n, const ChainSettings&);

    // Modules.
    Module& module (int id) { return *mods[(size_t) id]; }
    const Module& module (int id) const { return *mods[(size_t) id]; }
    static const char* moduleName (int id);
    static const char* moduleShortName (int id);

    // Typed access for the panels.
    Equalizer& eq (int instance) { return instance == 1 ? eq1 : eq2; }
    Equalizer eq1 { 1 }, eq2 { 2 };
    DynamicEq dynamicEq;
    Dynamics dynamics;
    Exciter exciter;
    Imager imager;
    Impact impact;
    LowEndFocus lowEndFocus;
    Clarity clarity;
    Stabilizer stabilizer;
    SpectralShaper spectralShaper;
    MatchEq matchEq;
    Maximizer maximizer;
    VintageCompressor vintageCompressor;
    VintageEq vintageEq;
    VintageLimiter vintageLimiter;
    VintageTape vintageTape;
    BassControl bassControl;

    // The order (message thread writes under the lock; the audio thread
    // copies it at the start of each block).
    void setOrder (const std::vector<int>& ids);
    std::vector<int> getOrder() const;
    bool inChain (int id) const;
    int  latency() const { return latencyNow.load (std::memory_order_relaxed); }

    // Meters and taps, read by the panel.
    MeterReadings takeInputMeters();
    MeterReadings takeOutputMeters();
    void resetMeters();
    float moduleGainDb (int id) const { return diffDb[(size_t) id]; }   // difference meter
    float gainMatchDb() const { return matchDb; }
    Tap inputTap, outputTap, moduleTap;
    std::atomic<int> tapModule { -1 };   // which module's output feeds moduleTap (-1: output)

    Analyzer analyser;   // fed while settings.analysing
    Dither dither;

    // Current tempo from the host, for synced modules.
    void setBpm (double bpm) { impact.hostBpm = bpm; }

private:
    void updateLatency();

    double sr = 48000.0;
    int maxBlock = 512;
    std::array<Module*, NumModules> mods {};

    // Order handoff.
    mutable std::atomic<bool> orderLock { false };
    std::array<int, kMaxChain> orderShared {};
    int orderSharedCount = 0;
    std::array<int, kMaxChain> order {};
    int orderCount = 0;
    std::atomic<int> latencyNow { 0 };

    // Per-module in/out levels for the difference meters.
    float inMs[NumModules] {}, outMs[NumModules] {};
    float diffDb[NumModules] {};

    Delay dryDelay[2];
    Biquad dcHp[2];
    std::vector<float> dryL, dryR, deltaL, deltaR, dlTmp, drTmp;
    std::vector<float> deltaHistL, deltaHistR;   // the delta module's input, for its latency
    int deltaHistPos = 0;
    LoudnessMeter matchMeter;
    Smoother inSm[2], outSm[2], bypassSm, monoSm, matchSm;
    LoudnessMeter inMeter, outMeter;
    MeterReadings inRead, outRead;
    float inPeakHold[2] {}, outPeakHold[2] {};
    float inMsM[2] {}, outMsM[2] {};
    float matchDb = 0.0f;
    bool  clipIn[2] {}, clipOut[2] {};
    int   lastDeltaModule = -1;
};
} // namespace burnish
