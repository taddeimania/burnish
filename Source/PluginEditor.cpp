#include "PluginEditor.h"

using namespace burnish;
using namespace burnish::ui;
using juce::Rectangle;
using juce::String;
using Rect = juce::Rectangle<int>;

namespace
{
// Section boxes (design space).
const Rectangle<float> kChainBox  { 12.0f, 92.0f, 1104.0f, 84.0f };
const Rectangle<float> kModuleBox { 12.0f, 192.0f, 1104.0f, 696.0f };
const Rectangle<float> kIoBox     { 1130.0f, 92.0f, 258.0f, 796.0f };

constexpr float kTileW = 112.0f, kTileH = 60.0f, kTileGap = 8.0f;
} // namespace

// ===========================================================================
// ChainStrip
// ===========================================================================
ChainStrip::ChainStrip (BurnishProcessor& p) : proc (p)
{
    setTooltip ("The signal chain, left to right. Click a tile to show its module; drag to reorder; the small buttons are power, solo and remove; + adds a module.");
}

Rectangle<float> ChainStrip::tileRect (int index) const
{
    const float x = 8.0f + (float) index * (kTileW + kTileGap);
    return { x, 6.0f, kTileW, kTileH };
}

Rectangle<float> ChainStrip::addRect() const
{
    return { 8.0f + (float) order.size() * (kTileW + kTileGap), 6.0f, 40.0f, kTileH };
}

int ChainStrip::tileAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) order.size(); ++i)
        if (tileRect (i).contains (p)) return i;
    return -1;
}

void ChainStrip::refresh()
{
    const auto o = proc.getOrder();
    if (o != order) { order = o; repaint(); }
    repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) order.size(); ++i)
    {
        const int id = order[(size_t) i];
        auto r = tileRect (i);
        if (dragging == i) r = r.withX (dragX - kTileW * 0.5f);
        const Module& m = proc.chain.module (id);
        const juce::Colour c (m.info().colour);
        const bool on = proc.getParam (proc.paramId (m, 0)) > 0.5f;
        const bool sel = id == selected;
        const bool soloed = proc.soloModule.load() == id;
        g.setColour (colours::black);
        g.fillRect (r);
        g.setColour (sel ? colours::ink : colours::inkDim);
        g.drawRect (r, sel ? 2.0f : 1.0f);
        drawLed (g, { r.getX() + 12.0f, r.getY() + 12.0f }, 5.0f, c, on);
        drawText (g, Chain::moduleShortName (id), r.withTrimmedLeft (20.0f).withTrimmedRight (4.0f).withHeight (24.0f), 11.0f, on ? c : colours::inkDim, true, juce::Justification::centredLeft);
        // Solo and remove.
        const auto sr = Rectangle<float> (r.getRight() - 40.0f, r.getY() + 4.0f, 16.0f, 16.0f);
        g.setColour (soloed ? colours::ledYellow : colours::socket);
        g.fillRoundedRectangle (sr, 2.0f);
        drawText (g, "S", sr, 9.0f, soloed ? colours::black : colours::ink, true);
        const auto xr = sr.translated (20.0f, 0.0f);
        g.setColour (colours::socket);
        g.fillRoundedRectangle (xr, 2.0f);
        drawText (g, "x", xr, 10.0f, colours::ink, true);
        // Difference meter.
        const auto mr = Rectangle<float> (r.getX() + 8.0f, r.getBottom() - 16.0f, r.getWidth() - 16.0f, 8.0f);
        g.setColour (colours::socket);
        g.fillRect (mr);
        const float d = juce::jlimit (-12.0f, 12.0f, proc.chain.moduleGainDb (id));
        const float zero = mr.getCentreX(), x = mr.getX() + (d + 12.0f) / 24.0f * mr.getWidth();
        g.setColour (on ? c : colours::inkDim);
        g.fillRect (Rectangle<float> (juce::jmin (zero, x), mr.getY() + 1.0f, std::abs (x - zero), mr.getHeight() - 2.0f));
        g.setColour (colours::inkDim);
        g.drawVerticalLine ((int) zero, mr.getY(), mr.getBottom());
        drawText (g, (d > 0 ? "+" : "") + String (d, 1), { r.getX() + 8.0f, r.getBottom() - 32.0f, r.getWidth() - 16.0f, 14.0f }, 10.0f, colours::lcd, true, juce::Justification::centredRight);
        // Arrow to the next.
        if (i < (int) order.size() - 1 || true)
        {
            g.setColour (colours::inkDim);
            const float ax = r.getRight() + kTileGap * 0.5f, ay = r.getCentreY();
            g.drawLine (r.getRight() + 1.0f, ay, r.getRight() + kTileGap - 1.0f, ay, 1.0f);
            juce::Path tri;
            tri.addTriangle (ax, ay - 3.0f, ax, ay + 3.0f, ax + 3.0f, ay);
            g.fillPath (tri);
        }
    }
    const auto a = addRect();
    g.setColour (hover == -2 ? colours::cream : colours::black);
    g.fillRect (a);
    g.setColour (colours::inkDim);
    g.drawRect (a, 1.0f);
    drawText (g, "+", a, 22.0f, hover == -2 ? colours::black : colours::ink, true);
    if (order.empty())
        drawText (g, "Empty chain. Press + to add a module, or ASSIST to have one built.", { a.getRight() + 16.0f, a.getY(), 600.0f, a.getHeight() }, 13.0f, colours::inkDim, false, juce::Justification::centredLeft);
}

