#include "ModulePanels.h"

namespace burnish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

namespace
{
const char* kShapeBlurb[NumFilterShapes] {
    "BELL: a smooth boost or cut around the frequency. Q sets how wide.",
    "PROPORTIONAL Q: the more you boost or cut, the narrower it gets. Tight corrective cuts.",
    "BAND SHELF: a bell with a flat top, for a block of frequencies.",
    "LOW SHELF: everything below the frequency up or down.",
    "LOW SHELF BAXANDALL: a gentle first-order shelf, the most transparent way to move the lows.",
    "LOW SHELF VINTAGE: a shelf with a complementary dip above it, as the passive program equalisers do.",
    "LOW SHELF RESONANT: a shelf with a bump at the corner; Q sets the resonance.",
    "HIGH SHELF: everything above the frequency up or down.",
    "HIGH SHELF BAXANDALL: a gentle first-order shelf for air without harshness.",
    "HIGH SHELF VINTAGE: a shelf with a complementary dip below it.",
    "HIGH SHELF RESONANT: a shelf with a bump at the corner; Q sets the resonance.",
    "HIGH PASS: removes everything below the frequency at the chosen slope.",
    "HIGH PASS RESONANT: a high pass with a boost at the corner; Q sets how much.",
    "HIGH PASS BRICKWALL: 96 dB per octave. Nothing gets through.",
    "LOW PASS: removes everything above the frequency at the chosen slope.",
    "LOW PASS RESONANT: a low pass with a boost at the corner; Q sets how much.",
    "LOW PASS BRICKWALL: 96 dB per octave." };

String bandTip (int b) { return "Band " + String (b + 1) + ": select it to edit. Alt-click a node to hear just that region."; }
} // namespace

// ===========================================================================
// EqPanel
// ===========================================================================
EqPanel::EqPanel (BurnishProcessor& p, int moduleId, SpectrumSource& s)
    : ModulePanel (p, moduleId, s), eq (p.chain.eq (moduleId == ModEq1 ? 1 : 2)), curve (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, eq.pChMode);

    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (setBtn[i], h.removeFromLeft (30), i == 0 ? "Show the A controls (mid or left)." : "Show the B controls (side or right).",
                   [this, i] (const ModifierKeys&) { setChannelSet (i); });
        addToggle (setOnBtn[i], pid (eq.pChanOn[i]), h.removeFromLeft (30), "Switch this channel's EQ on or off.");
        setOnBtn[i].setFontHeight (10.0f);
        addButton (setSoloBtn[i], h.removeFromLeft (24), "Hear this channel alone.", [this, i] (const ModifierKeys&)
        {
            eq.soloChannel.store (eq.soloChannel.load() == i ? -1 : i);
        });
        setSoloBtn[i].setFontHeight (10.0f);
        h.removeFromLeft (8);
    }
    addChoice (modeChoice, pid (eq.pMode), {}, h.removeFromLeft (100), "ANALOG: minimum-phase filters, no latency. DIGITAL: linear-phase, the same curve with no phase shift, one frame of latency.");
    modeChoice.fontH = 13.0f;
    h.removeFromLeft (8);
    addFader (amountFader, pid (eq.pAmount), {}, h.removeFromLeft (150), "AMOUNT scales the gain of every band: 0% is flat, 200% doubles every move.");
    amountFader.fill = colours::lcd;
    h.removeFromLeft (8);
    addToggle (linkBtn, pid (eq.pLink), h.removeFromLeft (50), "Link the A and B channels so a move on one is made on both.");
    linkBtn.setFontHeight (10.0f);
    h.removeFromLeft (14);
    addButton (hudBtn, h.removeFromLeft (50), "Edit one band at a time with the HUD under the curve.", [this] (const ModifierKeys&) { allView = false; updateHud(); });
    hudBtn.setFontHeight (10.0f);
    addButton (allBtn, h.removeFromLeft (76), "Every band's controls in a table.", [this] (const ModifierKeys&) { allView = true; updateHud(); });
    allBtn.setFontHeight (10.0f);

    // Curve.
    addAndMakeVisible (curve);
    curve.setBounds (kViewArea.withHeight (470));
    curve.source = &spectrum;
    curve.numBands = Equalizer::kBands;
    curve.getBand = [this] (int b)
    {
        EqCurveView::Band bd;
        const auto& bi = eq.band[set()][b];
        bd.on = pv (bi.on) > 0.5f;
        bd.hz = pv (bi.freq);
        const int shape = (int) pv (bi.shape);
        bd.hasGain = shapeHasGain (shape);
        bd.hasQ = shapeHasQ (shape) && ! shapeUsesSlope (shape);
        bd.gainDb = bd.hasGain ? pv (bi.gain) : 0.0f;
        bd.q = pv (bi.q);
        bd.colour = colours::node (b);
        return bd;
    };
    curve.setBandFreqGain = [this] (int b, float hz, float g) { setp (eq.band[set()][b].freq, hz); if (shapeHasGain ((int) pv (eq.band[set()][b].shape))) setp (eq.band[set()][b].gain, g); };
    curve.setBandQ = [this] (int b, float q) { setp (eq.band[set()][b].q, q); };
    curve.onSelect = [this] (int b) { selectBand (b); };
    curve.onAdd = [this] (int b, float hz)
    {
        const auto& bi = eq.band[set()][b];
        // Default shapes by frequency, as the manual describes.
        int shape = ProportionalQ;
        bool haveBax = false, haveTreble = false;
        for (int i = 0; i < Equalizer::kBands; ++i)
            if (pv (eq.band[set()][i].on) > 0.5f)
            {
                if ((int) pv (eq.band[set()][i].shape) == LowShelfBaxandall) haveBax = true;
                if ((int) pv (eq.band[set()][i].shape) == HighShelfBaxandall) haveTreble = true;
            }
        if (hz < 100.0f && ! haveBax) shape = LowShelfBaxandall;
        else if (hz > 8000.0f && ! haveTreble) shape = HighShelfBaxandall;
        setp (bi.shape, (float) shape);
        setp (bi.freq, hz);
        setp (bi.gain, 0.0f);
        setp (bi.q, 1.0f);
        setp (bi.on, 1.0f);
    };
    curve.onRemove = [this] (int b) { setp (eq.band[set()][b].on, 0.0f); };
    curve.onReset = [this] (int b) { setp (eq.band[set()][b].gain, 0.0f); setp (eq.band[set()][b].q, 1.0f); };
    curve.compositeDb = [this] (float hz) { float o; eq.curveDb (set(), &hz, &o, 1, true); return o; };
    curve.compositeNoAmountDb = [this] (float hz) { if (std::abs (pv (eq.pAmount) - 100.0f) < 0.5f) return 0.0f; float o; eq.curveDb (set(), &hz, &o, 1, false); return o; };
    curve.bandDb = [this] (int b, float hz) { float o; eq.bandCurveDb (set(), b, &hz, &o, 1, true); return o; };
    curve.onAltSolo = [this] (int b, bool on) { eq.soloBand.store (on ? b : -1); eq.soloSet.store (on ? set() : -1); };

    // HUD.
    auto c = kControlArea.withY (540).withHeight (130);
    auto left = c.removeFromLeft (46);
    for (int b = 0; b < Equalizer::kBands; ++b)
    {
        bandBtn[b].setLabel (String (b + 1));
        bandBtn[b].setFontHeight (11.0f);
        bandBtn[b].setLitColour (colours::node (b));
        addButton (bandBtn[b], Rect (left.getX() + (b % 2) * 22, left.getY() + (b / 2) * 30, 20, 26), bandTip (b), [this, b] (const ModifierKeys&) { selectBand (b); });
    }
    c.removeFromLeft (14);
    auto row = c.removeFromTop (44);
    addButton (bandOnBtn, row.removeFromLeft (56), "Switch the selected band on or off.", [this] (const ModifierKeys&) { const auto& bi = eq.band[set()][selected]; setp (bi.on, pv (bi.on) > 0.5f ? 0.0f : 1.0f); });
    bandOnBtn.setFontHeight (11.0f);
    row.removeFromLeft (6);
    addButton (bandSoloBtn, row.removeFromLeft (64), "Hear only the region this band affects.", [this] (const ModifierKeys&)
    {
        const bool on = eq.soloBand.load() != selected;
        eq.soloBand.store (on ? selected : -1); eq.soloSet.store (on ? set() : -1);
    });
    bandSoloBtn.setFontHeight (11.0f);
    row.removeFromLeft (6);
    addButton (bandRemoveBtn, row.removeFromLeft (40), "Remove the selected band.", [this] (const ModifierKeys&) { setp (eq.band[set()][selected].on, 0.0f); });
    row.removeFromLeft (20);
    addChoice (shapeChoice, pid (eq.band[0][0].shape), {}, row.removeFromLeft (190), "The filter shape. The line under the panel says what each does.");
    row.removeFromLeft (16);
    addReadout (freqRo, pid (eq.band[0][0].freq), "FREQ", row.removeFromLeft (110).withHeight (58), "Centre or cutoff frequency. Drag to scrub, double-click to reset.");
    row.removeFromLeft (10);
    addReadout (gainRo, pid (eq.band[0][0].gain), "GAIN", row.removeFromLeft (110).withHeight (58), "Boost or cut, -30 to +15 dB.");
    row.removeFromLeft (10);
    addReadout (qRo, pid (eq.band[0][0].q), "Q", row.removeFromLeft (100).withHeight (58), "Width: higher is narrower. For resonant filters, the size of the bump.");
    row.removeFromLeft (10);
    addChoice (slopeChoice, pid (eq.band[0][0].slope), "SLOPE", row.removeFromLeft (120).withHeight (58), "Slope of a pass filter, dB per octave.");

    // All bands table.
    for (int b = 0; b < Equalizer::kBands; ++b)
    {
        const int x = kControlArea.getX() + 60 + b * 128;
        const int y = 460;
        allOn[b].setLabel (String (b + 1));
        allOn[b].setLitColour (colours::node (b));
        allOn[b].setFontHeight (11.0f);
        addButton (allOn[b], Rect (x, y, 36, 24), "Click to remove the band (a removed band comes back from the HUD view or the + on the curve).",
                   [this, b] (const ModifierKeys&) { const auto& bi = eq.band[set()][b]; setp (bi.on, pv (bi.on) > 0.5f ? 0.0f : 1.0f); });
        addChoice (allShape[b], pid (eq.band[0][b].shape), {}, Rect (x, y + 30, 118, 26), "Filter shape.");
        allShape[b].fontH = 10.0f;
        addReadout (allFreq[b], pid (eq.band[0][b].freq), "FREQ", Rect (x, y + 62, 118, 44), "Frequency.");
        addReadout (allGain[b], pid (eq.band[0][b].gain), "GAIN", Rect (x, y + 112, 118, 44), "Gain.");
        addReadout (allQ[b], pid (eq.band[0][b].q), "Q", Rect (x, y + 162, 118, 44), "Q.");
        allFreq[b].fontH = allGain[b].fontH = allQ[b].fontH = 13.0f;
    }
    selectBand (0);
    updateHud();
}

