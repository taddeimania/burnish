// ModulePanels.h — one panel per module. All are 1104 x 696 in the panel's
// design space and are created once by the main panel, which shows the
// one for the selected module.
#pragma once

#include "Views.h"

namespace burnish::ui
{
std::unique_ptr<ModulePanel> makeModulePanel (int moduleId, BurnishProcessor&, SpectrumSource& spectrum, SpectrumSource& inputSpectrum);

// Shared layout constants.
inline constexpr int kPanelW = 1104, kPanelH = 696;
inline const juce::Rectangle<int> kViewArea { 14, 56, 1076, 372 };
inline const juce::Rectangle<int> kControlArea { 14, 440, 1076, 232 };

// --- Equalizer 1 / 2 --------------------------------------------------------
class EqPanel final : public ModulePanel
{
public:
    EqPanel (BurnishProcessor&, int moduleId, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    void updateHud();
    void setChannelSet (int s);
    int set() const { return viewSet; }
    Equalizer& eq;
    EqCurveView curve;
    ChoiceDisplay modeChoice, shapeChoice, slopeChoice;
    Fader amountFader { false };
    PanelButton linkBtn { "LINK" }, hudBtn { "HUD" }, allBtn { "ALL BANDS" };
    PanelButton setBtn[2] { PanelButton ("A"), PanelButton ("B") }, setOnBtn[2] { PanelButton ("ON"), PanelButton ("ON") }, setSoloBtn[2] { PanelButton ("S"), PanelButton ("S") };
    PanelButton bandBtn[Equalizer::kBands];
    PanelButton bandOnBtn { "ON" }, bandSoloBtn { "SOLO" }, bandRemoveBtn { "X" };
    Readout freqRo, gainRo, qRo;
    // All bands view.
    PanelButton allOn[Equalizer::kBands];
    ChoiceDisplay allShape[Equalizer::kBands];
    Readout allFreq[Equalizer::kBands], allGain[Equalizer::kBands], allQ[Equalizer::kBands];
    int viewSet = 0, selected = 0;
    bool allView = false;
    float lastLink[2][Equalizer::kBands][6] {};
    bool linkPrimed = false;
};

// --- Dynamic EQ -------------------------------------------------------------
class DynamicEqPanel final : public ModulePanel
{
public:
    DynamicEqPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    void updateHud();
    DynamicEq& deq;
    EqCurveView curve;
    ChoiceDisplay shapeChoice, directionChoice;
    PanelButton setBtn[2] { PanelButton ("A"), PanelButton ("B") }, setOnBtn[2] { PanelButton ("ON"), PanelButton ("ON") };
    PanelButton bandBtn[DynamicEq::kBands];
    PanelButton bandOnBtn { "ON" }, bandSoloBtn { "SOLO" }, bandRemoveBtn { "X" }, autoScaleBtn { "AUTO SCALE" };
    Readout freqRo, gainRo, qRo, attackRo, releaseRo, offsetRo;
    Fader threshFader;
    ThresholdMeter threshMeter;
    int viewSet = 0, selected = 0;
};

// --- Dynamics ---------------------------------------------------------------
class DynamicsPanel final : public ModulePanel
{
public:
    DynamicsPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    void bindBand();
    Dynamics& dyn;
    CrossoverView xover;
    TraceView trace;
    SpectrumView detView;
    PanelButton viewBtn[3] { PanelButton ("CROSSOVER"), PanelButton ("GAIN TRACE"), PanelButton ("DETECTION") };
    PanelButton linkBtn { "LINK BANDS" }, autoGainBtn { "AUTO GAIN" }, adaptiveBtn { "ADAPTIVE REL" }, learnBtn { "LEARN" };
    PanelButton setBtn[2] { PanelButton ("A"), PanelButton ("B") };
    PanelButton selBtn { "SELECTED" }, allBtn { "ALL BANDS" }, prevBtn { "<" }, nextBtn { ">" };
    ChoiceDisplay detectChoice, detFilterChoice, detSlopeChoice;
    Readout detFreqRo, detTiltRo;
    PanelButton detSoloBtn { "S" };
    // Selected band view.
    Fader compThreshFader, limThreshFader;
    ThresholdMeter threshMeter;
    Readout compRatioRo, limRatioRo, attackRo, releaseRo, kneeRo, parallelRo;
    Fader bandGainRo, globalGainRo;
    DynamicsCurveView curveView;
    // All bands view.
    Fader allComp[4], allLim[4];
    ThresholdMeter allMeter[4];
    Readout allRatio[4], allAttack[4], allRelease[4], allGain[4];
    int view = 0, viewSet = 0, selected = 0;
    bool allView = false;
};

// --- Exciter ----------------------------------------------------------------
class ExciterPanel final : public ModulePanel
{
public:
    ExciterPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindBands();
    Exciter& exc;
    CrossoverView xover;
    SpectrumView postView;
    PanelButton viewBtn[2] { PanelButton ("CROSSOVER"), PanelButton ("POST FILTER") };
    PanelButton linkBtn { "LINK BANDS" }, learnBtn { "LEARN" }, setBtn[2] { PanelButton ("A"), PanelButton ("B") };
    ChoiceDisplay osChoice, modeChoice[4];
    Fader amountFader[4], mixFader[4];
    Readout postFreqRo, postGainRo;
    int view = 0, viewSet = 0;
};

// --- Imager -----------------------------------------------------------------
class ImagerPanel final : public ModulePanel
{
public:
    ImagerPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    Imager& img;
    CrossoverView xover;
    TraceView corrTrace;
    SpectrumView widthView;
    Vectorscope scope;
    PanelButton viewBtn[3] { PanelButton ("CROSSOVER"), PanelButton ("CORRELATION"), PanelButton ("WIDTH") };
    PanelButton scopeBtn[3] { PanelButton ("POLAR SAMPLE"), PanelButton ("POLAR LEVEL"), PanelButton ("LISSAJOUS") };
    PanelButton linkBtn { "LINK BANDS" }, learnBtn { "LEARN" }, stereoizeBtn { "STEREOIZE" }, recoverBtn { "RECOVER SIDES" }, recoverSoloBtn { "S" };
    Fader widthFader[4], amountFader { false }, stereoizeFader { false }, recoverFader { false };
    ChoiceDisplay stereoizeMode;
    int view = 0;
};

// --- Impact -----------------------------------------------------------------
class ImpactPanel final : public ModulePanel
{
public:
    ImpactPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindBands();
    Impact& imp;
    CrossoverView xover;
    TraceView trace;
    PanelButton stereoLinkBtn { "STEREO LINK" }, linkBtn { "LINK BANDS" }, autoGainBtn { "AUTO GAIN" }, syncBtn { "SYNC" }, learnBtn { "LEARN" }, setBtn[2] { PanelButton ("A"), PanelButton ("B") };
    Fader impactFader[4], amountFader { false };
    Readout envelopeRo;
    ChoiceDisplay envSyncChoice;
    int viewSet = 0;
};

// --- Low End Focus ----------------------------------------------------------
class LowEndFocusPanel final : public ModulePanel
{
public:
    LowEndFocusPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    LowEndFocus& lef;
    DifferenceView view;
    float curve[TonalTargets::kNumBands] {};
    PanelButton modeBtn[2] { PanelButton ("PUNCHY"), PanelButton ("SMOOTH") }, soloBtn { "SOLO REGION" }, setBtn[2] { PanelButton ("A"), PanelButton ("B") };
    Knob contrastKnob, gainKnob;
    int viewSet = 0;
};

// --- Clarity / Stabilizer / Spectral Shaper ---------------------------------
class ClarityPanel final : public ModulePanel
{
public:
    ClarityPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    Clarity& clr;
    DifferenceView view;
    Knob amountKnob, tiltKnob, attackKnob, releaseKnob;
};

class StabilizerPanel final : public ModulePanel
{
public:
    StabilizerPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    Stabilizer& stb;
    DifferenceView view;
    ChoiceDisplay targetChoice;
    PanelButton modeBtn[2] { PanelButton ("SHAPE"), PanelButton ("CUT") }, tameBtn { "TAME TRANSIENTS" };
    Knob amountKnob, speedKnob, smoothingKnob, sensitivityKnob, lowKnob, midKnob, highKnob;
};

class ShaperPanel final : public ModulePanel
{
public:
    ShaperPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    SpectralShaper& shp;
    DifferenceView view;
    TraceView trace;
    PanelButton viewBtn[2] { PanelButton ("DIFFERENCE"), PanelButton ("GAIN TRACE") }, modeBtn[3] { PanelButton ("LOW"), PanelButton ("MEDIUM"), PanelButton ("HIGH") }, learnBtn { "LEARN" };
    Knob amountKnob, toneKnob, attackKnob, releaseKnob;
    int viewIdx = 0;
};

// --- Match EQ ---------------------------------------------------------------
class MatchEqPanel final : public ModulePanel
{
public:
    MatchEqPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    class MatchView final : public SpectrumView
    {
    public:
        explicit MatchView (MatchEqPanel& p) : panel (p) { showGainScale = true; }
        void paintOverlay (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
    private:
        MatchEqPanel& panel;
        int dragging = -1;
    };
    MatchEq& mtc;
    MatchView view;
    PanelButton refCapture { "CAPTURE" }, refStop { "STOP" }, refClear { "CLEAR" };
    PanelButton applyCapture { "CAPTURE" }, applyStop { "STOP" }, applyClear { "CLEAR" };
    Knob smoothingKnob, amountKnob;
    Readout lowRo, highRo;
};

// --- Maximizer --------------------------------------------------------------
class MaximizerPanel final : public ModulePanel
{
public:
    MaximizerPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    Maximizer& mx;
    SpectrumView specView;
    TraceView trace;
    PanelButton viewBtn[3] { PanelButton ("SPECTRUM"), PanelButton ("GAIN TRACE"), PanelButton ("GR METER") };
    PanelButton learnBtn { "LEARN" }, linkBtn { "LINK" }, truePeakBtn { "TRUE PEAK" }, clipBtn { "SOFT CLIP" }, transBtn { "TRANSIENT EMPH" }, stereoLinkBtn { "LINK" };
    ChoiceDisplay modeChoice, styleChoice, clipModeChoice;
    Readout targetRo;
    Fader gainFader, outputFader;
    Knob characterKnob, upwardKnob, clipKnob, transKnob, stereoTransKnob, stereoSustainKnob;
    Meter grMeterL, grMeterR;
    int view = 1;
};

// --- Vintage Compressor -----------------------------------------------------
class VintageCompressorPanel final : public ModulePanel
{
public:
    VintageCompressorPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindSet();
    VintageCompressor& vc;
    SpectrumView detView;
    TraceView trace;
    PanelButton viewBtn[2] { PanelButton ("DETECTION"), PanelButton ("GAIN TRACE") }, setBtn[2] { PanelButton ("A"), PanelButton ("B") }, autoGainBtn { "AUTO GAIN" }, detSoloBtn { "S" };
    ChoiceDisplay modeChoice, detTypeChoice;
    Readout detFreqRo, detGainRo;
    Fader threshFader;
    ThresholdMeter threshMeter;
    Knob ratioKnob, attackKnob, releaseKnob, gainKnob;
    int view = 0, viewSet = 0;
};

// --- Vintage EQ -------------------------------------------------------------
class VintageEqPanel final : public ModulePanel
{
public:
    VintageEqPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    class CurveView final : public SpectrumView
    {
    public:
        explicit CurveView (VintageEqPanel& p) : panel (p) { showGainScale = true; gainTop = 18.0f; gainBottom = -18.0f; }
        void paintOverlay (juce::Graphics&) override;
    private:
        VintageEqPanel& panel;
    };
    void bindSet();
    VintageEq& veq;
    CurveView view;
    PanelButton setBtn[2] { PanelButton ("A"), PanelButton ("B") }, setOnBtn[2] { PanelButton ("ON"), PanelButton ("ON") };
    ChoiceDisplay lowFreq, hiBoostFreq, hiCutFreq, lmFreq, midFreq, hmFreq;
    Knob lowBoost, lowCut, hiBoost, hiQ, hiCut, lmBoost, midCut, hmBoost;
    int viewSet = 0;
};

// --- Vintage Limiter --------------------------------------------------------
class VintageLimiterPanel final : public ModulePanel
{
public:
    VintageLimiterPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    VintageLimiter& vl;
    SpectrumView specView;
    TraceView trace;
    PanelButton viewBtn[2] { PanelButton ("SPECTRUM"), PanelButton ("GAIN TRACE") }, linkBtn { "LINK" }, truePeakBtn { "TRUE PEAK" };
    ChoiceDisplay modeChoice;
    Fader threshFader, ceilingFader;
    ThresholdMeter threshMeter;
    Knob characterKnob;
    int view = 0;
};

// --- Vintage Tape -----------------------------------------------------------
class VintageTapePanel final : public ModulePanel
{
public:
    VintageTapePanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    class CurveView final : public SpectrumView
    {
    public:
        explicit CurveView (VintageTapePanel& p) : panel (p) { showGainScale = true; gainTop = 12.0f; gainBottom = -12.0f; }
        void paintOverlay (juce::Graphics&) override;
    private:
        VintageTapePanel& panel;
    };
    VintageTape& tape;
    CurveView view;
    PanelButton speedBtn[3] { PanelButton ("7.5 IPS"), PanelButton ("15 IPS"), PanelButton ("30 IPS") };
    Knob driveKnob, biasKnob, harmonicsKnob, lowEmphKnob, highEmphKnob;
};

// --- Bass Control -----------------------------------------------------------
class BassControlPanel final : public ModulePanel
{
public:
    BassControlPanel (BurnishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    BassControl& bc;
    ScopeView scope;
    SweetSpotMeter balanceMeter, punchMeter;
    PanelButton soloBtn { "BASS SOLO" }, balanceOnBtn { "ON" }, punchOnBtn { "ON" }, sustainOnBtn { "ON" }, peakOnBtn { "ON" };
    Readout cutoffRo, zoomRo;
    Knob balanceKnob, punchKnob, sustainThreshKnob, sustainCharKnob, sustainAmountKnob, peakThreshKnob, peakCharKnob;
};
} // namespace burnish::ui
