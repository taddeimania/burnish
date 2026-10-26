#include "Settings.h"

namespace burnish
{
namespace
{
juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName         = "Burnish";
    o.filenameSuffix          = ".settings";
   #if JUCE_LINUX
    o.folderName              = ".config/Burnish";
   #else
    o.folderName              = "Burnish";
   #endif
    o.osxLibrarySubFolder     = "Application Support";
    o.storageFormat           = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1;   // every change is saved explicitly
    return o;
}
} // namespace

SettingsStore::SettingsStore() : props (options()) {}

juce::File Settings::file() { return options().getDefaultFile(); }

bool Settings::tooltipsEnabled() const { return store->props.getBoolValue ("tooltips", true); }
void Settings::setTooltipsEnabled (bool on) { store->props.setValue ("tooltips", on); store->props.saveIfNeeded(); }

bool Settings::genreFavourite (int index) const { return store->props.getBoolValue ("fav" + juce::String (index), false); }
void Settings::setGenreFavourite (int index, bool on) { store->props.setValue ("fav" + juce::String (index), on); store->props.saveIfNeeded(); }

int  Settings::spectrumAverage() const { return store->props.getIntValue ("spectrumAverage", 1); }
void Settings::setSpectrumAverage (int v) { store->props.setValue ("spectrumAverage", v); store->props.saveIfNeeded(); }
bool Settings::spectrumPeakHold() const { return store->props.getBoolValue ("spectrumPeakHold", true); }
void Settings::setSpectrumPeakHold (bool v) { store->props.setValue ("spectrumPeakHold", v); store->props.saveIfNeeded(); }
float Settings::spectrumTilt() const { return (float) store->props.getDoubleValue ("spectrumTilt", 3.0); }
void Settings::setSpectrumTilt (float v) { store->props.setValue ("spectrumTilt", v); store->props.saveIfNeeded(); }

juce::String Settings::assistantDefaults() const { return store->props.getValue ("assistantDefaults"); }
void Settings::setAssistantDefaults (const juce::String& xml) { store->props.setValue ("assistantDefaults", xml); store->props.saveIfNeeded(); }
} // namespace burnish