void EqPanel::setChannelSet (int s)
{
    viewSet = s;
    updateHud();
    curve.repaint();
}

void EqPanel::selectBand (int b)
{
    selected = jlimit (0, Equalizer::kBands - 1, b);
    curve.selected = selected;
    updateHud();
}

void EqPanel::updateHud()
{
    const auto& bi = eq.band[set()][selected];
    rebind (freqRo, pid (bi.freq));
    rebind (gainRo, pid (bi.gain));
    rebind (qRo, pid (bi.q));
    for (auto& c : choices)
    {
        if (c.first == &shapeChoice) c.second = pid (bi.shape);
        if (c.first == &slopeChoice) c.second = pid (bi.slope);
    }
    shapeChoice.onOpen = [this] { openChoiceMenu (pid (eq.band[set()][selected].shape), &shapeChoice); };
    slopeChoice.onOpen = [this] { openChoiceMenu (pid (eq.band[set()][selected].slope), &slopeChoice); };
    shapeChoice.getText = [this] { return String (filterShapeName ((int) pv (eq.band[set()][selected].shape))).toUpperCase(); };
    slopeChoice.getText = [this] { const int s = (int) pv (eq.band[set()][selected].slope); return String (kSlopes[jlimit (0, kNumSlopes - 1, s)], 0) + " DB/OCT"; };
    for (int b = 0; b < Equalizer::kBands; ++b)
    {
        const auto& ab = eq.band[set()][b];
        rebind (allFreq[b], pid (ab.freq));
        rebind (allGain[b], pid (ab.gain));
        rebind (allQ[b], pid (ab.q));
        for (auto& c : choices) if (c.first == &allShape[b]) c.second = pid (ab.shape);
        allShape[b].onOpen = [this, b] { openChoiceMenu (pid (eq.band[set()][b].shape), &allShape[b]); };
        allShape[b].getText = [this, b] { return String (filterShapeShort ((int) pv (eq.band[set()][b].shape))); };
    }
    // Which view.
    const bool hud = ! allView;
    curve.setBounds (kViewArea.withHeight (hud ? 470 : 390));
    showAll (hud, bandOnBtn, bandSoloBtn, bandRemoveBtn, shapeChoice, freqRo, gainRo, qRo, slopeChoice);
    for (int b = 0; b < Equalizer::kBands; ++b)
    {
        bandBtn[b].setVisible (hud);
        showAll (! hud, allOn[b], allShape[b], allFreq[b], allGain[b], allQ[b]);
    }
    hudBtn.setLit (hud);
    allBtn.setLit (! hud);
    repaint();
}

void EqPanel::refresh()
{
    ModulePanel::refresh();
    // LINK: a change on one channel set is copied to the other.
    {
        const bool link = pv (eq.pLink) > 0.5f;
        for (int b = 0; b < Equalizer::kBands; ++b)
        {
            const int idx[2][6] { { eq.band[0][b].on, eq.band[0][b].shape, eq.band[0][b].freq, eq.band[0][b].gain, eq.band[0][b].q, eq.band[0][b].slope },
                                  { eq.band[1][b].on, eq.band[1][b].shape, eq.band[1][b].freq, eq.band[1][b].gain, eq.band[1][b].q, eq.band[1][b].slope } };
            for (int k = 0; k < 6; ++k)
            {
                const float a = pv (idx[0][k]), bv = pv (idx[1][k]);
                if (link && linkPrimed)
                {
                    if (a != lastLink[0][b][k] && bv != a) setp (idx[1][k], a);
                    else if (bv != lastLink[1][b][k] && a != bv) setp (idx[0][k], bv);
                }
                lastLink[0][b][k] = pv (idx[0][k]);
                lastLink[1][b][k] = pv (idx[1][k]);
            }
        }
        linkPrimed = true;
    }
    const int chMode = (int) pv (eq.pChMode);
    const bool twoSets = chMode != Stereo;
    for (int i = 0; i < 2; ++i)
    {
        setBtn[i].setVisible (twoSets);
        setOnBtn[i].setVisible (twoSets);
        setSoloBtn[i].setVisible (twoSets);
        setBtn[i].setLit (twoSets && viewSet == i);
        setSoloBtn[i].setLit (eq.soloChannel.load() == i);
        setBtn[i].setLabel (chMode == MidSide ? (i == 0 ? "MID" : "SIDE") : (i == 0 ? "L" : "R"));
    }
    if (! twoSets && viewSet != 0) setChannelSet (0);
    if (! twoSets) eq.soloChannel.store (-1);
    const auto& bi = eq.band[set()][selected];
    bandOnBtn.setLit (pv (bi.on) > 0.5f);
    bandSoloBtn.setLit (eq.soloBand.load() == selected);
    for (int b = 0; b < Equalizer::kBands; ++b)
    {
        bandBtn[b].setLit (b == selected);
        bandBtn[b].setDimmed (pv (eq.band[set()][b].on) < 0.5f);
        allOn[b].setLit (pv (eq.band[set()][b].on) > 0.5f);
        const bool on = pv (eq.band[set()][b].on) > 0.5f;
        allShape[b].setEnabled (on); allFreq[b].setEnabled (on); allGain[b].setEnabled (on); allQ[b].setEnabled (on);
    }
    const int shape = (int) pv (bi.shape);
    const bool pass = shapeIsPass (shape);
    gainRo.setEnabled (shapeHasGain (shape));
    qRo.setEnabled (shapeHasQ (shape) && ! shapeUsesSlope (shape));
    slopeChoice.setVisible (! allView && shapeUsesSlope (shape));
    const bool digital = pv (eq.pMode) > 0.5f;
    statusLine = String (kShapeBlurb[jlimit (0, NumFilterShapes - 1, shape)]) + (digital ? "   DIGITAL: linear phase, 43 ms of latency." : "");
    ignoreUnused (pass);
    curve.repaint();
}

void EqPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawText (g, "AMOUNT", { 480.0f, 44.0f, 150.0f, 12.0f }, 9.0f, colours::inkDim, true);
    if (! allView)
        drawSectionLocal (g, Rect (kControlArea).withY (532).withTrimmedLeft (60).withHeight (70).toFloat(), "BAND " + String (selected + 1));
}

// ===========================================================================
// DynamicEqPanel
// ===========================================================================
DynamicEqPanel::DynamicEqPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModDynamicEq, s), deq (p.chain.dynamicEq), curve (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, deq.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's bands.", [this, i] (const ModifierKeys&) { viewSet = i; updateHud(); });
        addToggle (setOnBtn[i], pid (deq.pChanOn[i]), h.removeFromLeft (30), "Switch this channel on or off.");
        setOnBtn[i].setFontHeight (10.0f);
        h.removeFromLeft (8);
    }

    addAndMakeVisible (curve);
    curve.setBounds (kViewArea);
    curve.source = &spectrum;
    curve.numBands = DynamicEq::kBands;
    curve.getBand = [this] (int b)
    {
        EqCurveView::Band bd;
        const auto& bi = deq.band[viewSet][b];
        bd.on = pv (bi.on) > 0.5f;
        bd.hz = pv (bi.freq);
        bd.gainDb = pv (bi.gain);
        bd.q = pv (bi.q);
        const int sh = (int) pv (bi.shape);
        bd.hasQ = sh != DynamicEq::ShapeBaxBass && sh != DynamicEq::ShapeBaxTreble;
        bd.colour = colours::node (b);
        return bd;
    };
    curve.setBandFreqGain = [this] (int b, float hz, float g) { setp (deq.band[viewSet][b].freq, hz); setp (deq.band[viewSet][b].gain, g); };
    curve.setBandQ = [this] (int b, float q) { setp (deq.band[viewSet][b].q, q); };
    curve.onSelect = [this] (int b) { selectBand (b); };
    curve.onAdd = [this] (int b, float hz)
    {
        const auto& bi = deq.band[viewSet][b];
        setp (bi.shape, hz < 100.0f ? (float) DynamicEq::ShapeBaxBass : hz > 8000.0f ? (float) DynamicEq::ShapeBaxTreble : (float) DynamicEq::ShapePropQ);
        setp (bi.freq, hz); setp (bi.gain, -3.0f); setp (bi.q, 1.0f); setp (bi.on, 1.0f);
    };
    curve.onRemove = [this] (int b) { setp (deq.band[viewSet][b].on, 0.0f); };
    curve.onReset = [this] (int b) { setp (deq.band[viewSet][b].gain, -3.0f); setp (deq.band[viewSet][b].q, 1.0f); setp (deq.band[viewSet][b].offset, 0.0f); };
    curve.compositeDb = [this] (float hz) { float o; deq.curveDb (viewSet, &hz, &o, 1, true); return o; };
    curve.dynamicDb = [this] (float hz) { float o; deq.curveDb (viewSet, &hz, &o, 1, false); return o; };
    curve.bandDb = [this] (int b, float hz) { float o; deq.bandCurveDb (viewSet, b, &hz, &o, 1, true); return o; };
    curve.onAltSolo = [this] (int b, bool on) { deq.soloBand.store (on ? b : -1); deq.soloSet.store (on ? viewSet : -1); };

    auto c = kControlArea;
    auto left = c.removeFromLeft (46);
    for (int b = 0; b < DynamicEq::kBands; ++b)
    {
        bandBtn[b].setLabel (String (b + 1));
        bandBtn[b].setFontHeight (12.0f);
        bandBtn[b].setLitColour (colours::node (b));
        addButton (bandBtn[b], Rect (left.getX(), left.getY() + b * 32, 40, 28), bandTip (b), [this, b] (const ModifierKeys&) { selectBand (b); });
    }
    c.removeFromLeft (14);
    // Row 1: general + filter.
    auto row = c.removeFromTop (58);
    addButton (bandOnBtn, row.removeFromLeft (50).withHeight (40), "Band on or off.", [this] (const ModifierKeys&) { const auto& bi = deq.band[viewSet][selected]; setp (bi.on, pv (bi.on) > 0.5f ? 0.0f : 1.0f); });
    bandOnBtn.setFontHeight (11.0f);
    row.removeFromLeft (6);
    addButton (bandSoloBtn, row.removeFromLeft (56).withHeight (40), "Hear only this band's region.", [this] (const ModifierKeys&)
    {
        const bool on = deq.soloBand.load() != selected;
        deq.soloBand.store (on ? selected : -1); deq.soloSet.store (on ? viewSet : -1);
    });
    bandSoloBtn.setFontHeight (11.0f);
    row.removeFromLeft (6);
    addButton (bandRemoveBtn, row.removeFromLeft (36).withHeight (40), "Remove the band.", [this] (const ModifierKeys&) { setp (deq.band[viewSet][selected].on, 0.0f); });
    row.removeFromLeft (20);
    addChoice (shapeChoice, pid (deq.band[0][0].shape), "SHAPE", row.removeFromLeft (170), "Baxandall shelves, a band shelf, a bell or a proportional-Q bell.");
    row.removeFromLeft (12);
    addReadout (freqRo, pid (deq.band[0][0].freq), "FREQ", row.removeFromLeft (104), "Frequency.");
    row.removeFromLeft (10);
    addReadout (gainRo, pid (deq.band[0][0].gain), "GAIN", row.removeFromLeft (104), "The gain the band moves to (or from) when triggered.");
    row.removeFromLeft (10);
    addReadout (qRo, pid (deq.band[0][0].q), "Q", row.removeFromLeft (90), "Width.");
    row.removeFromLeft (12);
    addChoice (directionChoice, pid (deq.band[0][0].direction), "DIRECTION", row.removeFromLeft (110), "UP: the filter moves up when the signal crosses the threshold. DOWN: it moves down.");
    // Row 2: threshold and advanced.
    c.removeFromTop (14);
    auto row2 = c.removeFromTop (150);
    addFader (threshFader, pid (deq.band[0][0].threshold), "THRESHOLD", row2.removeFromLeft (60), "Level in the band's region at which the gain starts to move.");
    threshFader.bipolar = false;
    addAndMakeVisible (threshMeter);
    threshMeter.setBounds (row2.removeFromLeft (30).withTrimmedBottom (16).reduced (2, 8));
    threshMeter.floorDb = -60.0f;
    row2.removeFromLeft (30);
    addButton (autoScaleBtn, row2.removeFromLeft (110).withHeight (34), "Scale attack and release with the frequency: slower for low bands, faster for high ones.",
               [this] (const ModifierKeys&) { const auto& bi = deq.band[viewSet][selected]; setp (bi.autoScale, pv (bi.autoScale) > 0.5f ? 0.0f : 1.0f); });
    autoScaleBtn.setFontHeight (10.0f);
    row2.removeFromLeft (12);
    addReadout (attackRo, pid (deq.band[0][0].attack), "ATTACK", row2.removeFromLeft (100).withHeight (50), "How fast the band reacts when the level crosses the threshold.");
    row2.removeFromLeft (10);
    addReadout (releaseRo, pid (deq.band[0][0].release), "RELEASE", row2.removeFromLeft (100).withHeight (50), "How fast it returns when the level drops back.");
    row2.removeFromLeft (10);
    addReadout (offsetRo, pid (deq.band[0][0].offset), "OFFSET", row2.removeFromLeft (100).withHeight (50), "A static gain for the band, applied whatever the level.");
    selectBand (0);
}

