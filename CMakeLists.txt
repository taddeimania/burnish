cmake_minimum_required(VERSION 3.22)

# macOS 11+. Builds for the machine you're on by default; for a universal
# binary pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64". Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Burnish VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------------
# By default JUCE is downloaded on first configure. To use a local checkout:
#   cmake -B build -DBURNISH_JUCE_DIR=/path/to/JUCE
set(BURNISH_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")

if(BURNISH_JUCE_DIR)
    add_subdirectory(${BURNISH_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Signal path (no JUCE) -----------------------------------------------------
# Every module, the chain, metering, dither and the assistant's analysis.
# The tests build against this alone.
add_library(BurnishDSP STATIC
    Source/Engine/Module.cpp
    Source/Engine/Loudness.cpp
    Source/Engine/Chain.cpp
    Source/Engine/Dither.cpp
    Source/Engine/Analysis.cpp
    Source/Engine/Presets.cpp
    Source/Engine/Modules/Equalizer.cpp
    Source/Engine/Modules/DynamicEq.cpp
    Source/Engine/Modules/Dynamics.cpp
    Source/Engine/Modules/Exciter.cpp
    Source/Engine/Modules/Imager.cpp
    Source/Engine/Modules/Impact.cpp
    Source/Engine/Modules/LowEndFocus.cpp
    Source/Engine/Modules/Spectral.cpp
    Source/Engine/Modules/MatchEq.cpp
    Source/Engine/Modules/Maximizer.cpp
    Source/Engine/Modules/VintageCompressor.cpp
    Source/Engine/Modules/VintageEq.cpp
    Source/Engine/Modules/VintageLimiter.cpp
    Source/Engine/Modules/VintageTape.cpp
    Source/Engine/Modules/BassControl.cpp)
target_include_directories(BurnishDSP PUBLIC Source)
set_target_properties(BurnishDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(BurnishDSP PRIVATE /utf-8)
endif()

# --- Plugin -------------------------------------------------------------------
# Copy the built plugin into the system VST3 folder after each build:
#   macOS   ~/Library/Audio/Plug-Ins/VST3
#   Linux   ~/.vst3
#   Windows C:\Program Files\Common Files\VST3  (needs an administrator shell,
#           so it is off by default there)
if(WIN32)
    set(_burnish_copy_default OFF)
else()
    set(_burnish_copy_default ON)
endif()
option(BURNISH_COPY_PLUGIN "Install the plugin into the system VST3 folder after building" ${_burnish_copy_default})

juce_add_plugin(Burnish
    COMPANY_NAME                "Taddei"
    BUNDLE_ID                   com.taddei.burnish
    PLUGIN_MANUFACTURER_CODE    Tadd
    PLUGIN_CODE                 Brns
    PRODUCT_NAME                "Burnish"
    DESCRIPTION                 "Mastering suite"
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Mastering Dynamics EQ
    COPY_PLUGIN_AFTER_BUILD     ${BURNISH_COPY_PLUGIN})

set(BURNISH_PLUGIN_SOURCES
    Source/Settings.cpp
    Source/Assistant.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp
    Source/UI/Analyzer.cpp
    Source/UI/ModulePanels.cpp
    Source/UI/ModulePanels2.cpp
    Source/UI/AssistantView.cpp)

target_sources(Burnish PRIVATE ${BURNISH_PLUGIN_SOURCES})
target_include_directories(Burnish PRIVATE Source)
if(MSVC)
    target_compile_options(Burnish PRIVATE /utf-8)
endif()
target_compile_definitions(Burnish PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Burnish
    PRIVATE
        BurnishDSP
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Engine tests (no JUCE needed) --------------------------------------------
enable_testing()
add_executable(BurnishEngineTests Tests/EngineTests.cpp)
target_link_libraries(BurnishEngineTests PRIVATE BurnishDSP)
add_test(NAME engine COMMAND BurnishEngineTests)

# --- Optional dev tool --------------------------------------------------------
#   BurnishSnapshot out.png [module]   render the panel headless, showing a module
option(BURNISH_BUILD_TOOLS "Build the snapshot tool" OFF)
if(BURNISH_BUILD_TOOLS)
    juce_add_console_app(BurnishSnapshot PRODUCT_NAME "BurnishSnapshot")
    target_sources(BurnishSnapshot PRIVATE Tools/Snapshot.cpp ${BURNISH_PLUGIN_SOURCES})
    target_include_directories(BurnishSnapshot PRIVATE Source)
    if(MSVC)
        target_compile_options(BurnishSnapshot PRIVATE /utf-8)
    endif()
    target_compile_definitions(BurnishSnapshot PRIVATE
        JucePlugin_Name="Burnish"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(BurnishSnapshot PRIVATE
        BurnishDSP
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