void ChainStrip::openAddMenu()
{
    juce::PopupMenu m;
    for (int id = 0; id < NumModules; ++id)
    {
        bool present = false;
        for (int o : order) if (o == id) present = true;
        m.addItem (Chain::moduleName (id), ! present, false, [this, id] { proc.addModule (id); if (onSelect) onSelect (id); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addRect().toNearestInt())));
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (addRect().contains (p)) { openAddMenu(); return; }
    const int i = tileAt (p);
    if (i < 0) return;
    const int id = order[(size_t) i];
    const auto r = tileRect (i);
    const auto sr = Rectangle<float> (r.getRight() - 40.0f, r.getY() + 4.0f, 16.0f, 16.0f);
    const auto xr = sr.translated (20.0f, 0.0f);
    const auto led = Rectangle<float> (20.0f, 20.0f).withCentre ({ r.getX() + 12.0f, r.getY() + 12.0f });
    if (led.contains (p))
    {
        const auto pidOn = proc.paramId (proc.chain.module (id), 0);
        proc.setParam (pidOn, proc.getParam (pidOn) > 0.5f ? 0.0f : 1.0f);
        return;
    }
    if (sr.contains (p)) { proc.soloModule.store (proc.soloModule.load() == id ? -1 : id); repaint(); return; }
    if (xr.contains (p))
    {
        proc.removeModule (id);
        if (selected == id)
        {
            const auto o = proc.getOrder();
            if (onSelect) onSelect (o.empty() ? -1 : o.front());
        }
        return;
    }
    dragging = i;
    dragX = p.x;
    dragMoved = false;
    if (onSelect) onSelect (id);
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    if (std::abs (e.getDistanceFromDragStartX()) > 4) dragMoved = true;
    dragX = e.position.x;
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging >= 0 && dragMoved)
    {
        const int id = order[(size_t) dragging];
        int pos = (int) ((e.position.x - 8.0f + kTileGap * 0.5f) / (kTileW + kTileGap));
        pos = juce::jlimit (0, (int) order.size() - 1, pos);
        proc.moveModule (id, pos);
    }
    dragging = -1;
    refresh();
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    hover = addRect().contains (e.position) ? -2 : tileAt (e.position);
    repaint();
}

