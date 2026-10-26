// Params.h — the host parameters: every module's, from its descriptions,
// plus the global I/O, listening and dither controls.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Chain.h"

namespace burnish::ids
{
inline constexpr const char* inGainL   = "io.in.l";
inline constexpr const char* inGainR   = "io.in.r";
inline constexpr const char* inLink    = "io.in.link";
inline constexpr const char* outGainL  = "io.out.l";
inline constexpr const char* outGainR  = "io.out.r";
inline constexpr const char* outLink   = "io.out.link";
inline constexpr const char* bypass    = "io.bypass";
inline constexpr const char* gainMatch = "io.gainmatch";
inline constexpr const char* mono      = "io.mono";
inline constexpr const char* swap      = "io.swap";
inline constexpr const char* meterType = "io.metertype";
inline constexpr const char* meterMs   = "io.meterms";
inline constexpr const char* ditherOn  = "dither.on";
inline constexpr const char* ditherBits = "dither.bits";
inline constexpr const char* ditherBlank = "dither.autoblank";
inline constexpr const char* ditherAmount = "dither.amount";
inline constexpr const char* ditherHarmonic = "dither.harmonic";
inline constexpr const char* ditherLimit = "dither.limitpeaks";
inline constexpr const char* ditherShaping = "dither.shaping";
inline constexpr const char* dcFilter  = "dither.dcfilter";

// Not part of a preset: listening controls.
inline bool notInPreset (const juce::String& id)
{
    return id == bypass || id == mono || id == swap || id == gainMatch;
}

inline juce::String decibels (float v, int places = 1)
{
    const float k = std::pow (10.0f, (float) places);
    const float r = std::round (v * k) / k;
    return (r > 0.0f ? "+" : "") + juce::String (r, places) + " dB";
}
inline juce::String hertz (float v)
{
    if (v >= 10000.0f) return juce::String (v / 1000.0f, 1) + " kHz";
    if (v >= 1000.0f)  return juce::String (v / 1000.0f, 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}
inline juce::String millis (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " s";
    return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
}
inline juce::String ratio (float v)
{
    if (v < 0.0f) return "(" + juce::String (-v, 1) + "):1";
    return juce::String (v, 1) + ":1";
}
} // namespace burnish::ids

namespace burnish
{
// Builds a host parameter from a module description.
inline std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamDesc& d)
{
    using namespace juce;
    const ParameterID pid { d.id, 1 };
    if (d.kind == ParamKind::Bool)
        return std::make_unique<AudioParameterBool> (pid, d.name, d.def > 0.5f);
    if (d.kind == ParamKind::Choice)
    {
        StringArray choices;
        for (const auto& c : d.choices) choices.add (c);
        return std::make_unique<AudioParameterChoice> (pid, d.name, choices, (int) d.def);
    }
    NormalisableRange<float> range (d.lo, d.hi, 0.0f);
    if (d.skew == 0.0f && d.lo > 0.0f)
        range.setSkewForCentre (std::sqrt (d.lo * d.hi));   // log-ish
    else if (d.skew != 1.0f)
        range.skew = d.skew;
    const std::string unit = d.unit;
    const int decimals = d.decimals;
    auto text = [unit, decimals] (float v, int)
    {
        if (unit == "dB")  return ids::decibels (v, decimals);
        if (unit == "Hz")  return ids::hertz (v);
        if (unit == "ms")  return ids::millis (v);
        if (unit == ":1")  return ids::ratio (v);
        if (unit == "%")   return String (roundToInt (v)) + "%";
        if (unit == "LUFS") return String (v, 1) + " LUFS";
        if (unit == "dB/oct") return (v > 0 ? "+" : "") + String (v, 1) + " dB/oct";
        return String (v, decimals);
    };
    return std::make_unique<AudioParameterFloat> (pid, d.name, range, d.def,
        AudioParameterFloatAttributes().withStringFromValueFunction (text));
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const Chain& chain)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return ids::decibels (v); }));
    };
    auto toggle = [] (const char* id, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    };
    layout.add (db (ids::inGainL, "Input Gain L", -24.0f, 24.0f, 0.0f));
    layout.add (db (ids::inGainR, "Input Gain R", -24.0f, 24.0f, 0.0f));
    layout.add (toggle (ids::inLink, "Input Link", true));
    layout.add (db (ids::outGainL, "Output Gain L", -24.0f, 24.0f, 0.0f));
    layout.add (db (ids::outGainR, "Output Gain R", -24.0f, 24.0f, 0.0f));
    layout.add (toggle (ids::outLink, "Output Link", true));
    layout.add (toggle (ids::bypass, "Bypass", false));
    layout.add (toggle (ids::gainMatch, "Gain Match", false));
    layout.add (toggle (ids::mono, "Sum To Mono", false));
    layout.add (toggle (ids::swap, "Swap Channels", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::meterType, 1 }, "Meter Type", StringArray { "Peak", "RMS", "Loudness" }, 0));
    layout.add (toggle (ids::meterMs, "Meter Mid/Side", false));
    layout.add (toggle (ids::ditherOn, "Dither On", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::ditherBits, 1 }, "Dither Bit Depth", StringArray { "24", "20", "16", "12", "8" }, 2));
    layout.add (toggle (ids::ditherBlank, "Dither Auto-Blank", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::ditherAmount, 1 }, "Dither Amount", StringArray { "Off", "Low", "Medium", "Strong" }, 2));
    layout.add (toggle (ids::ditherHarmonic, "Dither Harmonic Suppression", false));
    layout.add (toggle (ids::ditherLimit, "Dither Limit Peaks", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::ditherShaping, 1 }, "Dither Noise Shaping", StringArray { "Off", "Light", "Moderate", "Heavy", "Max" }, 2));
    layout.add (toggle (ids::dcFilter, "DC Filter", false));

    for (int m = 0; m < NumModules; ++m)
        for (const auto& d : chain.module (m).params())
            layout.add (makeParameter (d));
    return layout;
}
} // namespace burnish
