// Views.h — the panel pieces shared by the module panels: the module
// panel base (parameter binding, the standard header), the crossover
// spectrum, the EQ curve editor, the scrolling gain trace, the spectral
// difference meter, the vectorscope and the dynamics curve.
#pragma once

#include "../PluginProcessor.h"
#include "Analyzer.h"
#include "Widgets.h"

#include <array>
#include <map>

namespace burnish::ui
{
// ---------------------------------------------------------------------------
// ModulePanel: one module's controls. The main panel shows one at a time
// and calls refresh() from its timer.
// ---------------------------------------------------------------------------
class ModulePanel : public juce::Component
{
public:
    ModulePanel (BurnishProcessor&, int moduleId, SpectrumSource& spectrum);
    ~ModulePanel() override;

    // Must be called before a panel is destroyed. The attachments live here in
    // the base class but the sliders they hold are members of the subclass,
    // which C++ destroys first; an attachment outliving its slider crashes
    // the host when it unhooks itself. BurnishPanel calls this on every panel.
    void releaseAttachments();

    int moduleId() const { return id; }
    virtual void refresh();          // 30 Hz while visible
    void paint (juce::Graphics&) override;

    // Binding helpers. `pid` is the full parameter id.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    void bindSlider (juce::Slider&, const juce::String& pid, juce::Rectangle<int>, const juce::String& tip);
    void addKnob (Knob&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip, bool small = true);
    void addFader (Fader&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addReadout (Readout&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addToggle (PanelButton&, const juce::String& pid, juce::Rectangle<int>, const juce::String& tip);
    void addChoice (ChoiceDisplay&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addButton (PanelButton&, juce::Rectangle<int>, const juce::String& tip, std::function<void (const juce::ModifierKeys&)> click);
    void rebind (juce::Slider&, const juce::String& pid);   // swap a slider to another parameter
    juce::String pid (int index) const { return proc.paramId (proc.chain.module (id), index); }
    float pv (int index) const { return proc.getParam (pid (index)); }
    void setp (int index, float v) { proc.setParam (pid (index), v); }

    // Link a group of sliders: while `linkPid` is on, dragging one moves the
    // others by the same amount (or the opposite amount when `inverse`).
    void linkSliders (std::vector<juce::Slider*> group, const juce::String& linkPid, bool inverse = false);

    // Standard header pieces, placed by the subclass.
    void addStandardHeader (bool delta, bool reset, int chModeParam = -1);
    juce::Rectangle<int> headerArea() const { return { 14, 10, getWidth() - 28, 34 }; }

    // Text drawn under the header by the subclass (e.g. what a mode means).
    juce::String statusLine;

protected:
    BurnishProcessor& proc;
    int id;
    SpectrumSource& spectrum;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::vector<std::pair<juce::Slider*, juce::String>> sliders;
    std::vector<std::pair<PanelButton*, juce::String>> toggles;
    std::vector<std::pair<ChoiceDisplay*, juce::String>> choices;
    std::map<juce::Slider*, double> lastValues;
    PanelButton deltaBtn { "DELTA" }, resetBtn { "RESET" }, presetBtn { "PRESETS" };
    ChoiceDisplay chModeChoice;
    void openModulePresets();
    void openChoiceMenu (const juce::String& pid, juce::Component* target);
    void drawSectionLocal (juce::Graphics&, juce::Rectangle<float>, const juce::String& title) const;
};

// ---------------------------------------------------------------------------
// CrossoverView: band regions over the spectrum. Drag the handles, hover
// for + to add a band, x to remove, power and S per band; click a band to
// select it.
// ---------------------------------------------------------------------------
class CrossoverView final : public SpectrumView
{
public:
    CrossoverView (ModulePanel&, MultibandParams& mb, std::atomic<int>& soloBand);
    int selectedBand = 0;
    std::function<void (int)> onSelect;
    // Optional per-band level line (Dynamics): dB per band, or NaN for none.
    std::function<float (int band)> bandLineDb;
    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();

private:
    int numBands() const;
    float xoverHz (int i) const;
    void setXover (int i, float hz);
    int handleAt (juce::Point<float>) const;
    int bandAt (float x) const;
    juce::Rectangle<float> powerRect (int band) const;
    juce::Rectangle<float> soloRect (int band) const;
    juce::Rectangle<float> removeRect (int band) const;
    juce::Rectangle<float> addRect() const;

    ModulePanel& panel;
    MultibandParams& mb;
    std::atomic<int>& solo;
    int dragging = -1;
    juce::Point<float> hover { -1.0f, -1.0f };
    bool hovering = false;
};

// ---------------------------------------------------------------------------
// EqCurveView: the EQ spectrum with draggable nodes. Works for the
// Equalizer (8 bands) and the Dynamic EQ (6 bands) through callbacks.
// ---------------------------------------------------------------------------
class EqCurveView final : public SpectrumView
{
public:
    struct Band
    {
        bool on = false;
        float hz = 1000.0f, gainDb = 0.0f, q = 1.0f;
        bool hasGain = true, hasQ = true;
        juce::Colour colour;
    };
    explicit EqCurveView (ModulePanel&);
    int numBands = 8;
    std::function<Band (int)> getBand;
    std::function<void (int, float hz, float gainDb)> setBandFreqGain;
    std::function<void (int, float q)> setBandQ;
    std::function<void (int)> onSelect;
    std::function<void (int, float hz)> onAdd;      // enable band `i` at hz (i = first free)
    std::function<void (int)> onRemove;
    std::function<void (int)> onReset;             // double-click a node
    std::function<float (float hz)> compositeDb;   // with amount
    std::function<float (float hz)> compositeNoAmountDb;   // without amount, or null
    std::function<float (int, float hz)> bandDb;
    std::function<float (float hz)> dynamicDb;    // Dynamic EQ: the moving curve
    std::function<void (int, bool)> onAltSolo;
    int selected = -1;
    bool drawSelectedFill = true;

    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int nodeAt (juce::Point<float>) const;
    juce::Point<float> nodePos (const Band&) const;
    ModulePanel& panel;
    int dragging = -1, dragQ = 0;   // dragQ: -1 left handle, +1 right handle
    juce::Point<float> dragStart;
    float startQ = 1.0f;
    juce::Point<float> hover { -1.0f, -1.0f };
    bool hovering = false, altSoloing = false;
};

// ---------------------------------------------------------------------------
// TraceView: a scrolling waveform with gain traces drawn over it.
// ---------------------------------------------------------------------------
class TraceView final : public juce::Component
{
public:
    static constexpr int kHistory = 360;   // 12 s at 30 Hz
    struct Lane { juce::Colour colour; bool fromTop = true; std::array<float, kHistory> values {}; };
    float rangeDb = 24.0f;                  // gain trace scale
    std::vector<Lane> lanes;
    int dimAllBut = -1;
    void setTap (const Tap* t) { tap = t; }
    // Push one frame: the waveform peak (linear) and each lane's value (dB, sign as drawn).
    void push (float wavePeak, const float* laneValues);
    void paint (juce::Graphics&) override;
    void reset();

private:
    const Tap* tap = nullptr;
    std::array<float, kHistory> wave {};
    int writePos = 0;
};

// ---------------------------------------------------------------------------
// DifferenceView: gain per sixth-octave band (the spectral modules), with
// optional action-region handles.
// ---------------------------------------------------------------------------
class DifferenceView final : public SpectrumView
{
public:
    explicit DifferenceView (ModulePanel&);
    const float* gainDb = nullptr;       // 61 bands
    const float* specDb = nullptr;       // optional: the module's own smoothed spectrum (relative)
    juce::Colour colour = colours::lcd;
    int lowParam = -1, highParam = -1;   // action region params (Hz), -1 for none
    float regionMinHz = 20.0f, regionMaxHz = 20000.0f;
    std::function<float (float hz)> targetDb;   // optional target curve (Stabilizer)
    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    ModulePanel& panel;
    int dragging = -1;
    float dragOffsetOct = 0.0f;
};

// ---------------------------------------------------------------------------
// Vectorscope: polar sample, polar level or Lissajous, with correlation and
// balance bars.
// ---------------------------------------------------------------------------
class Vectorscope final : public juce::Component
{
public:
    enum Mode { PolarSample = 0, PolarLevel, Lissajous };
    int mode = PolarSample;
    float correlation = 1.0f, balance = 0.0f;
    void setTap (const Tap* t) { tap = t; }
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; image.clear (image.getBounds(), juce::Colours::transparentBlack); repaint(); }

private:
    const Tap* tap = nullptr;
    juce::Image image;
    std::vector<float> l, r;
    bool clipped = false;
    std::array<float, 64> rays {};
};

// ---------------------------------------------------------------------------
// DynamicsCurveView: input vs output with the static curve of a band.
// ---------------------------------------------------------------------------
class DynamicsCurveView final : public juce::Component
{
public:
    std::function<float (float inDb)> curve;
    float inputDb = -90.0f, gainDb = 0.0f;
    juce::Colour colour = colours::band (0);
    float rangeDb = 60.0f;
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        rangeDb = juce::jlimit (24.0f, 90.0f, rangeDb - w.deltaY * 20.0f);
        repaint();
    }
};

// ---------------------------------------------------------------------------
// ThresholdMeter: a fader with the input level outside and the gain change
// inside, as in the dynamics panels.
// ---------------------------------------------------------------------------
class ThresholdMeter final : public juce::Component
{
public:
    float inputDb = -90.0f, gainDb = 0.0f, floorDb = -60.0f;
    juce::Colour colour = colours::band (0);
    void paint (juce::Graphics&) override;
};

// ---------------------------------------------------------------------------
// Oscilloscope for Bass Control: the low band waveform with threshold lines.
// ---------------------------------------------------------------------------
class ScopeView final : public juce::Component
{
public:
    const float* ring = nullptr;
    int ringSize = 0;
    std::atomic<int>* writePos = nullptr;
    float zoomDb = 0.0f;
    float solidLineDb = -1000.0f, dashedLineDb = -1000.0f;   // -1000: hidden
    std::function<void (float dbSolid)> onDragSolid;
    std::function<void (float dbDashed)> onDragDashed;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    float dbForY (float y) const;
    float yForDb (float db) const;
private:
    int dragging = 0;
};

// Sweet-spot meter: a ball on a bar with a target region.
class SweetSpotMeter final : public juce::Component
{
public:
    float value = 0.0f, lo = -1.0f, hi = 1.0f;          // display range
    float targetLo = -0.3f, targetHi = 0.3f;
    juce::String leftLabel, rightLabel;
    void set (float v) { v = juce::jlimit (lo, hi, v); if (std::abs (v - shown) > 0.005f) { shown += 0.25f * (v - shown); value = shown; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float shown = 0.0f;
};

// Bit meter for the dither panel.
class BitMeterView final : public juce::Component
{
public:
    const float* activity[2] { nullptr, nullptr };
    const float* hold[2] { nullptr, nullptr };
    int bitDepth = 16;
    void paint (juce::Graphics&) override;
};
} // namespace burnish::ui