void DynamicEqPanel::selectBand (int b)
{
    selected = jlimit (0, DynamicEq::kBands - 1, b);
    curve.selected = selected;
    updateHud();
}

void DynamicEqPanel::updateHud()
{
    const auto& bi = deq.band[viewSet][selected];
    rebind (freqRo, pid (bi.freq)); rebind (gainRo, pid (bi.gain)); rebind (qRo, pid (bi.q));
    rebind (threshFader, pid (bi.threshold)); rebind (attackRo, pid (bi.attack)); rebind (releaseRo, pid (bi.release)); rebind (offsetRo, pid (bi.offset));
    for (auto& c : choices)
    {
        if (c.first == &shapeChoice) c.second = pid (bi.shape);
        if (c.first == &directionChoice) c.second = pid (bi.direction);
    }
    shapeChoice.onOpen = [this] { openChoiceMenu (pid (deq.band[viewSet][selected].shape), &shapeChoice); };
    directionChoice.onOpen = [this] { openChoiceMenu (pid (deq.band[viewSet][selected].direction), &directionChoice); };
    shapeChoice.getText = [this] { if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (pid (deq.band[viewSet][selected].shape)))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    directionChoice.getText = [this] { if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (pid (deq.band[viewSet][selected].direction)))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    repaint();
}

void DynamicEqPanel::refresh()
{
    ModulePanel::refresh();
    const int chMode = (int) pv (deq.pChMode);
    const bool twoSets = chMode != Stereo;
    for (int i = 0; i < 2; ++i)
    {
        setBtn[i].setVisible (twoSets); setOnBtn[i].setVisible (twoSets);
        setBtn[i].setLit (twoSets && viewSet == i);
        setBtn[i].setLabel (chMode == MidSide ? (i == 0 ? "MID" : "SIDE") : (i == 0 ? "L" : "R"));
    }
    if (! twoSets && viewSet != 0) { viewSet = 0; updateHud(); }
    const auto& bi = deq.band[viewSet][selected];
    bandOnBtn.setLit (pv (bi.on) > 0.5f);
    bandSoloBtn.setLit (deq.soloBand.load() == selected);
    autoScaleBtn.setLit (pv (bi.autoScale) > 0.5f);
    for (int b = 0; b < DynamicEq::kBands; ++b)
    {
        bandBtn[b].setLit (b == selected);
        bandBtn[b].setDimmed (pv (deq.band[viewSet][b].on) < 0.5f);
    }
    const int c = chMode == Stereo ? 0 : viewSet;
    threshMeter.inputDb = deq.levelDb[c][selected];
    threshMeter.gainDb = deq.gainNowDb[c][selected] - pv (bi.offset);
    threshMeter.colour = colours::node (selected);
    threshMeter.repaint();
    const bool up = (int) pv (bi.direction) == 0;
    const float g = pv (bi.gain);
    statusLine = up ? (g >= 0 ? "UP with a positive gain: flat until triggered, then boosts toward the gain."
                              : "UP with a negative gain: cut all the time, moving back to flat when triggered.")
                    : (g >= 0 ? "DOWN with a positive gain: boosted all the time, moving back to flat when triggered."
                              : "DOWN with a negative gain: flat until triggered, then cuts toward the gain.");
    statusLine += "   Now: " + ids::decibels (deq.gainNowDb[c][selected]) + ".";
    curve.repaint();
}

void DynamicEqPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (60).toFloat().withTrimmedBottom (4.0f), "BAND " + String (selected + 1));
}

