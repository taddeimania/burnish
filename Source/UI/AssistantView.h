// AssistantView.h — the ASSIST pages: set up a run (target, intensity,
// loudness, which modules, how long to listen), watch it listen, then
// steer the result (tonal balance, loudness, dynamics, width, clarity,
// stability) with a handful of controls that drive the modules it built.
#pragma once

#include "../PluginProcessor.h"
#include "Analyzer.h"
#include "Widgets.h"

namespace burnish::ui
{
class AssistantView final : public juce::Component
{
public:
    AssistantView (BurnishProcessor&, SpectrumSource& outputSpectrum);
    ~AssistantView() override;

    enum Page { Setup = 0, Listening, Result };
    Page page() const { return currentPage; }
    void showSetup();
    void showResult();
    std::function<void()> onClose;   // back to the module view

    void refresh();   // 30 Hz while visible
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TargetList final : public juce::Component
    {
    public:
        explicit TargetList (AssistantView& v) : view (v) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent& e) override { hoverRow = rowAt (e.position.y); repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
        int filter = 0;   // 0 all, 1 genres, 2 custom, 3 favourites
        std::vector<int> visibleRows() const;
    private:
        int rowAt (float y) const;
        AssistantView& view;
        int hoverRow = -1;
        float scroll = 0.0f;
    };

    void addReference();
    void startRun();
    void saveDefaults();
    void loadDefaults();
    void setPage (Page);
    void setToggle (PanelButton&, juce::Rectangle<int>, const juce::String& tip, std::function<void (const juce::ModifierKeys&)>);
    juce::String analysisText() const;

    BurnishProcessor& proc;
    SpectrumSource& spectrum;
    PanelLookAndFeel lnf;
    Page currentPage = Setup;

    // Both pages.
    TargetList targets { *this };
    PanelButton filterBtn[4] { PanelButton ("ALL"), PanelButton ("GENRES"), PanelButton ("CUSTOM"), PanelButton ("FAVS") };
    PanelButton addTargetBtn { "+ REFERENCE" }, removeTargetBtn { "REMOVE" };

    // Setup page.
    PanelButton moduleBtn[7] { PanelButton ("EQUALIZER"), PanelButton ("BASS CONTROL"), PanelButton ("CLARITY"), PanelButton ("STABILIZER"), PanelButton ("IMPACT"), PanelButton ("IMAGER"), PanelButton ("MAXIMIZER") };
    PanelButton enableAllBtn { "ENABLE ALL" }, disableAllBtn { "DISABLE ALL" }, tempoSyncBtn { "TEMPO SYNC" };
    PanelButton intensityBtn[5] { PanelButton ("SUBTLE"), PanelButton ("TRANSPARENT"), PanelButton ("BALANCED"), PanelButton ("BOLD"), PanelButton ("TRANSFORMATIVE") };
    PanelButton destBtn[2] { PanelButton ("FULL SCALE"), PanelButton ("STREAMING") };
    PanelButton truePeakBtn { "TRUE PEAK" }, backBtn { "BACK" }, saveDefaultBtn { "SAVE AS DEFAULT" }, startBtn { "START ASSISTANT" }, oneClickBtn { "ONE-CLICK" };
    Knob analysisKnob, loudnessKnob, outputKnob;

    // Result page.
    PanelButton moduleViewBtn { "MODULE VIEW" }, relearnBtn { "RELEARN" }, gainMatchBtn { "GAIN MATCH" }, bypassBtn { "BYPASS" };
    PanelButton eqOnBtn { "ON" }, maxOnBtn { "ON" }, impactOnBtn { "ON" }, imagerOnBtn { "ON" }, clarityOnBtn { "ON" }, stabilizerOnBtn { "ON" };
    PanelButton resultDestBtn[2] { PanelButton ("FULL SCALE"), PanelButton ("STREAMING") };
    Fader eqAmount { false }, maxOffset { false }, impactAmount { false }, imagerAmount { false }, clarityAmount { false }, stabilizerAmount { false };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<juce::FileChooser> chooser;
    bool saveAsDefault = false;
    float liveCurve[TonalTargets::kNumBands] {};
};
} // namespace burnish::ui
