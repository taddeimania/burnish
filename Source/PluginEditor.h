#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Settings.h"
#include "UI/Analyzer.h"
#include "UI/AssistantView.h"
#include "UI/ModulePanels.h"
#include "UI/Widgets.h"

// ---------------------------------------------------------------------------
// The signal chain strip: one tile per module in use, drag to reorder, + to
// add, each with power, solo, a difference meter and remove.
// ---------------------------------------------------------------------------
class ChainStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChainStrip (BurnishProcessor&);
    std::function<void (int)> onSelect;
    int selected = -1;
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    juce::Rectangle<float> tileRect (int index) const;
    juce::Rectangle<float> addRect() const;
    int tileAt (juce::Point<float>) const;
    void openAddMenu();
    BurnishProcessor& proc;
    std::vector<int> order;
    int dragging = -1, hover = -1;
    float dragX = 0.0f;
    bool dragMoved = false;
};

// ---------------------------------------------------------------------------
// The I/O panel: meters, gains, listening switches, dither.
// ---------------------------------------------------------------------------
class IOPanel final : public juce::Component
{
public:
    IOPanel (BurnishProcessor&, burnish::ui::PanelLookAndFeel&);
    std::function<void()> onDither;
    std::function<void()> onAssist;
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using Knob = burnish::ui::Knob;
    using PanelButton = burnish::ui::PanelButton;
    using Meter = burnish::ui::Meter;
    void addKnob (Knob&, const char* id, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addToggle (PanelButton&, const char* id, juce::Rectangle<int>, const juce::String& tip);
    BurnishProcessor& proc;
    Meter inL, inR, outL, outR;
    Knob inKnobL, inKnobR, outKnobL, outKnobR;
    PanelButton inLinkBtn { "LINK" }, outLinkBtn { "LINK" }, bypassBtn { "BYPASS" }, matchBtn { "GAIN MATCH" }, monoBtn { "MONO" }, swapBtn { "SWAP" },
                ditherBtn { "DITHER" }, ditherOnBtn { "" }, meterBtn { "PEAK" }, msBtn { "L/R" };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::pair<PanelButton*, juce::String>> toggles;
    burnish::MeterReadings inRead, outRead;
    juce::String shownText;
};

// ---------------------------------------------------------------------------
// The dither panel, shown over the module area.
// ---------------------------------------------------------------------------
class DitherPanel final : public juce::Component
{
public:
    DitherPanel (BurnishProcessor&);
    std::function<void()> onClose;
    void refresh();
    void paint (juce::Graphics&) override;

private:
    using PanelButton = burnish::ui::PanelButton;
    using ChoiceDisplay = burnish::ui::ChoiceDisplay;
    void addToggle (PanelButton&, const char* id, juce::Rectangle<int>, const juce::String& tip);
    void addChoice (ChoiceDisplay&, const char* id, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    BurnishProcessor& proc;
    PanelButton onBtn { "DITHER ON" }, blankBtn { "AUTO-BLANK" }, harmonicBtn { "HARMONIC SUPPRESSION" }, limitBtn { "LIMIT PEAKS" }, dcBtn { "DC FILTER" }, resetBtn { "RESET" }, closeBtn { "CLOSE" };
    ChoiceDisplay bitsChoice, amountChoice, shapingChoice;
    burnish::ui::BitMeterView bitMeter;
    std::vector<std::pair<PanelButton*, juce::String>> toggles;
    std::vector<std::pair<ChoiceDisplay*, juce::String>> choices;
};

// ---------------------------------------------------------------------------
// The whole panel, in a fixed design space scaled to the window.
// ---------------------------------------------------------------------------
class BurnishPanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1400;
    static constexpr int designH = 900;

    explicit BurnishPanel (BurnishProcessor&);
    ~BurnishPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override {}
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (bool)> onTooltips;
    void syncTooltips();

private:
    using PanelButton = burnish::ui::PanelButton;
    using ChoiceDisplay = burnish::ui::ChoiceDisplay;
    void timerCallback() override;
    void addButton (PanelButton&, juce::Rectangle<int>, const juce::String& tip, std::function<void (const juce::ModifierKeys&)>);
    void selectModule (int id);
    void showAssistant (bool);
    void showDither (bool);
    void openPresetMenu();
    void savePresetDialog();
    void openOptionsMenu();

    BurnishProcessor& proc;
    burnish::ui::PanelLookAndFeel lnf;
    burnish::Settings settings;
    burnish::ui::SpectrumSource outputSpectrum, inputSpectrum;

    ChoiceDisplay presetChoice;
    PanelButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, undoBtn { "UNDO" }, assistBtn { "ASSIST" }, optionsBtn { "OPTIONS" }, tipsBtn { "TIPS" };
    PanelButton snapBtn[4] { PanelButton ("A"), PanelButton ("B"), PanelButton ("C"), PanelButton ("D") };

    ChainStrip chain;
    IOPanel io;
    std::array<std::unique_ptr<burnish::ui::ModulePanel>, burnish::NumModules> panels;
    burnish::ui::AssistantView assistant;
    DitherPanel dither;
    int selectedModule = -1;
    bool assistantShown = false, ditherShown = false;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    int tooltipsApplied = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BurnishPanel)
};

class BurnishEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BurnishEditor (BurnishProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BurnishPanel panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BurnishEditor)
};