// ===========================================================================
// IOPanel
// ===========================================================================
IOPanel::IOPanel (BurnishProcessor& p, PanelLookAndFeel&) : proc (p)
{
    setSize (258, 796);
    for (auto* m : { &inL, &inR, &outL, &outR }) { addAndMakeVisible (*m); m->setInterceptsMouseClicks (false, false); }
    inL.setBounds (30, 48, 12, 230); inR.setBounds (46, 48, 12, 230);
    outL.setBounds (200, 48, 12, 230); outR.setBounds (216, 48, 12, 230);
    addKnob (inKnobL, ids::inGainL, "IN L", { 10, 300, 56, 72 }, "Input gain, left.");
    addKnob (inKnobR, ids::inGainR, "IN R", { 66, 300, 56, 72 }, "Input gain, right.");
    addKnob (outKnobL, ids::outGainL, "OUT L", { 136, 300, 56, 72 }, "Output gain, left.");
    addKnob (outKnobR, ids::outGainR, "OUT R", { 192, 300, 56, 72 }, "Output gain, right.");
    for (auto* k : { &inKnobL, &inKnobR, &outKnobL, &outKnobR }) k->bipolar = true;
    addToggle (inLinkBtn, ids::inLink, { 34, 378, 64, 22 }, "Move both input gains together.");
    addToggle (outLinkBtn, ids::outLink, { 160, 378, 64, 22 }, "Move both output gains together.");
    inLinkBtn.setFontHeight (9.0f); outLinkBtn.setFontHeight (9.0f);
    inKnobL.onValueChange = [this] { if (proc.getParam (ids::inLink) > 0.5f && inKnobL.isMouseButtonDown()) proc.setParam (ids::inGainR, (float) inKnobL.getValue()); };
    inKnobR.onValueChange = [this] { if (proc.getParam (ids::inLink) > 0.5f && inKnobR.isMouseButtonDown()) proc.setParam (ids::inGainL, (float) inKnobR.getValue()); };
    outKnobL.onValueChange = [this] { if (proc.getParam (ids::outLink) > 0.5f && outKnobL.isMouseButtonDown()) proc.setParam (ids::outGainR, (float) outKnobL.getValue()); };
    outKnobR.onValueChange = [this] { if (proc.getParam (ids::outLink) > 0.5f && outKnobR.isMouseButtonDown()) proc.setParam (ids::outGainL, (float) outKnobR.getValue()); };

    meterBtn.setFontHeight (9.0f);
    meterBtn.onClick = [this] (const juce::ModifierKeys&) { proc.setParam (ids::meterType, (float) ((proc.geti (ids::meterType) + 1) % 3)); };
    meterBtn.setTooltip ("What the meters show: PEAK, RMS or short-term LOUDNESS.");
    addAndMakeVisible (meterBtn);
    meterBtn.setBounds (96, 20, 66, 20);
    addToggle (msBtn, ids::meterMs, { 96, 44, 66, 20 }, "Meter left/right or mid/side.");
    msBtn.setFontHeight (9.0f);

    addToggle (bypassBtn, ids::bypass, { 14, 420, 230, 40 }, "Hear the input, delayed to line up, instead of the processed signal.");
    bypassBtn.setFontHeight (13.0f);
    addToggle (matchBtn, ids::gainMatch, { 14, 468, 230, 34 }, "Turn the output to the loudness of the input, so BYPASS compares fairly.");
    matchBtn.setFontHeight (11.0f);
    addToggle (monoBtn, ids::mono, { 14, 510, 112, 34 }, "Sum the output to mono, to check it survives one speaker.");
    monoBtn.setFontHeight (11.0f);
    addToggle (swapBtn, ids::swap, { 132, 510, 112, 34 }, "Swap left and right.");
    swapBtn.setFontHeight (11.0f);
    addToggle (ditherOnBtn, ids::ditherOn, { 14, 560, 34, 34 }, "Dither on or off.");
    ditherBtn.setFontHeight (12.0f);
    ditherBtn.onClick = [this] (const juce::ModifierKeys&) { if (onDither) onDither(); };
    ditherBtn.setTooltip ("Open the dither panel: word length, noise shaping and the bit meter.");
    addAndMakeVisible (ditherBtn);
    ditherBtn.setBounds (54, 560, 190, 34);
}

void IOPanel::addKnob (Knob& k, const char* id, const String& caption, Rect r, const String& tip)
{
    k.caption = caption;
    k.small = true;
    k.setTooltip (tip);
    if (auto* param = proc.param (id))
        k.format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    addAndMakeVisible (k);
    k.setBounds (r);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
}

void IOPanel::addToggle (PanelButton& b, const char* id, Rect r, const String& tip)
{
    b.setTooltip (tip);
    const String pidStr (id);
    b.onClick = [this, pidStr] (const juce::ModifierKeys&) { proc.setParam (pidStr, proc.getParam (pidStr) > 0.5f ? 0.0f : 1.0f); };
    addAndMakeVisible (b);
    b.setBounds (r);
    toggles.push_back ({ &b, pidStr });
}

void IOPanel::refresh()
{
    for (auto& [b, id] : toggles) b->setLit (proc.getParam (id) > 0.5f);
    inRead = proc.chain.takeInputMeters();
    outRead = proc.chain.takeOutputMeters();
    const int type = proc.geti (ids::meterType);
    meterBtn.setLabel (type == 0 ? "PEAK" : type == 1 ? "RMS" : "LUFS");
    msBtn.setLabel (proc.getParam (ids::meterMs) > 0.5f ? "M/S" : "L/R");
    const bool ms = proc.getParam (ids::meterMs) > 0.5f;
    auto conv = [ms] (const MeterReadings& r, int c)
    {
        if (! ms) return r.peak[c];
        // Approximate M/S from the two peaks: mid ~ mean, side ~ half difference.
        return c == 0 ? 0.5f * (r.peak[0] + r.peak[1]) : 0.5f * std::abs (r.peak[0] - r.peak[1]);
    };
    auto value = [&] (const MeterReadings& r, int c)
    {
        if (type == 0) return conv (r, c);
        if (type == 1) return ms ? conv (r, c) : r.rms[c];
        return dbToGain (r.lufsShort + 0.0f);   // short-term loudness on a dBFS-like scale
    };
    inL.push (value (inRead, 0)); inR.push (value (inRead, 1));
    outL.push (value (outRead, 0)); outR.push (value (outRead, 1));
    const auto t = String (inRead.lufsShort, 1) + String (outRead.lufsShort, 1) + String (outRead.truePeakDb, 1) + String ((int) inRead.clipped[0]) + String ((int) outRead.clipped[0]);
    if (t != shownText) { shownText = t; repaint(); }
}

void IOPanel::mouseDown (const juce::MouseEvent& e)
{
    // Click the readouts to clear the clip indicators and the integrated reading.
    if (e.position.y < 290.0f) proc.resetClips();
    if (e.position.y > 600.0f) proc.resetMeters();
}