// ===========================================================================
// DynamicsPanel
// ===========================================================================
DynamicsPanel::DynamicsPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModDynamics, s), dyn (p.chain.dynamics), xover (*this, dyn.mb, dyn.soloBand)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, dyn.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 3; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (i == 0 ? 90 : 92), i == 0 ? "The bands over the spectrum, with each band's gain change as a line."
                   : i == 1 ? "A scrolling trace of the selected band's gain reduction." : "The filter in front of the level detector.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    addToggle (linkBtn, pid (dyn.pLink), h.removeFromLeft (84), "Move every band's control together.");
    linkBtn.setFontHeight (10.0f);
    addToggle (autoGainBtn, pid (dyn.pAutoGain), h.removeFromLeft (76), "Make-up gain per band from the RMS difference between input and output.");
    autoGainBtn.setFontHeight (10.0f);
    addToggle (adaptiveBtn, pid (dyn.pAdaptive), h.removeFromLeft (96), "Shorter release on transients, longer on sustained sound.");
    adaptiveBtn.setFontHeight (10.0f);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at dips in the spectrum of what is playing.", [this] (const ModifierKeys&)
    {
        // Crossovers at spectral minima between 60 Hz and 12 kHz.
        const int nb = (int) pv (dyn.mb.pNumBands) + 1;
        if (nb < 2) return;
        std::vector<std::pair<float, float>> minima;   // (hz, db)
        for (int c = 8; c < SpectrumSource::kColumns - 8; ++c)
        {
            const float hz = SpectrumSource::columnHz (c);
            if (hz < 60.0f || hz > 12000.0f) continue;
            const float* cv = spectrum.curve();
            bool isMin = true;
            for (int k = -6; k <= 6; ++k) if (cv[c + k] < cv[c]) isMin = false;
            if (isMin) minima.push_back ({ hz, cv[c] });
        }
        for (int i = 0; i < nb - 1; ++i)
        {
            const float want = pv (dyn.mb.pXover[i]);
            float best = want, bd = 1e9f;
            for (auto& [hz, db] : minima)
            {
                const float d = std::abs (std::log2 (hz / want));
                if (d < bd && d < 1.0f) { bd = d; best = hz; }
            }
            setp (dyn.mb.pXover[i], best);
        }
    });
    learnBtn.setFontHeight (10.0f);
    h.removeFromLeft (10);
    for (int i = 0; i < 2; ++i)
    {
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; bindBand(); });
    }

    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;
    xover.showGainScale = true; xover.gainTop = 12.0f; xover.gainBottom = -24.0f;
    xover.onSelect = [this] (int b) { selectBand (b); };
    xover.bandLineDb = [this] (int b)
    {
        const int c = (int) pv (dyn.pChMode) == Stereo ? 0 : viewSet;
        return dyn.tm[c][b].gainDb + dyn.tm[c][b].autoDb + pv (dyn.band[viewSet][b].gain);
    };
    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.lanes.push_back ({ colours::band (0), true, {} });
    trace.setTap (&proc.chain.inputTap);
    addAndMakeVisible (detView);
    detView.setBounds (kViewArea);
    detView.source = &spectrum;
    detView.showGainScale = true; detView.gainTop = 12.0f; detView.gainBottom = -24.0f;
    addChoice (detFilterChoice, pid (dyn.pDetFilter), "FILTER", Rect (kViewArea.getX() + 14, kViewArea.getY() + 12, 110, 50), "NONE, a HIGHPASS so bass does not drive the compressor, or a TILT that favours the highs.");
    addReadout (detFreqRo, pid (dyn.pDetFreq), "FREQ", Rect (kViewArea.getX() + 134, kViewArea.getY() + 12, 90, 50), "High-pass frequency.");
    addChoice (detSlopeChoice, pid (dyn.pDetSlope), "SLOPE", Rect (kViewArea.getX() + 234, kViewArea.getY() + 12, 100, 50), "High-pass slope.");
    addReadout (detTiltRo, pid (dyn.pDetTilt), "AMOUNT", Rect (kViewArea.getX() + 344, kViewArea.getY() + 12, 90, 50), "Tilt slope, dB per octave.");
    addButton (detSoloBtn, Rect (kViewArea.getX() + 444, kViewArea.getY() + 12, 30, 30), "Hear the filtered detection signal.", [this] (const ModifierKeys&) { dyn.detSolo.store (! dyn.detSolo.load()); });

    // Controls.
    auto c = kControlArea;
    auto top = c.removeFromTop (30);
    addChoice (detectChoice, pid (dyn.pDetect), "DETECT", top.removeFromLeft (170), "PEAK follows the peaks; ENV an averaged level that treats all frequencies alike; RMS the average power.");
    detectChoice.captionLeft = true;
    top.removeFromLeft (20);
    addButton (prevBtn, top.removeFromLeft (28), "Previous band.", [this] (const ModifierKeys&) { selectBand (selected - 1); });
    addButton (selBtn, top.removeFromLeft (96), "The selected band's full controls.", [this] (const ModifierKeys&) { allView = false; bindBand(); });
    selBtn.setFontHeight (10.0f);
    addButton (nextBtn, top.removeFromLeft (28), "Next band.", [this] (const ModifierKeys&) { selectBand (selected + 1); });
    top.removeFromLeft (6);
    addButton (allBtn, top.removeFromLeft (90), "Key controls of every band side by side.", [this] (const ModifierKeys&) { allView = true; bindBand(); });
    allBtn.setFontHeight (10.0f);
    c.removeFromTop (12);

    // Selected band view.
    auto sel = c;
    addFader (limThreshFader, pid (dyn.band[0][0].limThresh), "LIMIT", sel.removeFromLeft (54), "Limiter threshold. Above it the limiter ratio applies.");
    addAndMakeVisible (threshMeter);
    threshMeter.setBounds (sel.removeFromLeft (34).withTrimmedBottom (16).reduced (3, 8));
    addFader (compThreshFader, pid (dyn.band[0][0].compThresh), "COMP", sel.removeFromLeft (54), "Compressor threshold. Above it the compressor ratio applies.");
    sel.removeFromLeft (16);
    auto col = sel.removeFromLeft (120);
    addReadout (compRatioRo, pid (dyn.band[0][0].compRatio), "COMP RATIO", col.removeFromTop (52), "Compressor ratio. Negative values, shown as (4):1, compress upward: quiet parts are raised toward the threshold.");
    col.removeFromTop (8);
    addReadout (limRatioRo, pid (dyn.band[0][0].limRatio), "LIMIT RATIO", col.removeFromTop (52), "Limiter ratio. Negative values expand upward: louder parts are pushed up.");
    col.removeFromTop (8);
    addReadout (kneeRo, pid (dyn.band[0][0].knee), "KNEE", col.removeFromTop (52), "Soft knee width. Higher starts the compression gradually.");
    sel.removeFromLeft (12);
    col = sel.removeFromLeft (120);
    addReadout (attackRo, pid (dyn.band[0][0].attack), "ATTACK", col.removeFromTop (52), "Time to reach full compression.");
    col.removeFromTop (8);
    addReadout (releaseRo, pid (dyn.band[0][0].release), "RELEASE", col.removeFromTop (52), "Time to recover.");
    col.removeFromTop (8);
    addReadout (parallelRo, pid (dyn.band[0][0].parallel), "PARALLEL", col.removeFromTop (52), "Dry signal mixed back in.");
    sel.removeFromLeft (12);
    addAndMakeVisible (curveView);
    curveView.setBounds (sel.removeFromLeft (190).withTrimmedBottom (16));
    curveView.curve = [this] (float in) { return dyn.staticGainDb (viewSet, selected, in); };
    sel.removeFromLeft (16);
    addFader (bandGainRo, pid (dyn.band[0][0].gain), "BAND GAIN", sel.removeFromLeft (60), "Gain after this band's dynamics.");
    bandGainRo.bipolar = true;
    sel.removeFromLeft (10);
    addFader (globalGainRo, pid (dyn.pGlobalGain), "GLOBAL", sel.removeFromLeft (60), "Gain after all bands.");
    globalGainRo.bipolar = true;

    // All bands view.
    for (int b = 0; b < 4; ++b)
    {
        const int x = kControlArea.getX() + b * 262;
        const int y = kControlArea.getY() + 42;
        addFader (allLim[b], pid (dyn.band[0][b].limThresh), "LIMIT", Rect (x, y, 50, 170), "Limiter threshold.");
        addAndMakeVisible (allMeter[b]);
        allMeter[b].setBounds (Rect (x + 52, y + 8, 28, 138));
        allMeter[b].colour = colours::band (b);
        addFader (allComp[b], pid (dyn.band[0][b].compThresh), "COMP", Rect (x + 82, y, 50, 170), "Compressor threshold.");
        addReadout (allRatio[b], pid (dyn.band[0][b].compRatio), "RATIO", Rect (x + 140, y, 100, 44), "Compressor ratio.");
        addReadout (allAttack[b], pid (dyn.band[0][b].attack), "ATTACK", Rect (x + 140, y + 50, 100, 44), "Attack.");
        addReadout (allRelease[b], pid (dyn.band[0][b].release), "RELEASE", Rect (x + 140, y + 100, 100, 44), "Release.");
        addReadout (allGain[b], pid (dyn.band[0][b].gain), "GAIN", Rect (x + 140, y + 150, 100, 44), "Band gain.");
        allRatio[b].fontH = allAttack[b].fontH = allRelease[b].fontH = allGain[b].fontH = 13.0f;
    }
    linkSliders ({ &allComp[0], &allComp[1], &allComp[2], &allComp[3] }, pid (dyn.pLink));
    linkSliders ({ &allLim[0], &allLim[1], &allLim[2], &allLim[3] }, pid (dyn.pLink));
    linkSliders ({ &allRatio[0], &allRatio[1], &allRatio[2], &allRatio[3] }, pid (dyn.pLink));
    linkSliders ({ &allAttack[0], &allAttack[1], &allAttack[2], &allAttack[3] }, pid (dyn.pLink));
    linkSliders ({ &allRelease[0], &allRelease[1], &allRelease[2], &allRelease[3] }, pid (dyn.pLink));
    linkSliders ({ &allGain[0], &allGain[1], &allGain[2], &allGain[3] }, pid (dyn.pLink));
    selectBand (0);
}

void DynamicsPanel::selectBand (int b)
{
    const int nb = (int) pv (dyn.mb.pNumBands) + 1;
    selected = jlimit (0, nb - 1, b);
    xover.selectedBand = selected;
    bindBand();
}

void DynamicsPanel::bindBand()
{
    const auto& bi = dyn.band[viewSet][selected];
    rebind (limThreshFader, pid (bi.limThresh)); rebind (compThreshFader, pid (bi.compThresh));
    rebind (compRatioRo, pid (bi.compRatio)); rebind (limRatioRo, pid (bi.limRatio)); rebind (kneeRo, pid (bi.knee));
    rebind (attackRo, pid (bi.attack)); rebind (releaseRo, pid (bi.release)); rebind (parallelRo, pid (bi.parallel)); rebind (bandGainRo, pid (bi.gain));
    for (int b = 0; b < 4; ++b)
    {
        const auto& ab = dyn.band[viewSet][b];
        rebind (allLim[b], pid (ab.limThresh)); rebind (allComp[b], pid (ab.compThresh));
        rebind (allRatio[b], pid (ab.compRatio)); rebind (allAttack[b], pid (ab.attack)); rebind (allRelease[b], pid (ab.release)); rebind (allGain[b], pid (ab.gain));
    }
    const Colour col = colours::band (selected);
    limThreshFader.fill = compThreshFader.fill = col;
    threshMeter.colour = col;
    curveView.colour = col;
    trace.lanes[0].colour = col;
    showAll (! allView, limThreshFader, compThreshFader, threshMeter, compRatioRo, limRatioRo, kneeRo, attackRo, releaseRo, parallelRo, curveView, bandGainRo, globalGainRo);
    globalGainRo.setVisible (true);
    for (int b = 0; b < 4; ++b)
        showAll (allView, allLim[b], allMeter[b], allComp[b], allRatio[b], allAttack[b], allRelease[b], allGain[b]);
    selBtn.setLit (! allView);
    allBtn.setLit (allView);
    repaint();
}

