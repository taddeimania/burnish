#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#include "Assistant.h"
#include "Engine/Chain.h"
#include "Engine/Presets.h"
#include "Params.h"

class BurnishProcessor final : public juce::AudioProcessor,
                               private juce::Timer,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    BurnishProcessor();
    ~BurnishProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, factoryIndex); }
    void setCurrentProgram (int index) override { loadFactory (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- parameters (message thread) ---------------------------------------------
    void setParam (const juce::String& id, float plainValue);
    float getParam (const juce::String& id) const;
    int   geti (const juce::String& id) const { return juce::roundToInt (getParam (id)); }
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    juce::String paramId (const burnish::Module& m, int index) const { return m.params()[(size_t) index].id; }

    // --- the chain ----------------------------------------------------------------------
    burnish::Chain chain;
    std::vector<int> getOrder() const { return chain.getOrder(); }
    void setOrder (const std::vector<int>& order);
    void addModule (int id, int position = -1);
    void removeModule (int id);
    void moveModule (int id, int newPosition);
    void resetModule (int id);                 // every parameter to its default

    // Listening.
    std::atomic<int> soloModule { -1 }, deltaModule { -1 };

    // --- presets --------------------------------------------------------------------------
    void loadFactory (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name, juce::String& error);
    void stepPreset (int delta);
    juce::String presetName() const { return currentName; }
    int  presetNumber() const;
    bool presetEdited() const;
    void loadModulePreset (int module, const burnish::ModulePreset&);
    bool loadUserModulePreset (int module, const juce::File&);
    bool saveUserModulePreset (int module, const juce::String& name, juce::String& error);
    static juce::File userPresetFolder();
    static juce::File userModulePresetFolder (int module);
    static juce::Array<juce::File> userPresets();
    static juce::Array<juce::File> userModulePresets (int module);

    // --- undo and snapshots ---------------------------------------------------------------
    void undo();
    bool canUndo() const { return ! undoStack.empty(); }
    void setSnapshot (int slot);          // A..D = 0..3
    void recallSnapshot (int slot);
    bool hasSnapshot (int slot) const { return snapshots[(size_t) slot] != nullptr; }
    int  activeSnapshot() const { return activeSnap; }

    // --- assistant ----------------------------------------------------------------------------
    burnish::Assistant assistant;
    burnish::AssistantSettings assistantSettings;
    void startAssistant();
    double hostBpm() const { return bpm.load(); }

    // Meter resets (message thread).
    void resetMeters() { meterResets.fetch_add (1); }
    void resetClips() { clipResets.fetch_add (1); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override;
    void cacheParams();
    void readSettings();
    void pushUndo();
    std::unique_ptr<juce::XmlElement> stateXml();
    void applyStateXml (const juce::XmlElement&, bool includeOrder);
    std::vector<float> snapshot() const;
    void syncOrderFromState();

    // Module parameter pointers, in each module's own order.
    std::vector<std::vector<std::atomic<float>*>> moduleParams;
    struct
    {
        std::atomic<float>* inL = nullptr; std::atomic<float>* inR = nullptr; std::atomic<float>* outL = nullptr; std::atomic<float>* outR = nullptr;
        std::atomic<float>* bypass = nullptr; std::atomic<float>* gainMatch = nullptr; std::atomic<float>* mono = nullptr; std::atomic<float>* swap = nullptr;
        std::atomic<float>* meterType = nullptr; std::atomic<float>* meterMs = nullptr;
        std::atomic<float>* ditherOn = nullptr; std::atomic<float>* ditherBits = nullptr; std::atomic<float>* ditherBlank = nullptr;
        std::atomic<float>* ditherAmount = nullptr; std::atomic<float>* ditherHarmonic = nullptr; std::atomic<float>* ditherLimit = nullptr;
        std::atomic<float>* ditherShaping = nullptr; std::atomic<float>* dcFilter = nullptr;
    } fast;
    burnish::ChainSettings settings;
    std::atomic<bool> analysing { false };
    std::atomic<int> meterResets { 0 }, clipResets { 0 };
    int seenMeterResets = 0, seenClipResets = 0;
    std::atomic<double> bpm { 120.0 };
    juce::AudioBuffer<float> monoScratch;
    int lastLatency = -1;
    bool wasPlaying = false;

    // Presets.
    int factoryIndex = 0;
    juce::File userFile;
    juce::String currentName;
    std::vector<float> loadedValues;
    std::vector<int> loadedOrder;

    // Undo.
    std::vector<std::unique_ptr<juce::XmlElement>> undoStack;
    std::unique_ptr<juce::XmlElement> snapshots[4];
    int activeSnap = -1;
    std::atomic<bool> dirty { false };
    bool applying = false;
    double lastPushMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BurnishProcessor)
};