void IOPanel::paint (juce::Graphics& g)
{
    // Labels and readouts over the meters.
    drawText (g, "IN", { 24.0f, 20.0f, 40.0f, 14.0f }, 11.0f, colours::ink, true);
    drawText (g, "OUT", { 194.0f, 20.0f, 40.0f, 14.0f }, 11.0f, colours::ink, true);
    auto readout = [&] (float x, const MeterReadings& r)
    {
        const bool clip = r.clipped[0] || r.clipped[1];
        const String txt = String (gainToDb (juce::jmax (r.peak[0], r.peak[1], 1e-5f)), 1);
        const auto box = Rectangle<float> (x, 282.0f, 60.0f, 16.0f);
        g.setColour (clip ? colours::ledRed : colours::black);
        g.fillRect (box);
        g.setColour (colours::inkDim);
        g.drawRect (box, 1.0f);
        drawText (g, clip ? "CLIP" : txt, box, 10.0f, clip ? colours::black : colours::lcd, true);
    };
    readout (6.0f, inRead);
    readout (190.0f, outRead);
    // Scale between the meters.
    for (int db = 0; db >= -60; db -= 12)
    {
        const float y = 48.0f + (float) (-db) / 60.0f * 230.0f;
        drawText (g, String (db), { 64.0f, y - 6.0f, 36.0f, 12.0f }, 9.0f, colours::inkDim, false, juce::Justification::centredLeft);
        drawText (g, String (db), { 160.0f, y - 6.0f, 36.0f, 12.0f }, 9.0f, colours::inkDim, false, juce::Justification::centredRight);
    }
    // Loudness block.
    const auto lcd = Rectangle<float> (14.0f, 612.0f, 230.0f, 150.0f);
    drawLcdBox (g, lcd);
    drawText (g, "LOUDNESS", { 22.0f, 618.0f, 100.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredLeft);
    drawText (g, String (outRead.lufsIntegrated, 1), { 22.0f, 632.0f, 120.0f, 34.0f }, 30.0f, colours::lcd, true, juce::Justification::centredLeft);
    drawText (g, "LUFS INTEGRATED", { 22.0f, 666.0f, 150.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredLeft);
    drawText (g, "SHORT " + String (outRead.lufsShort, 1), { 22.0f, 690.0f, 110.0f, 14.0f }, 11.0f, colours::lcd, true, juce::Justification::centredLeft);
    drawText (g, "MOM " + String (outRead.lufsMomentary, 1), { 132.0f, 690.0f, 110.0f, 14.0f }, 11.0f, colours::lcd, true, juce::Justification::centredLeft);
    const bool over = outRead.truePeakDb > -0.05f;
    drawText (g, "TRUE PEAK " + String (outRead.truePeakDb, 1) + " dBTP", { 22.0f, 712.0f, 220.0f, 14.0f }, 11.0f, over ? colours::ledRed : colours::lcd, true, juce::Justification::centredLeft);
    drawText (g, "IN " + String (inRead.lufsShort, 1) + " LUFS   MATCH " + ids::decibels (proc.chain.gainMatchDb()), { 22.0f, 736.0f, 220.0f, 14.0f }, 10.0f, colours::inkDim, true, juce::Justification::centredLeft);
    drawText (g, "click to reset", { 22.0f, 752.0f, 220.0f, 10.0f }, 8.0f, colours::inkFaint, false, juce::Justification::centredLeft);
    drawText (g, "LATENCY " + String (proc.chain.latency() * 1000.0 / juce::jmax (1.0, proc.getSampleRate()), 1) + " ms", { 14.0f, 772.0f, 230.0f, 14.0f }, 10.0f, colours::inkDim, true);
}

// ===========================================================================
// DitherPanel
// ===========================================================================
DitherPanel::DitherPanel (BurnishProcessor& p) : proc (p)
{
    setSize (kPanelW, kPanelH);
    addToggle (onBtn, ids::ditherOn, { 14, 14, 130, 36 }, "Apply dither to the output. Put Burnish last, after any output gain, and switch off the DAW's own dither.");
    addChoice (bitsChoice, ids::ditherBits, "BIT DEPTH", { 14, 70, 150, 56 }, "The word length of the file you will export: 16 for CD, 24 for most else.");
    addChoice (amountChoice, ids::ditherAmount, "DITHER AMOUNT", { 180, 70, 150, 56 }, "How much noise: STRONG removes all quantisation distortion at a slightly higher floor; MEDIUM is the usual choice.");
    addChoice (shapingChoice, ids::ditherShaping, "NOISE SHAPING", { 346, 70, 150, 56 }, "Pushes the dither noise up where hearing is least sensitive: OFF to MAX (about 14 dB less audible noise).");
    addToggle (blankBtn, ids::ditherBlank, { 14, 150, 150, 34 }, "Mute the dither noise after 0.7 s of digital silence.");
    addToggle (harmonicBtn, ids::ditherHarmonic, { 180, 150, 220, 34 }, "With dither OFF: alter the truncation so its distortion falls away from the overtones. Adds no noise.");
    addToggle (limitBtn, ids::ditherLimit, { 416, 150, 150, 34 }, "Keep spikes of shaped noise from going over full scale.");
    addToggle (dcBtn, ids::dcFilter, { 14, 200, 150, 34 }, "A 1 Hz high-pass before the first limiter, to remove DC offset.");
    for (auto* b : { &onBtn, &blankBtn, &harmonicBtn, &limitBtn, &dcBtn }) b->setFontHeight (10.0f);
    resetBtn.setFontHeight (10.0f);
    resetBtn.onClick = [this] (const juce::ModifierKeys&) { proc.chain.dither.resetBitMeter(); proc.chain.dither.resetDc(); };
    resetBtn.setTooltip ("Clear the bit meter's hold and the DC reading.");
    addAndMakeVisible (resetBtn);
    resetBtn.setBounds (14, 560, 80, 30);
    closeBtn.setFontHeight (11.0f);
    closeBtn.onClick = [this] (const juce::ModifierKeys&) { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
    closeBtn.setBounds (kPanelW - 100, 14, 86, 36);
    addAndMakeVisible (bitMeter);
    bitMeter.setBounds (14, 300, 560, 120);
    bitMeter.activity[0] = proc.chain.dither.bitActivity[0]; bitMeter.activity[1] = proc.chain.dither.bitActivity[1];
    bitMeter.hold[0] = proc.chain.dither.bitHold[0]; bitMeter.hold[1] = proc.chain.dither.bitHold[1];
}

void DitherPanel::addToggle (PanelButton& b, const char* id, Rect r, const String& tip)
{
    b.setTooltip (tip);
    const String pidStr (id);
    b.onClick = [this, pidStr] (const juce::ModifierKeys&) { proc.setParam (pidStr, proc.getParam (pidStr) > 0.5f ? 0.0f : 1.0f); };
    addAndMakeVisible (b);
    b.setBounds (r);
    toggles.push_back ({ &b, pidStr });
}

void DitherPanel::addChoice (ChoiceDisplay& c, const char* id, const String& caption, Rect r, const String& tip)
{
    c.caption = caption;
    c.setTooltip (tip);
    const String pidStr (id);
    c.getText = [this, pidStr] { if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.param (pidStr))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    c.onOpen = [this, pidStr, &c]
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.param (pidStr));
        if (param == nullptr) return;
        juce::PopupMenu m;
        for (int i = 0; i < param->choices.size(); ++i)
            m.addItem (param->choices[i], true, i == param->getIndex(), [this, pidStr, i] { proc.setParam (pidStr, (float) i); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
    };
    addAndMakeVisible (c);
    c.setBounds (r);
    choices.push_back ({ &c, pidStr });
}

void DitherPanel::refresh()
{
    for (auto& [b, id] : toggles) b->setLit (proc.getParam (id) > 0.5f);
    for (auto& [c, id] : choices) c->refresh();
    const int bitsIdx = proc.geti (ids::ditherBits);
    bitMeter.bitDepth = bitsIdx == 0 ? 24 : bitsIdx == 1 ? 20 : bitsIdx == 2 ? 16 : bitsIdx == 3 ? 12 : 8;
    harmonicBtn.setEnabled (proc.geti (ids::ditherAmount) == 0);
    bitMeter.repaint();
    repaint();
}

void DitherPanel::paint (juce::Graphics& g)
{
    drawText (g, "BIT METER: which bits of the output are toggling. Top row is the hold, below it live. Red bits lie beyond the chosen word length.",
              { 14.0f, 270.0f, 800.0f, 14.0f }, 11.0f, colours::inkDim, false, juce::Justification::centredLeft);
    drawText (g, "MSB", { 14.0f, 424.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredLeft);
    drawText (g, "LSB", { 534.0f, 424.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredRight);
    drawText (g, "L", { 2.0f, 310.0f, 12.0f, 40.0f }, 10.0f, colours::ink, true);
    drawText (g, "R", { 2.0f, 366.0f, 12.0f, 40.0f }, 10.0f, colours::ink, true);
    // Noise shaping curve.
    const auto nr = Rectangle<float> (600.0f, 300.0f, 480.0f, 120.0f);
    drawLcdBox (g, nr);
    drawText (g, "NOISE SHAPING CURVE", { nr.getX() + 6.0f, nr.getY() + 4.0f, 200.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredLeft);
    const int sh = proc.geti (ids::ditherShaping);
    juce::Path p;
    for (int i = 0; i <= 100; ++i)
    {
        const float t = (float) i / 100.0f;                           // 0 .. Nyquist
        const float w = t * 3.14159f;
        // Error-feedback shaping magnitude |1 - sum h z^-k| sketched: a rising curve.
        float db = 0.0f;
        if (sh == 1) db = 20.0f * std::log10 (std::max (0.05f, 2.0f * std::abs (std::sin (w * 0.5f))));
        else if (sh == 2) db = 20.0f * std::log10 (std::max (0.05f, 3.0f * std::pow (std::abs (std::sin (w * 0.5f)), 1.5f)));
        else if (sh == 3) db = 20.0f * std::log10 (std::max (0.03f, 5.0f * std::pow (std::abs (std::sin (w * 0.5f)), 2.5f)));
        else if (sh == 4) db = 20.0f * std::log10 (std::max (0.02f, 8.0f * std::pow (std::abs (std::sin (w * 0.5f)), 4.0f)));
        const float y = nr.getBottom() - 10.0f - juce::jlimit (0.0f, 1.0f, (db + 30.0f) / 60.0f) * (nr.getHeight() - 24.0f);
        const float x = nr.getX() + 6.0f + t * (nr.getWidth() - 12.0f);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colours::lcd);
    g.strokePath (p, juce::PathStrokeType (1.5f));
    // DC.
    const auto& d = proc.chain.dither;
    drawText (g, "DC OFFSET  L " + String (d.dcOffset[0] * 100.0f, 3) + "%   R " + String (d.dcOffset[1] * 100.0f, 3) + "%",
              { 180.0f, 206.0f, 500.0f, 22.0f }, 13.0f, colours::lcd, true, juce::Justification::centredLeft);
    g.setColour (colours::inkDim);
    g.setFont (panelFont (12.0f, false));
    g.drawFittedText ("Dither should be the very last thing before the export: put Burnish in the last slot, after any output gain, and switch off dithering in the DAW's export dialog so it is not applied twice. "
                      "The DC filter and its meter only act when a Maximizer or Vintage Limiter is in the chain; the filter runs just before whichever comes first.",
                      Rect (14, 450, 1060, 80), juce::Justification::topLeft, 5, 1.0f);
}

// ===========================================================================
// BurnishPanel
// ===========================================================================
BurnishPanel::BurnishPanel (BurnishProcessor& p)
    : proc (p), chain (p), io (p, lnf), assistant (p, outputSpectrum), dither (p)
{
    setLookAndFeel (&lnf);
    setSize (designW, designH);
    setWantsKeyboardFocus (true);

    // --- header -------------------------------------------------------------------
    presetChoice.getText = [this]
    {
        const int n = proc.presetNumber();
        return (n > 0 ? String (n).paddedLeft ('0', 2) + "   " : String()) + proc.presetName().toUpperCase() + (proc.presetEdited() ? "  *" : "");
    };
    presetChoice.fontH = 16.0f;
    presetChoice.onOpen = [this] { openPresetMenu(); };
    presetChoice.setTooltip ("Presets: a chain and the settings of every module in it. A * means you've changed it since loading.");
    addAndMakeVisible (presetChoice);
    presetChoice.setBounds (560, 18, 290, 40);
    addButton (prevBtn, { 516, 18, 40, 40 }, "Previous preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (-1); });
    addButton (nextBtn, { 854, 18, 40, 40 }, "Next preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (1); });
    prevBtn.setFontHeight (16.0f); nextBtn.setFontHeight (16.0f);
    addButton (saveBtn, { 904, 22, 60, 32 }, "Save these settings as a preset of your own.", [this] (const juce::ModifierKeys&) { savePresetDialog(); });
    saveBtn.setFontHeight (10.0f);
    addButton (undoBtn, { 972, 22, 60, 32 }, "Undo the last change. The A B C D buttons hold snapshots: click to recall, shift-click to store.", [this] (const juce::ModifierKeys&) { proc.undo(); });
    undoBtn.setFontHeight (10.0f);
    for (int i = 0; i < 4; ++i)
    {
        addButton (snapBtn[i], { 1040 + i * 32, 22, 30, 32 }, "Snapshot " + String ((char) ('A' + i)) + ": click to recall, shift-click to store the current settings here.",
                   [this, i] (const juce::ModifierKeys& m) { if (m.isShiftDown() || ! proc.hasSnapshot (i)) proc.setSnapshot (i); else proc.recallSnapshot (i); });
        snapBtn[i].setFontHeight (11.0f);
    }
    addButton (assistBtn, { 1180, 18, 100, 40 }, "ASSIST: listen to the song and build a chain toward a target.", [this] (const juce::ModifierKeys&) { showAssistant (! assistantShown); });
    assistBtn.setFontHeight (14.0f);
    addButton (optionsBtn, { 1290, 22, 50, 32 }, "Analyser options.", [this] (const juce::ModifierKeys&) { openOptionsMenu(); });
    optionsBtn.setFontHeight (8.0f);
    addButton (tipsBtn, { 1346, 22, 42, 32 }, "Show or hide these tips (remembered for every Burnish).", [this] (const juce::ModifierKeys&)
    {
        settings.setTooltipsEnabled (! settings.tooltipsEnabled());
        syncTooltips();
    });
    tipsBtn.setFontHeight (9.0f);

    // --- chain, modules, I/O ------------------------------------------------------------
    addAndMakeVisible (chain);
    chain.setBounds (kChainBox.toNearestInt().reduced (4, 6));
    chain.onSelect = [this] (int id) { selectModule (id); };
    for (int id = 0; id < NumModules; ++id)
    {
        panels[(size_t) id] = makeModulePanel (id, proc, outputSpectrum, inputSpectrum);
        if (panels[(size_t) id] != nullptr)
        {
            addChildComponent (*panels[(size_t) id]);
            panels[(size_t) id]->setBounds (kModuleBox.toNearestInt());
        }
    }
    addAndMakeVisible (io);
    io.setBounds (kIoBox.toNearestInt());
    io.onDither = [this] { showDither (! ditherShown); };
    addChildComponent (assistant);
    assistant.setBounds (Rect (12, 96, 1104, 804));
    assistant.onClose = [this] { showAssistant (false); };
    addChildComponent (dither);
    dither.setBounds (kModuleBox.toNearestInt());
    dither.onClose = [this] { showDither (false); };

    outputSpectrum.setSampleRate (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    inputSpectrum.setSampleRate (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    const float avg[] { 0.0f, 1.0f, 3.0f, 10.0f };
    outputSpectrum.setAverageSeconds (avg[juce::jlimit (0, 3, settings.spectrumAverage())]);
    outputSpectrum.setPeakHold (settings.spectrumPeakHold());
    outputSpectrum.setTilt (settings.spectrumTilt());

    const auto order = proc.getOrder();
    selectModule (order.empty() ? -1 : order.front());
    if (proc.assistant.analysing()) showAssistant (true);
    startTimerHz (30);
    timerCallback();
}

BurnishPanel::~BurnishPanel()
{
    stopTimer();
    // Detach every parameter binding while every slider still exists.
    for (auto& p : panels)
        if (p != nullptr)
            p->releaseAttachments();
    setLookAndFeel (nullptr);
}

void BurnishPanel::addButton (PanelButton& b, Rect r, const String& tip, std::function<void (const juce::ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    b.setBounds (r);
}

void BurnishPanel::selectModule (int id)
{
    selectedModule = id;
    chain.selected = id;
    for (int i = 0; i < NumModules; ++i)
        if (panels[(size_t) i] != nullptr)
            panels[(size_t) i]->setVisible (i == id && ! assistantShown && ! ditherShown);
    proc.chain.tapModule.store (-1);   // module views show the chain output
    repaint();
}

void BurnishPanel::showAssistant (bool show)
{
    assistantShown = show;
    if (show) { ditherShown = false; dither.setVisible (false); assistant.showResult(); }
    assistant.setVisible (show);
    chain.setVisible (! show);
    assistBtn.setLit (show);
    selectModule (selectedModule);
}

void BurnishPanel::showDither (bool show)
{
    ditherShown = show;
    if (show) { assistantShown = false; assistant.setVisible (false); chain.setVisible (true); assistBtn.setLit (false); }
    dither.setVisible (show);
    selectModule (selectedModule);
}

bool BurnishPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo(); return true; }
    return false;
}

void BurnishPanel::openPresetMenu()
{
    juce::PopupMenu m;
    const int cur = proc.presetNumber();
    m.addSectionHeader ("FACTORY");
    const auto& f = factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
        m.addItem (String (i + 1).paddedLeft ('0', 2) + "   " + f[(size_t) i].name, true, cur == i + 1, [this, i] { proc.loadFactory (i); });
    const auto users = BurnishProcessor::userPresets();
    m.addSectionHeader ("YOURS");
    if (users.isEmpty())
        m.addItem ("(none yet: press SAVE)", false, false, [] {});
    for (int i = 0; i < users.size(); ++i)
    {
        const auto file = users[i];
        const int n = (int) f.size() + i + 1;
        m.addItem (String (n).paddedLeft ('0', 2) + "   " + file.getFileNameWithoutExtension(), true, cur == n, [this, file] { proc.loadUserPreset (file); });
    }
    m.addSeparator();
    m.addItem ("Save as...", [this] { savePresetDialog(); });
    m.addItem ("Show presets folder", []
    {
        const auto folder = BurnishProcessor::userPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetChoice));
}

void BurnishPanel::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name it. Saving under an existing name replaces it.", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presetName());
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            String error;
            if (! proc.saveUserPreset (saveWindow->getTextEditorContents ("name"), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", error);
        }
        saveWindow.reset();
    }), false);
}

void BurnishPanel::openOptionsMenu()
{
    juce::PopupMenu m;
    juce::PopupMenu avg;
    const char* names[] { "Real time", "1 second", "3 seconds", "10 seconds" };
    for (int i = 0; i < 4; ++i)
        avg.addItem (names[i], true, settings.spectrumAverage() == i, [this, i]
        {
            settings.setSpectrumAverage (i);
            const float a[] { 0.0f, 1.0f, 3.0f, 10.0f };
            outputSpectrum.setAverageSeconds (a[i]);
        });
    m.addSubMenu ("Spectrum averaging", avg);
    m.addItem ("Show peak hold", true, settings.spectrumPeakHold(), [this] { settings.setSpectrumPeakHold (! settings.spectrumPeakHold()); outputSpectrum.setPeakHold (settings.spectrumPeakHold()); });
    juce::PopupMenu tilt;
    for (float t : { 0.0f, 3.0f, 4.5f })
        tilt.addItem (String (t, 1) + " dB/oct" + (t == 3.0f ? " (pink reads flat)" : ""), true, std::abs (settings.spectrumTilt() - t) < 0.1f, [this, t] { settings.setSpectrumTilt (t); outputSpectrum.setTilt (t); });
    m.addSubMenu ("Spectrum tilt", tilt);
    m.addItem ("Reset peak hold", [this] { outputSpectrum.resetPeaks(); });
    m.addSeparator();
    m.addItem ("Show settings folder", [] { Settings::file().getParentDirectory().createDirectory(); Settings::file().getParentDirectory().revealToUser(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsBtn));
}

void BurnishPanel::syncTooltips()
{
    const bool on = settings.tooltipsEnabled();
    tipsBtn.setLit (on);
    if ((int) on != tooltipsApplied && onTooltips)
    {
        tooltipsApplied = (int) on;
        onTooltips (on);
    }
}

void BurnishPanel::timerCallback()
{
    if (proc.getSampleRate() > 0) { outputSpectrum.setSampleRate (proc.getSampleRate()); inputSpectrum.setSampleRate (proc.getSampleRate()); }
    outputSpectrum.update (proc.chain.moduleTap);
    inputSpectrum.update (proc.chain.inputTap);
    presetChoice.refresh();
    undoBtn.setDimmed (! proc.canUndo());
    for (int i = 0; i < 4; ++i)
    {
        snapBtn[i].setLit (proc.activeSnapshot() == i);
        snapBtn[i].setDimmed (! proc.hasSnapshot (i));
    }
    chain.refresh();
    io.refresh();
    if (assistantShown) assistant.refresh();
    else if (ditherShown) dither.refresh();
    else if (selectedModule >= 0 && panels[(size_t) selectedModule] != nullptr)
        panels[(size_t) selectedModule]->refresh();
    if (proc.assistant.analysing() && ! assistantShown)
        showAssistant (true);
}

void BurnishPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    drawText (g, "BURNISH", { 24.0f, 14.0f, 220.0f, 48.0f }, 38.0f, colours::brand, true, juce::Justification::centredLeft);
    drawText (g, "mastering suite", { 240.0f, 22.0f, 160.0f, 34.0f }, 16.0f, colours::ink, true, juce::Justification::centredLeft);

    if (! assistantShown)
    {
        drawSection (g, kChainBox, "SIGNAL CHAIN");
        if (ditherShown)
            drawSection (g, kModuleBox, "DITHER");
        else if (selectedModule >= 0)
            drawSection (g, kModuleBox, Chain::moduleName (selectedModule), juce::Colour (proc.chain.module (selectedModule).info().colour));
        else
        {
            drawSection (g, kModuleBox, "MODULE");
            g.setColour (colours::inkDim);
            g.setFont (panelFont (14.0f, false));
            g.drawFittedText ("No module selected. Add one with + in the signal chain, pick a preset, or press ASSIST to have a chain built for the song.",
                              kModuleBox.toNearestInt().reduced (40), juce::Justification::centred, 3, 1.0f);
        }
    }
    else
        drawSection (g, Rectangle<float> (12.0f, 92.0f, 1104.0f, 796.0f), "ASSIST");
    drawSection (g, kIoBox, "I/O");
}

// ===========================================================================
BurnishEditor::BurnishEditor (BurnishProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onTooltips = [this] (bool on)
    {
        if (on && tooltips == nullptr) tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
        else if (! on) tooltips.reset();
    };
    panel.syncTooltips();
    setResizable (true, true);
    const double ratio = (double) BurnishPanel::designW / BurnishPanel::designH;
    setResizeLimits (840, (int) (840 / ratio), 2800, (int) (2800 / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);
    setSize (1260, (int) (1260 / ratio));
}

void BurnishEditor::paint (juce::Graphics& g) { g.fillAll (colours::panel); }

void BurnishEditor::resized()
{
    const float s = (float) getWidth() / (float) BurnishPanel::designW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, BurnishPanel::designW, BurnishPanel::designH);
}