void DynamicsPanel::refresh()
{
    ModulePanel::refresh();
    const int chMode = (int) pv (dyn.pChMode);
    const bool twoSets = chMode != Stereo;
    for (int i = 0; i < 2; ++i)
    {
        setBtn[i].setVisible (twoSets);
        setBtn[i].setLit (twoSets && viewSet == i);
        setBtn[i].setLabel (i == 0 ? "MID" : "SIDE");
    }
    if (! twoSets && viewSet != 0) { viewSet = 0; bindBand(); }
    for (int i = 0; i < 3; ++i) viewBtn[i].setLit (view == i);
    xover.setVisible (view == 0);
    trace.setVisible (view == 1);
    detView.setVisible (view == 2);
    showAll (view == 2, detFilterChoice, detFreqRo, detSlopeChoice, detTiltRo, detSoloBtn);
    const int df = (int) pv (dyn.pDetFilter);
    detFreqRo.setEnabled (df == Dynamics::DetHighpass); detSlopeChoice.setEnabled (df == Dynamics::DetHighpass); detTiltRo.setEnabled (df == Dynamics::DetTilt);
    detSoloBtn.setLit (dyn.detSolo.load());
    const int nb = (int) pv (dyn.mb.pNumBands) + 1;
    if (selected >= nb) selectBand (nb - 1);
    const int c = chMode == Stereo ? 0 : viewSet;
    const auto& t = dyn.tm[c][selected];
    threshMeter.inputDb = t.inDb; threshMeter.gainDb = t.gainDb; threshMeter.repaint();
    curveView.inputDb = t.inDb; curveView.gainDb = t.gainDb; curveView.repaint();
    for (int b = 0; b < 4; ++b)
    {
        allMeter[b].inputDb = dyn.tm[c][b].inDb; allMeter[b].gainDb = dyn.tm[c][b].gainDb; allMeter[b].repaint();
        const bool on = b < nb && pv (dyn.mb.pBandOn[b]) > 0.5f;
        enableAll (on, allLim[b], allComp[b], allRatio[b], allAttack[b], allRelease[b], allGain[b]);
    }
    if (view == 1)
    {
        float l[256], r[256];
        proc.chain.inputTap.latest (l, r, 256);
        float pk = 0.0f;
        for (int i = 0; i < 256; ++i) pk = jmax (pk, std::abs (l[i]), std::abs (r[i]));
        const float lane[1] { t.gainDb };
        trace.push (pk, lane);
    }
    xover.refresh();
    statusLine = "Band " + String (selected + 1) + ": " + (t.gainDb < -0.05f ? ids::decibels (t.gainDb) + " of reduction now" : t.gainDb > 0.05f ? ids::decibels (t.gainDb) + " of boost now" : "no gain change now")
               + (pv (dyn.pAutoGain) > 0.5f ? ", auto gain " + ids::decibels (t.autoDb) : "") + ".";
}

void DynamicsPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
}

// ===========================================================================
// ExciterPanel
// ===========================================================================
ExciterPanel::ExciterPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModExciter, s), exc (p.chain.exciter), xover (*this, exc.mb, exc.soloBand)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, exc.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (92), i == 0 ? "Bands over the spectrum." : "Where the saturation lands, and a high shelf on the wet signal to tame it.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    addChoice (osChoice, pid (exc.pOversample), {}, h.removeFromLeft (80), "Oversampling of the saturation to keep aliasing down. Costs CPU.");
    osChoice.fontH = 12.0f;
    h.removeFromLeft (10);
    addToggle (linkBtn, pid (exc.pLink), h.removeFromLeft (84), "Move every band's control together.");
    linkBtn.setFontHeight (10.0f);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at dips in the spectrum.", [this] (const ModifierKeys&)
    {
        const int nb = (int) pv (exc.mb.pNumBands) + 1;
        for (int i = 0; i < nb - 1; ++i)
        {
            const float want = pv (exc.mb.pXover[i]);
            float best = want, bd = 1e9f;
            const float* cv = spectrum.curve();
            for (int c = 8; c < SpectrumSource::kColumns - 8; ++c)
            {
                const float hz = SpectrumSource::columnHz (c);
                bool isMin = true;
                for (int k = -6; k <= 6; ++k) if (cv[c + k] < cv[c]) isMin = false;
                const float d = std::abs (std::log2 (hz / want));
                if (isMin && d < bd && d < 1.0f) { bd = d; best = hz; }
            }
            setp (exc.mb.pXover[i], best);
        }
    });
    learnBtn.setFontHeight (10.0f);
    h.removeFromLeft (10);
    for (int i = 0; i < 2; ++i)
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; bindBands(); });

    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;
    addAndMakeVisible (postView);
    postView.setBounds (kViewArea);
    postView.source = &spectrum;
    addReadout (postFreqRo, pid (exc.pPostFreq), "POST FILTER FREQ", Rect (kViewArea.getX() + 14, kViewArea.getY() + 12, 130, 50), "High shelf on the wet signal: frequency.");
    addReadout (postGainRo, pid (exc.pPostGain), "POST FILTER GAIN", Rect (kViewArea.getX() + 154, kViewArea.getY() + 12, 130, 50), "High shelf on the wet signal: cut.");

    for (int b = 0; b < 4; ++b)
    {
        const int x = kControlArea.getX() + b * 268;
        const int y = kControlArea.getY();
        addChoice (modeChoice[b], pid (exc.band[0][b].mode), "MODE", Rect (x, y, 150, 50), "ANALOG: driven odd harmonics. RETRO: a few, slowly decaying. TAPE: brighter. TUBE: harder on attacks. WARM: even harmonics only. TRIODE and DUAL TRIODE: tube circuits, the second with more drive.");
        addFader (amountFader[b], pid (exc.band[0][b].amount), "AMOUNT", Rect (x + 160, y, 46, 220), "How hard this band is driven.");
        addFader (mixFader[b], pid (exc.band[0][b].mix), "MIX", Rect (x + 210, y, 46, 220), "How much of the saturated signal is blended back in.");
        amountFader[b].fill = mixFader[b].fill = colours::band (b);
    }
    linkSliders ({ &amountFader[0], &amountFader[1], &amountFader[2], &amountFader[3] }, pid (exc.pLink));
    linkSliders ({ &mixFader[0], &mixFader[1], &mixFader[2], &mixFader[3] }, pid (exc.pLink));
    bindBands();
}

void ExciterPanel::bindBands()
{
    for (int b = 0; b < 4; ++b)
    {
        rebind (amountFader[b], pid (exc.band[viewSet][b].amount));
        rebind (mixFader[b], pid (exc.band[viewSet][b].mix));
        for (auto& c : choices) if (c.first == &modeChoice[b]) c.second = pid (exc.band[viewSet][b].mode);
        modeChoice[b].onOpen = [this, b] { openChoiceMenu (pid (exc.band[viewSet][b].mode), &modeChoice[b]); };
        modeChoice[b].getText = [this, b] { if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (pid (exc.band[viewSet][b].mode)))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    }
}

void ExciterPanel::refresh()
{
    ModulePanel::refresh();
    const bool twoSets = (int) pv (exc.pChMode) != Stereo;
    for (int i = 0; i < 2; ++i) { setBtn[i].setVisible (twoSets); setBtn[i].setLit (twoSets && viewSet == i); setBtn[i].setLabel (i == 0 ? "MID" : "SIDE"); }
    if (! twoSets && viewSet != 0) { viewSet = 0; bindBands(); }
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    xover.setVisible (view == 0); postView.setVisible (view == 1);
    postFreqRo.setVisible (view == 1); postGainRo.setVisible (view == 1);
    const int nb = (int) pv (exc.mb.pNumBands) + 1;
    for (int b = 0; b < 4; ++b)
    {
        const bool on = b < nb && pv (exc.mb.pBandOn[b]) > 0.5f;
        modeChoice[b].setEnabled (on); amountFader[b].setEnabled (on); mixFader[b].setEnabled (on);
        modeChoice[b].setVisible (b < nb); amountFader[b].setVisible (b < nb); mixFader[b].setVisible (b < nb);
    }
    xover.refresh();
    postView.repaint();
    repaint();
}

void ExciterPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    const int nb = (int) pv (exc.mb.pNumBands) + 1;
    for (int b = 0; b < nb; ++b)
    {
        const int x = kControlArea.getX() + b * 268;
        drawSectionLocal (g, Rect (x - 6, kControlArea.getY() - 10, 264, 240).toFloat(), "BAND " + String (b + 1));
        // Harmonic level LED.
        drawLed (g, { (float) x + 240.0f, (float) kControlArea.getY() - 2.0f }, 5.0f, colours::band (b), exc.harmonicLevel[b] > 0.02f, exc.harmonicLevel[b]);
    }
    if (view == 1)
    {
        // Harmonic histogram: brighter fill where saturation is added.
        const auto r = postView.getBounds().toFloat();
        for (int b = 0; b < nb; ++b)
        {
            const float x0 = b == 0 ? r.getX() : postView.xForHz (pv (exc.mb.pXover[b - 1])) + r.getX();
            const float x1 = b == nb - 1 ? r.getRight() : postView.xForHz (pv (exc.mb.pXover[b])) + r.getX();
            g.setColour (colours::ink.withAlpha (0.35f * exc.harmonicLevel[b]));
            g.fillRect (Rectangle<float> (x0, r.getY() + 70.0f, x1 - x0, r.getHeight() - 90.0f));
        }
    }
}

// ===========================================================================
// ImagerPanel
// ===========================================================================
ImagerPanel::ImagerPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModImager, s), img (p.chain.imager), xover (*this, img.mb, img.soloBand)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, -1);
    auto h = headerArea().withTrimmedLeft (70);
    for (int i = 0; i < 3; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (100), i == 0 ? "Bands over the spectrum." : i == 1 ? "Stereo correlation over time: white in phase, red out of phase." : "A mirrored spectrum of the stereo width.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    addFader (amountFader, pid (img.pAmount), {}, h.removeFromLeft (150), "AMOUNT scales every band's width: 0% is no change, 100% the full settings.");
    h.removeFromLeft (10);
    addToggle (linkBtn, pid (img.pLink), h.removeFromLeft (84), "Move every band's width together.");
    linkBtn.setFontHeight (10.0f);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at dips in the spectrum.", [this] (const ModifierKeys&)
    {
        const int nb = (int) pv (img.mb.pNumBands) + 1;
        for (int i = 0; i < nb - 1; ++i)
        {
            const float want = pv (img.mb.pXover[i]);
            float best = want, bd = 1e9f;
            const float* cv = spectrum.curve();
            for (int c = 8; c < SpectrumSource::kColumns - 8; ++c)
            {
                const float hz = SpectrumSource::columnHz (c);
                bool isMin = true;
                for (int k = -6; k <= 6; ++k) if (cv[c + k] < cv[c]) isMin = false;
                const float d = std::abs (std::log2 (hz / want));
                if (isMin && d < bd && d < 1.0f) { bd = d; best = hz; }
            }
            setp (img.mb.pXover[i], best);
        }
    });
    learnBtn.setFontHeight (10.0f);

    const Rect viewR = kViewArea.withWidth (740);
    addAndMakeVisible (xover);
    xover.setBounds (viewR);
    xover.source = &spectrum;
    addAndMakeVisible (corrTrace);
    corrTrace.setBounds (viewR);
    corrTrace.rangeDb = 2.0f;
    corrTrace.lanes.push_back ({ colours::ink, true, {} });
    corrTrace.setTap (&proc.chain.outputTap);
    addAndMakeVisible (widthView);
    widthView.setBounds (viewR);
    widthView.showLabels = true;
    addAndMakeVisible (scope);
    scope.setBounds (Rect (viewR.getRight() + 12, kViewArea.getY(), 320, 340));
    scope.setTap (&proc.chain.outputTap);

    auto c = kControlArea;
    for (int b = 0; b < 4; ++b)
    {
        addFader (widthFader[b], pid (img.pWidth[b]), "WIDTH " + String (b + 1), c.removeFromLeft (70), "Side gain for this band: +100 doubles the sides, -100 makes it mono.");
        widthFader[b].bipolar = true;
        widthFader[b].fill = colours::band (b);
        c.removeFromLeft (10);
    }
    c.removeFromLeft (30);
    auto st = c.removeFromLeft (300);
    addToggle (stereoizeBtn, pid (img.pStereoizeOn), st.removeFromTop (34).withWidth (120), "Adds width to mono or narrow sources by putting a decorrelated copy of the middle into the sides. Mono compatible.");
    stereoizeBtn.setFontHeight (10.0f);
    st.removeFromTop (8);
    addFader (stereoizeFader, pid (img.pStereoizeAmount), "STEREOIZE AMOUNT", st.removeFromTop (44), "How much.");
    st.removeFromTop (6);
    addChoice (stereoizeMode, pid (img.pStereoizeMode), "MODE", st.removeFromTop (50).withWidth (160), "MODE I: a delayed copy. MODE II: a diffused copy that keeps transients sharper.");
    c.removeFromLeft (30);
    auto rc = c.removeFromLeft (300);
    addToggle (recoverBtn, pid (img.pRecoverOn), rc.removeFromTop (34).withWidth (140), "When a band is narrowed, what was removed from the sides comes back in the middle instead of being lost.");
    recoverBtn.setFontHeight (10.0f);
    addButton (recoverSoloBtn, Rect (rc.getX() + 150, rc.getY() - 34, 30, 34), "Hear only the recovered sides.", [this] (const ModifierKeys&) { img.recoverSolo.store (! img.recoverSolo.load()); });
    rc.removeFromTop (8);
    addFader (recoverFader, pid (img.pRecoverGain), "RECOVER GAIN", rc.removeFromTop (44), "Gain of the recovered signal.");
    recoverFader.bipolar = true;
    for (int i = 0; i < 3; ++i)
    {
        addButton (scopeBtn[i], Rect (viewR.getRight() + 12 + i * 108, kViewArea.getY() + 346, 104, 24), "Vectorscope style.", [this, i] (const ModifierKeys&) { scope.mode = i; });
        scopeBtn[i].setFontHeight (9.0f);
    }
    linkSliders ({ &widthFader[0], &widthFader[1], &widthFader[2], &widthFader[3] }, pid (img.pLink));
}

void ImagerPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 3; ++i) viewBtn[i].setLit (view == i);
    for (int i = 0; i < 3; ++i) scopeBtn[i].setLit (scope.mode == i);
    xover.setVisible (view == 0); corrTrace.setVisible (view == 1); widthView.setVisible (view == 2);
    const int nb = (int) pv (img.mb.pNumBands) + 1;
    const float amount = pv (img.pAmount) * 0.01f;
    for (int b = 0; b < 4; ++b)
    {
        widthFader[b].setVisible (b < nb);
        widthFader[b].setEnabled (pv (img.mb.pBandOn[b]) > 0.5f);
        widthFader[b].fillScale = amount;
        widthFader[b].repaint();
    }
    bool wider = false, narrower = false;
    for (int b = 0; b < nb; ++b) { if (pv (img.pWidth[b]) > 0.5f) wider = true; if (pv (img.pWidth[b]) < -0.5f) narrower = true; }
    stereoizeBtn.setEnabled (wider); stereoizeFader.setEnabled (wider); stereoizeMode.setEnabled (wider);
    recoverBtn.setEnabled (narrower); recoverFader.setEnabled (narrower);
    recoverSoloBtn.setLit (img.recoverSolo.load());
    scope.correlation = img.correlation;
    scope.balance = img.balance;
    scope.refresh();
    if (view == 1)
    {
        const float lane[1] { img.correlation < 0.0f ? -1.0f - img.correlation : 1.0f - img.correlation };
        corrTrace.lanes[0].colour = img.correlation < 0.0f ? colours::ledRed : colours::ink;
        corrTrace.push (0.0f, lane);
    }
    if (view == 2) widthView.repaint();
    xover.refresh();
    statusLine = "Correlation " + String (img.correlation, 2) + (img.correlation < 0.0f ? " (out of phase: check in mono)" : "") + ", balance " + (std::abs (img.balance) < 0.05f ? "centred" : img.balance < 0 ? "left" : "right") + ".";
}

void ImagerPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawText (g, "AMOUNT", { 390.0f, 44.0f, 150.0f, 12.0f }, 9.0f, colours::inkDim, true);
    drawSectionLocal (g, Rect (kControlArea.getX() + 344, kControlArea.getY() - 6, 310, 170).toFloat(), "STEREOIZE");
    drawSectionLocal (g, Rect (kControlArea.getX() + 674, kControlArea.getY() - 6, 310, 170).toFloat(), "RECOVER SIDES");
    if (view == 2)
    {
        // Mirrored width spectrum: side level above the centre line, mid below, per column.
        const auto r = widthView.getBounds().toFloat().reduced (1.0f);
        const int nb = (int) pv (img.mb.pNumBands) + 1;
        for (int b = 0; b < nb; ++b)
        {
            const float x0 = b == 0 ? r.getX() : r.getX() + freqToNorm (pv (img.mb.pXover[b - 1])) * r.getWidth();
            const float x1 = b == nb - 1 ? r.getRight() : r.getX() + freqToNorm (pv (img.mb.pXover[b])) * r.getWidth();
            const float w = (img.bandWidth[b] + 1.0f) * 0.5f;   // 0 mono .. 1 all side
            const float hh = w * r.getHeight() * 0.45f;
            g.setColour (colours::band (b).withAlpha (0.5f));
            g.fillRect (Rectangle<float> (x0 + 2.0f, r.getCentreY() - hh, x1 - x0 - 4.0f, hh * 2.0f));
        }
        g.setColour (colours::ink);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        drawText (g, "WIDE", { r.getX() + 6.0f, r.getY() + 4.0f, 60.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, "MONO", { r.getX() + 6.0f, r.getCentreY() - 14.0f, 60.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredLeft);
    }
}

// ===========================================================================
// ImpactPanel
// ===========================================================================
ImpactPanel::ImpactPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModImpact, s), imp (p.chain.impact), xover (*this, imp.mb, imp.soloBand)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, imp.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    addToggle (stereoLinkBtn, pid (imp.pStereoLink), h.removeFromLeft (96), "Apply the same change to both sides, judged from the middle.");
    stereoLinkBtn.setFontHeight (10.0f);
    h.removeFromLeft (10);
    addFader (amountFader, pid (imp.pAmount), {}, h.removeFromLeft (150), "AMOUNT scales every band's impact.");
    h.removeFromLeft (10);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at dips in the spectrum.", [this] (const ModifierKeys&)
    {
        const int nb = (int) pv (imp.mb.pNumBands) + 1;
        for (int i = 0; i < nb - 1; ++i)
        {
            const float want = pv (imp.mb.pXover[i]);
            float best = want, bd = 1e9f;
            const float* cv = spectrum.curve();
            for (int c = 8; c < SpectrumSource::kColumns - 8; ++c)
            {
                const float hz = SpectrumSource::columnHz (c);
                bool isMin = true;
                for (int k = -6; k <= 6; ++k) if (cv[c + k] < cv[c]) isMin = false;
                const float d = std::abs (std::log2 (hz / want));
                if (isMin && d < bd && d < 1.0f) { bd = d; best = hz; }
            }
            setp (imp.mb.pXover[i], best);
        }
    });
    learnBtn.setFontHeight (10.0f);
    h.removeFromLeft (10);
    for (int i = 0; i < 2; ++i)
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; bindBands(); });

    addAndMakeVisible (xover);
    xover.setBounds (kViewArea.withHeight (220));
    xover.source = &spectrum;
    addAndMakeVisible (trace);
    trace.setBounds (kViewArea.withTrimmedTop (228));
    trace.rangeDb = 12.0f;
    for (int b = 0; b < 4; ++b) trace.lanes.push_back ({ colours::band (b), true, {} });
    trace.setTap (&proc.chain.inputTap);

    auto c = kControlArea;
    for (int b = 0; b < 4; ++b)
    {
        addFader (impactFader[b], pid (imp.pImpact[0][b]), "IMPACT " + String (b + 1), c.removeFromLeft (70), "Positive opens up the microdynamics (punchier); negative packs them together (denser).");
        impactFader[b].bipolar = true;
        impactFader[b].fill = colours::band (b);
        c.removeFromLeft (12);
    }
    c.removeFromLeft (40);
    auto col = c.removeFromLeft (200);
    addReadout (envelopeRo, pid (imp.pEnvelope), "ENVELOPE", col.removeFromTop (56), "How long a microdynamic event lasts before the gain returns.");
    col.removeFromTop (8);
    addChoice (envSyncChoice, pid (imp.pEnvelopeSync), "ENVELOPE (SYNC)", col.removeFromTop (56), "Envelope as a beat division of the host tempo.");
    col.removeFromTop (10);
    addToggle (syncBtn, pid (imp.pSync), col.removeFromTop (34).withWidth (90), "Set the envelope in beats rather than milliseconds.");
    syncBtn.setFontHeight (10.0f);
    c.removeFromLeft (30);
    col = c.removeFromLeft (200);
    addToggle (autoGainBtn, pid (imp.pAutoGain), col.removeFromTop (34).withWidth (120), "Keep the average level where it was.");
    autoGainBtn.setFontHeight (10.0f);
    col.removeFromTop (10);
    addToggle (linkBtn, pid (imp.pLink), col.removeFromTop (34).withWidth (120), "Move every band's impact together.");
    linkBtn.setFontHeight (10.0f);
    linkSliders ({ &impactFader[0], &impactFader[1], &impactFader[2], &impactFader[3] }, pid (imp.pLink));
    bindBands();
}

void ImpactPanel::bindBands()
{
    for (int b = 0; b < 4; ++b)
        rebind (impactFader[b], pid (imp.pImpact[viewSet][b]));
}

void ImpactPanel::refresh()
{
    ModulePanel::refresh();
    const bool twoSets = (int) pv (imp.pChMode) != Stereo;
    for (int i = 0; i < 2; ++i) { setBtn[i].setVisible (twoSets); setBtn[i].setLit (twoSets && viewSet == i); setBtn[i].setLabel (i == 0 ? "MID" : "SIDE"); }
    if (! twoSets && viewSet != 0) { viewSet = 0; bindBands(); }
    const int nb = (int) pv (imp.mb.pNumBands) + 1;
    const float amount = pv (imp.pAmount) * 0.01f;
    for (int b = 0; b < 4; ++b)
    {
        impactFader[b].setVisible (b < nb);
        impactFader[b].setEnabled (pv (imp.mb.pBandOn[b]) > 0.5f);
        impactFader[b].fillScale = amount;
        impactFader[b].repaint();
    }
    const bool sync = pv (imp.pSync) > 0.5f;
    envelopeRo.setEnabled (! sync); envSyncChoice.setEnabled (sync);
    float l[256], r[256];
    proc.chain.inputTap.latest (l, r, 256);
    float pk = 0.0f;
    for (int i = 0; i < 256; ++i) pk = jmax (pk, std::abs (l[i]), std::abs (r[i]));
    float lanes[4];
    const int ch = twoSets ? viewSet : 0;
    for (int b = 0; b < 4; ++b) lanes[b] = b < nb ? -imp.gainDb[ch][b] : 0.0f;   // drawn from the top: boost goes up
    trace.dimAllBut = -1;
    for (int b = 0; b < 4; ++b)
        if (impactFader[b].isMouseButtonDown()) trace.dimAllBut = b;
    trace.push (pk, lanes);
    xover.refresh();
    statusLine = sync ? "Envelope " + String (Impact::syncDivisionName ((int) pv (imp.pEnvelopeSync))) + " at " + String (proc.hostBpm(), 0) + " bpm." : "";
}

void ImpactPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawText (g, "AMOUNT", { 302.0f, 44.0f, 150.0f, 12.0f }, 9.0f, colours::inkDim, true);
    drawText (g, "GAIN TRACES: up is boost, down is cut, per band", { (float) kViewArea.getX() + 8.0f, (float) kViewArea.getY() + 232.0f, 400.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
}
} // namespace burnish::ui
