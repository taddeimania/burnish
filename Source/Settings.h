// Settings.h — per-user preferences shared by every Burnish on the machine:
// tooltips, favourite targets, the assistant's saved defaults and the
// analyser options. User presets and targets live in folders beside this
// file.
//
//   macOS:   ~/Library/Application Support/Burnish/Burnish.settings
//   Windows: %APPDATA%\Burnish\Burnish.settings
//   Linux:   ~/.config/Burnish/Burnish.settings
//
// Message thread only.
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace burnish
{
struct SettingsStore
{
    SettingsStore();
    juce::PropertiesFile props;
};

class Settings
{
public:
    bool tooltipsEnabled() const;          // default: on
    void setTooltipsEnabled (bool);

    bool genreFavourite (int index) const;
    void setGenreFavourite (int index, bool);

    // Analyser display options.
    int  spectrumAverage() const;          // 0 real time, 1 one second, 2 three seconds, 3 ten seconds
    void setSpectrumAverage (int);
    bool spectrumPeakHold() const;
    void setSpectrumPeakHold (bool);
    float spectrumTilt() const;            // dB/oct, default 3 (pink reads flat)
    void setSpectrumTilt (float);

    juce::String assistantDefaults() const;        // XML text, or empty
    void setAssistantDefaults (const juce::String&);

    static juce::File file();

private:
    juce::SharedResourcePointer<SettingsStore> store;
};
} // namespace burnish
