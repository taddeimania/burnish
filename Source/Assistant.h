// Assistant.h — ASSIST: listens to the track for a while, compares what
// it heard with a target (a genre, or a reference file of your own) and
// builds a chain to close the gap: EQ for tonal balance, Clarity and
// Stabilizer for detail and consistency, Impact for microdynamics, Imager
// for width, and the Maximizer for loudness. Message thread.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/Analysis.h"
#include "Engine/Chain.h"

#include <functional>

namespace burnish
{
struct AssistantSettings
{
    int targetIndex = 1;            // index into Assistant::targets()
    int intensity = 2;              // 0 Subtle .. 4 Transformative
    float loudnessLufs = -12.0f;
    int destination = 1;            // 0 full scale (-0.1 dB, no true peak), 1 streaming (-1 dB, true peak)
    float analysisSeconds = 12.0f;
    bool tempoSync = false;
    int analysisBars = 8;
    bool useEq = true, useClarity = true, useStabilizer = true, useImpact = true, useImager = true, useMaximizer = true, useBass = true;
};

class Assistant
{
public:
    // The processor supplies these: set a parameter by id (plain value),
    // and replace the chain order.
    std::function<void (const juce::String&, float)> setParam;
    std::function<void (const std::vector<int>&)> setOrder;

    explicit Assistant (Chain& chain);

    // Targets: the genres, then any custom ones from reference files.
    const std::vector<Target>& targets() const { return all; }
    int numGenreTargets() const { return TonalTargets::kNumTargets; }
    void loadCustomTargets();
    bool addReferenceTarget (const juce::File& audioFile, juce::String& error);   // analyses the loudest 8 s
    void removeCustomTarget (int index);
    bool isFavourite (int index) const;
    void setFavourite (int index, bool);

    // The run.
    void start (const AssistantSettings&, double hostBpm);
    void cancel();
    bool analysing() const { return running; }
    float progress() const;                // 0..1 while analysing
    void tick();                           // call from a timer; finishes the analysis
    bool hasResult() const { return lastResult.valid; }
    const AnalysisResult& result() const { return lastResult; }
    const AssistantSettings& settings() const { return current; }
    const Target& target() const { return all[(size_t) juce::jlimit (0, (int) all.size() - 1, current.targetIndex)]; }
    bool built() const { return hasBuilt; }

    // After a run: change the target (rebuilds from the stored analysis).
    void retarget (int targetIndex);
    // The Maximizer gain the build chose, so the +-4 dB control has a centre.
    float builtMaximizerGain() const { return maxGainBuilt; }
    // Summary lines for the panel.
    juce::String summary() const { return summaryText; }

    static juce::File targetsFolder();
    static const char* intensityName (int i);

private:
    void build();
    static float intensityFactor (int i);

    Chain& chain;
    std::vector<Target> all;
    std::vector<bool> favourites;
    AssistantSettings current;
    bool running = false, hasBuilt = false;
    AnalysisResult lastResult;
    float maxGainBuilt = 0.0f;
    double bpm = 120.0;
    juce::String summaryText;
};
} // namespace burnish
