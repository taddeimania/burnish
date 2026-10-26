#include "ModulePanels.h"

namespace burnish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

namespace
{
void pushTrace (TraceView& t, const Tap& tap, const float* lanes)
{
    float l[256], r[256];
    tap.latest (l, r, 256);
    float pk = 0.0f;
    for (int i = 0; i < 256; ++i) pk = jmax (pk, std::abs (l[i]), std::abs (r[i]));
    t.push (pk, lanes);
}

void learnRegion (ModulePanel& panel, SpectrumSource& spectrum, int lowParam, int highParam, float minHz, float maxHz)
{
    // The action region around the brightest, narrowest part of the spectrum
    // relative to a smoothed version: where resonances live.
    const float* cv = spectrum.curve();
    int bestC = -1; float bestD = -1e9f;
    for (int c = 10; c < SpectrumSource::kColumns - 10; ++c)
    {
        const float hz = SpectrumSource::columnHz (c);
        if (hz < minHz || hz > maxHz) continue;
        float sm = 0.0f;
        for (int k = -10; k <= 10; ++k) sm += cv[c + k];
        sm /= 21.0f;
        const float d = cv[c] - sm;
        if (d > bestD) { bestD = d; bestC = c; }
    }
    if (bestC < 0) return;
    const float hz = SpectrumSource::columnHz (bestC);
    panel.setp (lowParam, jlimit (minHz, maxHz, hz / 2.0f));
    panel.setp (highParam, jlimit (minHz, maxHz, hz * 2.0f));
}
} // namespace

// ===========================================================================
// LowEndFocusPanel
// ===========================================================================
LowEndFocusPanel::LowEndFocusPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModLowEndFocus, s), lef (p.chain.lowEndFocus), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, lef.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; rebind (contrastKnob, pid (lef.pContrast[i])); rebind (gainKnob, pid (lef.pGain[i])); });
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.gainDb = curve;
    view.colour = colours::band (0);
    view.lowParam = lef.pLow; view.highParam = lef.pHigh;
    view.regionMinHz = 20.0f; view.regionMaxHz = 300.0f;
    view.gainTop = 12.0f; view.gainBottom = -12.0f;

    auto c = kControlArea.withTrimmedTop (10);
    auto col = c.removeFromLeft (160);
    for (int i = 0; i < 2; ++i)
    {
        addButton (modeBtn[i], col.removeFromTop (40), i == 0 ? "PUNCHY: fast, brings out transients." : "SMOOTH: slower, works on what sustains.", [this, i] (const ModifierKeys&) { setp (lef.pMode, (float) i); });
        modeBtn[i].setFontHeight (12.0f);
        col.removeFromTop (8);
    }
    col.removeFromTop (10);
    addButton (soloBtn, col.removeFromTop (40), "Hear the action region before processing.", [this] (const ModifierKeys&) { lef.regionSolo.store (! lef.regionSolo.load()); });
    soloBtn.setFontHeight (11.0f);
    c.removeFromLeft (60);
    addKnob (contrastKnob, pid (lef.pContrast[0]), "CONTRAST", c.removeFromLeft (170).withHeight (190), "Positive: more difference between loud and quiet in the region, so transients stand out and the mud under them drops. Negative: less, blurring the low end like saturation would.", false);
    contrastKnob.bipolar = true;
    c.removeFromLeft (30);
    addKnob (gainKnob, pid (lef.pGain[0]), "GAIN", c.removeFromLeft (150).withHeight (190), "Make-up gain on the action region.", false);
    gainKnob.bipolar = true;
}

void LowEndFocusPanel::refresh()
{
    ModulePanel::refresh();
    const bool twoSets = (int) pv (lef.pChMode) != Stereo;
    for (int i = 0; i < 2; ++i) { setBtn[i].setVisible (twoSets); setBtn[i].setLit (twoSets && viewSet == i); setBtn[i].setLabel (i == 0 ? "MID" : "SIDE"); }
    if (! twoSets && viewSet != 0) { viewSet = 0; rebind (contrastKnob, pid (lef.pContrast[0])); rebind (gainKnob, pid (lef.pGain[0])); }
    for (int i = 0; i < 2; ++i) modeBtn[i].setLit ((int) pv (lef.pMode) == i);
    soloBtn.setLit (lef.regionSolo.load());
    // The difference curve: the region's gain now, with soft edges.
    const float lo = pv (lef.pLow), hi = pv (lef.pHigh);
    const float g = lef.regionGainDb[twoSets ? viewSet : 0];
    for (int b = 0; b < TonalTargets::kNumBands; ++b)
    {
        const float hz = TonalTargets::centreHz (b);
        const float edge = jmin (jlimit (0.0f, 1.0f, std::log2 (hz / lo) * 3.0f + 1.0f), jlimit (0.0f, 1.0f, std::log2 (hi / hz) * 3.0f + 1.0f));
        curve[b] = g * edge;
    }
    view.repaint();
    statusLine = "Region " + ids::hertz (lo) + " to " + ids::hertz (hi) + ", " + ids::decibels (g) + " now.";
}

void LowEndFocusPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea.getX() - 6, kControlArea.getY() + 2, 170, 190).toFloat(), "MODE");
}

// ===========================================================================
// ClarityPanel
// ===========================================================================
ClarityPanel::ClarityPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModClarity, s), clr (p.chain.clarity), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, clr.pChMode);
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.gainDb = clr.gainDb[0];
    view.specDb = clr.specDb[0];
    view.colour = colours::band (1);
    view.lowParam = clr.pLow; view.highParam = clr.pHigh;
    view.regionMinHz = 300.0f; view.regionMaxHz = 20000.0f;
    view.gainTop = 8.0f; view.gainBottom = -8.0f;
    auto c = kControlArea;
    addKnob (amountKnob, pid (clr.pAmount), "AMOUNT", c.removeFromLeft (200).withHeight (200), "How far toward the target the spectrum is contoured. At 100 the most it can move anything is 6 dB.", false);
    c.removeFromLeft (30);
    addKnob (tiltKnob, pid (clr.pTilt), "TILT", c.removeFromLeft (170).withHeight (200), "The target: 0 is pink noise; +3 dB/oct is white (brighter); -3 is brown (darker).", false);
    tiltKnob.bipolar = true;
    c.removeFromLeft (30);
    addKnob (attackKnob, pid (clr.pAttack), "ATTACK", c.removeFromLeft (150).withHeight (200), "How fast the contour follows a rise.", false);
    c.removeFromLeft (10);
    addKnob (releaseKnob, pid (clr.pRelease), "RELEASE", c.removeFromLeft (150).withHeight (200), "How fast it lets go.", false);
}

void ClarityPanel::refresh()
{
    ModulePanel::refresh();
    const int ch = (int) pv (clr.pChMode) == Stereo ? 0 : 0;
    view.gainDb = clr.gainDb[ch];
    view.specDb = clr.specDb[ch];
    view.repaint();
    float mx = 0.0f;
    for (int b = 0; b < TonalTargets::kNumBands; ++b) mx = jmax (mx, std::abs (clr.gainDb[0][b]));
    statusLine = "Contouring toward " + String (pv (clr.pTilt), 1) + " dB/oct noise between " + ids::hertz (pv (clr.pLow)) + " and " + ids::hertz (pv (clr.pHigh)) + "; largest move now " + String (mx, 1) + " dB.";
}

void ClarityPanel::paint (Graphics& g) { ModulePanel::paint (g); }

// ===========================================================================
// StabilizerPanel
// ===========================================================================
StabilizerPanel::StabilizerPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModStabilizer, s), stb (p.chain.stabilizer), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, stb.pChMode);
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.gainDb = stb.gainDb[0];
    view.specDb = stb.specDb[0];
    view.colour = colours::band (3);
    view.gainTop = 10.0f; view.gainBottom = -10.0f;
    view.targetDb = [this] (float hz)
    {
        // Target shape relative to its mean, scaled to the view.
        const float pos = std::log2 (jmax (20.0f, hz) / 20.0f) * 6.0f;
        const int b = jlimit (0, TonalTargets::kNumBands - 1, (int) pos);
        return stb.targetDb (b) * 0.4f;
    };
    auto c = kControlArea;
    auto col = c.removeFromLeft (220);
    addChoice (targetChoice, pid (stb.pTarget), "TARGET", col.removeFromTop (54), "The tonal balance to aim for. ASSISTANT is the shape of a reference you gave the assistant.");
    col.removeFromTop (12);
    auto mb = col.removeFromTop (40);
    for (int i = 0; i < 2; ++i)
    {
        addButton (modeBtn[i], mb.removeFromLeft (104), i == 0 ? "SHAPE: boosts and cuts toward the target, loudness neutral." : "CUT: only cuts what sticks out above the target.", [this, i] (const ModifierKeys&) { setp (stb.pMode, (float) i); });
        modeBtn[i].setFontHeight (11.0f);
        mb.removeFromLeft (8);
    }
    col.removeFromTop (12);
    addToggle (tameBtn, pid (stb.pTame), col.removeFromTop (40).withWidth (170), "React instantly to transients.");
    tameBtn.setFontHeight (10.0f);
    c.removeFromLeft (20);
    addKnob (amountKnob, pid (stb.pAmount), "AMOUNT", c.removeFromLeft (150).withHeight (190), "Scale of the corrections; at 100 the most is 9 dB.", false);
    addKnob (speedKnob, pid (stb.pSpeed), "SPEED", c.removeFromLeft (120).withHeight (160).translated (0, 15), "How quickly it follows the audio. Fast is precise but can be heard working.");
    addKnob (smoothingKnob, pid (stb.pSmoothing), "SMOOTHING", c.removeFromLeft (120).withHeight (160).translated (0, 15), "In SHAPE mode: 100 looks like three or four broad filters, 0 like many narrow ones.");
    addKnob (sensitivityKnob, pid (stb.pSensitivity), "SENSITIVITY", c.removeFromLeft (120).withHeight (160).translated (0, 15), "How small a deviation is corrected. 0 lets all but the worst resonances through.");
    c.removeFromLeft (20);
    addKnob (lowKnob, pid (stb.pLow), "LOW", c.removeFromLeft (90).withHeight (120).translated (0, 35), "Amount below 100 Hz.");
    addKnob (midKnob, pid (stb.pMid), "MID", c.removeFromLeft (90).withHeight (120).translated (0, 35), "Amount from 100 Hz to 5.6 kHz.");
    addKnob (highKnob, pid (stb.pHigh), "HIGH", c.removeFromLeft (90).withHeight (120).translated (0, 35), "Amount above 5.6 kHz.");
}

void StabilizerPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 2; ++i) modeBtn[i].setLit ((int) pv (stb.pMode) == i);
    smoothingKnob.setEnabled ((int) pv (stb.pMode) == 0);
    view.repaint();
    statusLine = String ((int) pv (stb.pMode) == 0 ? "SHAPE" : "CUT") + " toward " + targetChoice.getText() + ". The grey line is the target's shape; the coloured curve is the correction now.";
}

void StabilizerPanel::paint (Graphics& g) { ModulePanel::paint (g); }

// ===========================================================================
// ShaperPanel
// ===========================================================================
ShaperPanel::ShaperPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModSpectralShaper, s), shp (p.chain.spectralShaper), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, shp.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (100), i == 0 ? "The reduction across the spectrum, with the action region." : "A scrolling trace of the reduction.", [this, i] (const ModifierKeys&) { viewIdx = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    addButton (learnBtn, h.removeFromLeft (60), "Put the action region around the most resonant part of the spectrum.", [this] (const ModifierKeys&) { learnRegion (*this, spectrum, shp.pLow, shp.pHigh, 20.0f, 20000.0f); });
    learnBtn.setFontHeight (10.0f);
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.gainDb = shp.gainDb[0];
    view.colour = colours::band (2);
    view.lowParam = shp.pLow; view.highParam = shp.pHigh;
    view.gainTop = 6.0f; view.gainBottom = -24.0f;
    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.lanes.push_back ({ colours::band (2), true, {} });
    auto c = kControlArea.withTrimmedTop (10);
    auto col = c.removeFromLeft (130);
    for (int i = 0; i < 3; ++i)
    {
        addButton (modeBtn[i], col.removeFromTop (40), "How much sticks out before it is shaped: LOW only the worst, HIGH anything above the envelope.", [this, i] (const ModifierKeys&) { setp (shp.pMode, (float) i); });
        modeBtn[i].setFontHeight (11.0f);
        col.removeFromTop (8);
    }
    c.removeFromLeft (40);
    addKnob (amountKnob, pid (shp.pAmount), "AMOUNT", c.removeFromLeft (180).withHeight (200), "How much of the excess is removed.", false);
    addKnob (toneKnob, pid (shp.pTone), "TONE", c.removeFromLeft (150).withHeight (170).translated (0, 15), "Positive leaves the highs brighter; negative darker.");
    toneKnob.bipolar = true;
    addKnob (attackKnob, pid (shp.pAttack), "ATTACK", c.removeFromLeft (130).withHeight (150).translated (0, 25), "Time to apply a reduction.");
    addKnob (releaseKnob, pid (shp.pRelease), "RELEASE", c.removeFromLeft (130).withHeight (150).translated (0, 25), "Time to let it go.");
}

void ShaperPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (viewIdx == i);
    view.setVisible (viewIdx == 0); trace.setVisible (viewIdx == 1);
    for (int i = 0; i < 3; ++i) modeBtn[i].setLit ((int) pv (shp.pMode) == i);
    float mn = 0.0f;
    for (int b = 0; b < TonalTargets::kNumBands; ++b) mn = jmin (mn, shp.gainDb[0][b]);
    if (viewIdx == 1) { const float lane[1] { mn }; pushTrace (trace, proc.chain.inputTap, lane); }
    view.repaint();
    statusLine = "Deepest reduction now " + String (-mn, 1) + " dB, between " + ids::hertz (pv (shp.pLow)) + " and " + ids::hertz (pv (shp.pHigh)) + ".";
}

void ShaperPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea.getX() - 6, kControlArea.getY() + 2, 140, 150).toFloat(), "MODE");
}

// ===========================================================================
// MatchEqPanel
// ===========================================================================
void MatchEqPanel::MatchView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    auto drawSnapshot = [&] (const std::vector<float>& db, Colour c)
    {
        Path p;
        bool started = false;
        // Relative to the mean in 100 Hz .. 8 kHz, scaled onto the gain axis.
        double sum = 0; int cnt = 0;
        for (int k = 1; k < MatchEq::kBins; ++k)
        {
            const float hz = MatchEq::binHz (k, panel.proc.getSampleRate() > 0 ? panel.proc.getSampleRate() : 48000.0);
            if (hz >= 100.0f && hz <= 8000.0f && db[(size_t) k] > -150.0f) { sum += db[(size_t) k]; ++cnt; }
        }
        if (cnt == 0) return;
        const float mean = (float) (sum / cnt);
        const double sr = panel.proc.getSampleRate() > 0 ? panel.proc.getSampleRate() : 48000.0;
        for (int k = 1; k < MatchEq::kBins; ++k)
        {
            const float hz = MatchEq::binHz (k, sr);
            if (hz < 20.0f || hz > 20000.0f || db[(size_t) k] < -150.0f) continue;
            const float v = (db[(size_t) k] - mean) * 0.4f;
            const float x = xForHz (hz), y = jlimit (r.getY(), r.getBottom(), yForGain (v));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (c.withAlpha (0.8f));
        g.strokePath (p, PathStrokeType (1.0f));
    };
    drawSnapshot (panel.mtc.referenceDb, Colour (0xffd9a84c));
    drawSnapshot (panel.mtc.applyDb, Colour (0xff5c9ad6));
    if (panel.mtc.hasReference() && panel.mtc.hasApply())
    {
        const double sr = panel.proc.getSampleRate() > 0 ? panel.proc.getSampleRate() : 48000.0;
        Path p;
        bool started = false;
        for (int k = 1; k < MatchEq::kBins; ++k)
        {
            const float hz = MatchEq::binHz (k, sr);
            if (hz < 20.0f || hz > 20000.0f) continue;
            const float x = xForHz (hz), y = jlimit (r.getY(), r.getBottom(), yForGain (panel.mtc.curveDb[(size_t) k]));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (colours::ink);
        g.strokePath (p, PathStrokeType (2.0f));
    }
    const float lo = panel.pv (panel.mtc.pLow), hi = panel.pv (panel.mtc.pHigh);
    for (int i = 0; i < 2; ++i)
    {
        const float x = xForHz (i == 0 ? lo : hi);
        g.setColour (colours::ink.withAlpha (0.7f));
        g.drawLine (x, r.getY(), x, r.getBottom(), 1.2f);
        const auto h = Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getBottom() - 30.0f });
        g.setColour (colours::cream);
        g.fillEllipse (h);
        drawText (g, ids::hertz (i == 0 ? lo : hi), { x - 40.0f, r.getBottom() - 54.0f, 80.0f, 14.0f }, 10.0f, colours::ink, true);
    }
}

void MatchEqPanel::MatchView::mouseDown (const MouseEvent& e)
{
    const float x0 = xForHz (panel.pv (panel.mtc.pLow)), x1 = xForHz (panel.pv (panel.mtc.pHigh));
    dragging = std::abs (e.position.x - x0) < 8.0f ? 0 : std::abs (e.position.x - x1) < 8.0f ? 1 : -1;
}

void MatchEqPanel::MatchView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const float hz = jlimit (20.0f, 20000.0f, hzForX (e.position.x));
    if (dragging == 0) panel.setp (panel.mtc.pLow, jmin (hz, panel.pv (panel.mtc.pHigh) / 1.1f));
    else panel.setp (panel.mtc.pHigh, jmax (hz, panel.pv (panel.mtc.pLow) * 1.1f));
}

MatchEqPanel::MatchEqPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModMatchEq, s), mtc (p.chain.matchEq), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, mtc.pChMode);
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.gainTop = 18.0f; view.gainBottom = -18.0f;
    auto c = kControlArea;
    auto ref = c.removeFromLeft (250);
    ref.removeFromTop (20);
    auto rr = ref.removeFromTop (40);
    addButton (refCapture, rr.removeFromLeft (80), "Play the reference and capture its average spectrum.", [this] (const ModifierKeys&) { mtc.capture.store (MatchEq::CaptureReference); });
    rr.removeFromLeft (4);
    addButton (refStop, rr.removeFromLeft (70), "Freeze the reference snapshot.", [this] (const ModifierKeys&) { if (mtc.capture.load() == MatchEq::CaptureReference) mtc.capture.store (MatchEq::Idle); });
    rr.removeFromLeft (4);
    addButton (refClear, rr.removeFromLeft (70), "Clear the reference snapshot.", [this] (const ModifierKeys&) { mtc.clearReference(); });
    for (auto* b : { &refCapture, &refStop, &refClear }) b->setFontHeight (10.0f);
    c.removeFromLeft (20);
    auto ap = c.removeFromLeft (250);
    ap.removeFromTop (20);
    auto ar = ap.removeFromTop (40);
    addButton (applyCapture, ar.removeFromLeft (80), "Play your track and capture its average spectrum.", [this] (const ModifierKeys&) { mtc.capture.store (MatchEq::CaptureApply); });
    ar.removeFromLeft (4);
    addButton (applyStop, ar.removeFromLeft (70), "Freeze the snapshot of your track.", [this] (const ModifierKeys&) { if (mtc.capture.load() == MatchEq::CaptureApply) mtc.capture.store (MatchEq::Idle); });
    ar.removeFromLeft (4);
    addButton (applyClear, ar.removeFromLeft (70), "Clear the snapshot of your track.", [this] (const ModifierKeys&) { mtc.clearApply(); });
    for (auto* b : { &applyCapture, &applyStop, &applyClear }) b->setFontHeight (10.0f);
    c.removeFromLeft (30);
    addKnob (smoothingKnob, pid (mtc.pSmoothing), "SMOOTHING", c.removeFromLeft (150).withHeight (180), "Higher smooths the matched curve into a broad shape; lower follows every peak and dip.", false);
    addKnob (amountKnob, pid (mtc.pAmount), "AMOUNT", c.removeFromLeft (150).withHeight (180), "How much of the match to apply. Under 50% is usually wise.", false);
    c.removeFromLeft (20);
    addReadout (lowRo, pid (mtc.pLow), "LOW CUTOFF", c.removeFromLeft (110).withHeight (54), "Below this the match fades out.");
    c.removeFromLeft (10);
    addReadout (highRo, pid (mtc.pHigh), "HIGH CUTOFF", c.removeFromLeft (110).withHeight (54), "Above this the match fades out.");
}

void MatchEqPanel::refresh()
{
    ModulePanel::refresh();
    const int cap = mtc.capture.load();
    refCapture.setLit (cap == MatchEq::CaptureReference);
    applyCapture.setLit (cap == MatchEq::CaptureApply);
    view.repaint();
    statusLine = String ("Reference: ") + (mtc.hasReference() ? String (mtc.referenceFrames()) + " frames" : "none") + ".  Apply to: "
               + (mtc.hasApply() ? String (mtc.applyFrames_()) + " frames" : "none") + ".  "
               + (mtc.hasReference() && mtc.hasApply() ? "Matched curve in white; gold is the reference, blue your track." : "Capture both to build a matched curve.");
}

void MatchEqPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea.getX() - 6, kControlArea.getY() + 8, 250, 70).toFloat(), "REFERENCE");
    drawSectionLocal (g, Rect (kControlArea.getX() + 264, kControlArea.getY() + 8, 250, 70).toFloat(), "APPLY TO");
}

// ===========================================================================
// MaximizerPanel
// ===========================================================================
MaximizerPanel::MaximizerPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModMaximizer, s), mx (p.chain.maximizer)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, -1);
    auto h = headerArea().withTrimmedLeft (70);
    for (int i = 0; i < 3; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (92), i == 0 ? "The spectrum of the output." : i == 1 ? "Gain over time: attenuation from the top, boost from upward compression from the bottom." : "Gain reduction per side.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (14);
    addButton (learnBtn, h.removeFromLeft (110), "Listen for five seconds and set GAIN so the output lands near the target loudness. Not for compliance.",
               [this] (const ModifierKeys&) { mx.learnRequests.fetch_add (1); });
    learnBtn.setFontHeight (10.0f);
    h.removeFromLeft (8);
    addReadout (targetRo, pid (mx.pTargetLufs), "TARGET", h.removeFromLeft (190), "The loudness LEARN aims for.");
    targetRo.captionLeft = true;
    targetRo.fontH = 13.0f;

    const Rect viewR = kViewArea.withWidth (760);
    addAndMakeVisible (specView);
    specView.setBounds (viewR);
    specView.source = &spectrum;
    addAndMakeVisible (trace);
    trace.setBounds (viewR);
    trace.rangeDb = 18.0f;
    trace.lanes.push_back ({ colours::brand, true, {} });
    trace.lanes.push_back ({ colours::band (0), false, {} });
    addAndMakeVisible (grMeterL);
    addAndMakeVisible (grMeterR);
    grMeterL.setBounds (viewR.getX() + 300, viewR.getY() + 20, 40, 300);
    grMeterR.setBounds (viewR.getX() + 360, viewR.getY() + 20, 40, 300);
    grMeterL.setRangeDb (-24.0f); grMeterR.setRangeDb (-24.0f);

    // Right of the view: gain and output faders.
    const int fx = viewR.getRight() + 20;
    addFader (gainFader, pid (mx.pGain), "GAIN", Rect (fx, kViewArea.getY(), 70, 330), "Push the signal into the limiter. Up means louder.");
    gainFader.fill = colours::brand;
    addFader (outputFader, pid (mx.pOutput), "OUTPUT LEVEL", Rect (fx + 90, kViewArea.getY(), 90, 330), "The ceiling. -1 dB with TRUE PEAK for streaming; -0.1 for a CD.");
    addToggle (linkBtn, pid (mx.pLink), Rect (fx + 190, kViewArea.getY() + 60, 60, 34), "Move GAIN and OUTPUT LEVEL in opposite directions, to judge the limiter without the louder-is-better bias.");
    linkBtn.setFontHeight (10.0f);
    addToggle (truePeakBtn, pid (mx.pTruePeak), Rect (fx + 190, kViewArea.getY() + 110, 110, 34), "Limit the peaks a converter will produce between samples, not just the samples.");
    truePeakBtn.setFontHeight (10.0f);
    addChoice (modeChoice, pid (mx.pMode), "MODE", Rect (fx + 190, kViewArea.getY() + 170, 120, 54), "LOW LATENCY and I to III: single-band limiters, from fastest to most careful. IV: three-band pre-limiting. V: four-band, the cleanest at high gain.");
    addChoice (styleChoice, pid (mx.pStyle3), "STYLE", Rect (fx + 190, kViewArea.getY() + 236, 120, 54), "How the release is held back: CLIPPING is the loudest and roughest, PUMPING the slowest.");

    auto c = kControlArea;
    addKnob (characterKnob, pid (mx.pCharacter), "CHARACTER", c.removeFromLeft (150).withHeight (190), "Speed of the limiter, from CLIPPING (0) to VERY SLOW (10).", false);
    c.removeFromLeft (10);
    addKnob (upwardKnob, pid (mx.pUpward), "UPWARD", c.removeFromLeft (110).withHeight (150).translated (0, 20), "Upward compression: the most that quiet passages are raised. Peaks near full scale stay where they are.");
    c.removeFromLeft (20);
    auto clip = c.removeFromLeft (230);
    addToggle (clipBtn, pid (mx.pClipOn), clip.removeFromTop (34).withWidth (100), "Soft clipping before the limiter, 4x oversampled.");
    clipBtn.setFontHeight (10.0f);
    clip.removeFromTop (6);
    auto cr = clip.removeFromTop (150);
    addKnob (clipKnob, pid (mx.pClipAmount), "AMOUNT", cr.removeFromLeft (110).withHeight (130), "Wet/dry of the clipping. At 100% the signal clips fully at 0 dBFS.");
    addChoice (clipModeChoice, pid (mx.pClipMode), {}, cr.removeFromLeft (110).withHeight (30).translated (0, 40), "LIGHT starts at -3 dBFS, MODERATE at -9, HEAVY at -30.");
    clipModeChoice.fontH = 12.0f;
    c.removeFromLeft (10);
    auto tr = c.removeFromLeft (120);
    addToggle (transBtn, pid (mx.pTransOn), tr.removeFromTop (34), "Sharpen transients before limiting so drums survive it.");
    transBtn.setFontHeight (9.0f);
    tr.removeFromTop (6);
    addKnob (transKnob, pid (mx.pTransAmount), "AMOUNT", tr.removeFromTop (130), "How much emphasis.");
    c.removeFromLeft (10);
    auto st = c.removeFromLeft (250);
    addToggle (stereoLinkBtn, pid (mx.pStereoLink), st.removeFromTop (34).withWidth (60), "Link the two sliders.");
    stereoLinkBtn.setFontHeight (10.0f);
    st.removeFromTop (6);
    auto sr = st.removeFromTop (150);
    addKnob (stereoTransKnob, pid (mx.pStereoTrans), "TRANSIENT", sr.removeFromLeft (110).withHeight (130), "How independently the two sides are limited on transients. 0 keeps the image; 100 is louder but narrower.");
    addKnob (stereoSustainKnob, pid (mx.pStereoSustain), "SUSTAIN", sr.removeFromLeft (110).withHeight (130), "The same for sustained material.");
    linkSliders ({ &gainFader, &outputFader }, pid (mx.pLink), true);
}

void MaximizerPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 3; ++i) viewBtn[i].setLit (view == i);
    specView.setVisible (view == 0); trace.setVisible (view == 1);
    grMeterL.setVisible (view == 2); grMeterR.setVisible (view == 2);
    learnBtn.setLit (mx.learning());
    learnBtn.setLabel (mx.learning() ? "LEARNING " + String ((int) (mx.learnProgress() * 100)) + "%" : "LEARN GAIN");
    const int mode = (int) pv (mx.pMode);
    styleChoice.setVisible (mode == Maximizer::Irc3 || mode == Maximizer::Irc4 || mode == Maximizer::Irc5);
    for (auto& ch : choices)
        if (ch.first == &styleChoice) ch.second = pid (mode == Maximizer::Irc4 ? mx.pStyle4 : mx.pStyle3);
    styleChoice.onOpen = [this] { openChoiceMenu (pid ((int) pv (mx.pMode) == Maximizer::Irc4 ? mx.pStyle4 : mx.pStyle3), &styleChoice); };
    styleChoice.getText = [this] { if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (pid ((int) pv (mx.pMode) == Maximizer::Irc4 ? mx.pStyle4 : mx.pStyle3)))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    clipKnob.setEnabled (pv (mx.pClipOn) > 0.5f); clipModeChoice.setEnabled (pv (mx.pClipOn) > 0.5f);
    transKnob.setEnabled (pv (mx.pTransOn) > 0.5f);
    stereoSustainKnob.setEnabled (pv (mx.pStereoLink) < 0.5f);
    if (view == 1) { const float lanes[2] { mx.grDb, mx.upwardDb }; pushTrace (trace, proc.chain.outputTap, lanes); }
    grMeterL.push (dbToGain (-mx.grLDb));
    grMeterR.push (dbToGain (-mx.grRDb));
    const int lat = mx.latency();
    statusLine = String (Maximizer::characterText (pv (mx.pCharacter))) + " character. Reduction now " + String (std::abs (mx.grDb), 1) + " dB"
               + (pv (mx.pUpward) > 0.0f ? ", upward boost " + String (mx.upwardDb, 1) + " dB" : "")
               + (pv (mx.pClipOn) > 0.5f ? ", clip rounding " + String (mx.clipDb, 1) + " dB" : "")
               + ". Output true peak " + String (mx.outTruePeakDb, 1) + " dBTP. Latency " + String (lat * 1000.0 / jmax (1.0, proc.getSampleRate()), 1) + " ms.";
    repaint();
}

void MaximizerPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea.getX() + 284, kControlArea.getY() - 8, 232, 200).toFloat(), "SOFT CLIP");
    drawSectionLocal (g, Rect (kControlArea.getX() + 524, kControlArea.getY() - 8, 122, 200).toFloat(), "TRANSIENT");
    drawSectionLocal (g, Rect (kControlArea.getX() + 654, kControlArea.getY() - 8, 252, 200).toFloat(), "STEREO INDEPENDENCE");
    if (view == 2)
    {
        const auto r = grMeterL.getBounds().toFloat();
        drawText (g, "L", { r.getX(), r.getBottom() + 4.0f, r.getWidth(), 14.0f }, 11.0f, colours::ink, true);
        drawText (g, "R", { r.getX() + 60.0f, r.getBottom() + 4.0f, r.getWidth(), 14.0f }, 11.0f, colours::ink, true);
        drawText (g, "GAIN REDUCTION", { r.getX() - 100.0f, r.getBottom() + 24.0f, 300.0f, 14.0f }, 11.0f, colours::inkDim, true);
        drawText (g, "-" + String (mx.grDb, 1) + " dB", { r.getX() - 100.0f, r.getBottom() + 42.0f, 300.0f, 22.0f }, 20.0f, colours::lcd, true);
    }
}

// ===========================================================================
// VintageCompressorPanel
// ===========================================================================
VintageCompressorPanel::VintageCompressorPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModVintageCompressor, s), vc (p.chain.vintageCompressor)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, vc.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (100), i == 0 ? "The filter in the feedback loop, in front of the detector." : "Gain reduction over time.", [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    for (int i = 0; i < 2; ++i)
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; bindSet(); });
    addAndMakeVisible (detView);
    detView.setBounds (kViewArea);
    detView.source = &spectrum;
    addChoice (detTypeChoice, pid (vc.pDetType), "DETECTION FILTER", Rect (kViewArea.getX() + 14, kViewArea.getY() + 12, 150, 54), "NONE; a HIGHPASS so the bass does not pump it; a HIGH SHELF boost; or a WIDE BOOST in the highs so they drive it.");
    addReadout (detFreqRo, pid (vc.pDetFreq), "FREQ", Rect (kViewArea.getX() + 174, kViewArea.getY() + 12, 100, 54), "Filter frequency.");
    addReadout (detGainRo, pid (vc.pDetGain), "BOOST", Rect (kViewArea.getX() + 284, kViewArea.getY() + 12, 100, 54), "Boost of the shelf or the wide peak.");
    addButton (detSoloBtn, Rect (kViewArea.getX() + 394, kViewArea.getY() + 12, 30, 30), "Hear the detector's input.", [this] (const ModifierKeys&) { vc.detSolo.store (! vc.detSolo.load()); });
    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.lanes.push_back ({ colours::band (1), true, {} });

    auto c = kControlArea;
    addChoice (modeChoice, pid (vc.pMode), "MODE", c.removeFromLeft (170).withHeight (54), "SHARP keeps transients crisp; BALANCED judges by the signal; SMOOTH thickens and fills.");
    c.removeFromLeft (20);
    addFader (threshFader, pid (vc.set[0].threshold), "THRESHOLD", c.removeFromLeft (60), "Where compression starts. The knee below it depends on the mode.");
    addAndMakeVisible (threshMeter);
    threshMeter.setBounds (c.removeFromLeft (40).withTrimmedBottom (16).reduced (4, 8));
    threshMeter.colour = colours::band (1);
    c.removeFromLeft (20);
    addKnob (ratioKnob, pid (vc.set[0].ratio), "RATIO", c.removeFromLeft (120).withHeight (150), "How much is taken off above the threshold.");
    addKnob (attackKnob, pid (vc.set[0].attack), "ATTACK", c.removeFromLeft (120).withHeight (150), "How fast it grabs.");
    addKnob (releaseKnob, pid (vc.set[0].release), "RELEASE", c.removeFromLeft (120).withHeight (150), "Release for transients; sustained compression lets go a good deal slower.");
    addKnob (gainKnob, pid (vc.set[0].gain), "GAIN", c.removeFromLeft (120).withHeight (150), "Make-up gain.");
    gainKnob.bipolar = true;
    c.removeFromLeft (20);
    addToggle (autoGainBtn, pid (vc.pAutoGain), c.removeFromLeft (110).withHeight (40).translated (0, 50), "Match the output level to the input over time.");
    autoGainBtn.setFontHeight (10.0f);
    bindSet();
}

void VintageCompressorPanel::bindSet()
{
    const auto& st = vc.set[viewSet];
    rebind (threshFader, pid (st.threshold)); rebind (ratioKnob, pid (st.ratio)); rebind (attackKnob, pid (st.attack)); rebind (releaseKnob, pid (st.release)); rebind (gainKnob, pid (st.gain));
}

void VintageCompressorPanel::refresh()
{
    ModulePanel::refresh();
    const bool twoSets = (int) pv (vc.pChMode) != Stereo;
    for (int i = 0; i < 2; ++i) { setBtn[i].setVisible (twoSets); setBtn[i].setLit (twoSets && viewSet == i); setBtn[i].setLabel (i == 0 ? "MID" : "SIDE"); }
    if (! twoSets && viewSet != 0) { viewSet = 0; bindSet(); }
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    detView.setVisible (view == 0); trace.setVisible (view == 1);
    showAll (view == 0, detTypeChoice, detFreqRo, detGainRo, detSoloBtn);
    const int dt = (int) pv (vc.pDetType);
    detFreqRo.setEnabled (dt != 0); detGainRo.setEnabled (dt >= 2);
    detSoloBtn.setLit (vc.detSolo.load());
    const int ch = twoSets ? viewSet : 0;
    threshMeter.inputDb = vc.ch[ch].inDb; threshMeter.gainDb = vc.ch[ch].grDb; threshMeter.repaint();
    if (view == 1) { const float lane[1] { vc.ch[ch].grDb }; pushTrace (trace, proc.chain.inputTap, lane); }
    statusLine = "Reduction now " + String (-vc.ch[ch].grDb, 1) + " dB.";
}

void VintageCompressorPanel::paint (Graphics& g) { ModulePanel::paint (g); }

// ===========================================================================
// VintageEqPanel
// ===========================================================================
void VintageEqPanel::CurveView::paintOverlay (Graphics& g)
{
    drawCurve (g, [this] (float hz) { float o; panel.veq.curveDb (panel.viewSet, &hz, &o, 1); return o; }, colours::ink, 2.2f, true);
}

VintageEqPanel::VintageEqPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModVintageEq, s), veq (p.chain.vintageEq), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, veq.pChMode);
    auto h = headerArea().withTrimmedLeft (70 + 126);
    for (int i = 0; i < 2; ++i)
    {
        addButton (setBtn[i], h.removeFromLeft (34), "Show this channel's controls.", [this, i] (const ModifierKeys&) { viewSet = i; bindSet(); });
        addToggle (setOnBtn[i], pid (veq.pChanOn[i]), h.removeFromLeft (30), "Switch this channel on or off.");
        setOnBtn[i].setFontHeight (10.0f);
        h.removeFromLeft (8);
    }
    addAndMakeVisible (view);
    view.setBounds (kViewArea.withHeight (250));
    view.source = &spectrum;

    const int y1 = kViewArea.getY() + 268, y2 = y1 + 176;
    int x = 14;
    // Top row: the program equaliser.
    addChoice (lowFreq, pid (veq.set[0].lowFreq), "LOW FREQ", Rect (x, y1 + 40, 110, 54), "Where the low shelf sits."); x += 120;
    addKnob (lowBoost, pid (veq.set[0].lowBoost), "LOW BOOST", Rect (x, y1, 110, 150), "Boost below the low frequency."); x += 110;
    addKnob (lowCut, pid (veq.set[0].lowCut), "LOW CUT", Rect (x, y1, 110, 150), "Cut a little above it. Boost and cut together give a bump then a dip, the famous trick."); x += 130;
    addChoice (hiBoostFreq, pid (veq.set[0].hiBoostFreq), "HIGH BOOST FREQ", Rect (x, y1 + 40, 120, 54), "Centre of the high boost."); x += 130;
    addKnob (hiBoost, pid (veq.set[0].hiBoost), "HIGH BOOST", Rect (x, y1, 110, 150), "Boost at the chosen frequency."); x += 110;
    addKnob (hiQ, pid (veq.set[0].hiQ), "BANDWIDTH", Rect (x, y1, 110, 150), "Sharp to broad; it tightens a little as you boost."); x += 130;
    addChoice (hiCutFreq, pid (veq.set[0].hiCutFreq), "HIGH CUT FREQ", Rect (x, y1 + 40, 120, 54), "Where the high cut shelf starts."); x += 130;
    addKnob (hiCut, pid (veq.set[0].hiCut), "HIGH CUT", Rect (x, y1, 110, 150), "Cut above it.");
    // Bottom row: the mid equaliser.
    x = 14;
    addChoice (lmFreq, pid (veq.set[0].lmFreq), "LOW-MID FREQ", Rect (x, y2 + 40, 110, 54), "Centre of the low-mid boost."); x += 120;
    addKnob (lmBoost, pid (veq.set[0].lmBoost), "LM BOOST", Rect (x, y2, 110, 150), "Boost; wider as it goes up."); x += 150;
    addChoice (midFreq, pid (veq.set[0].midFreq), "MID FREQ", Rect (x, y2 + 40, 110, 54), "Centre of the mid cut."); x += 120;
    addKnob (midCut, pid (veq.set[0].midCut), "MID CUT", Rect (x, y2, 110, 150), "Cut."); x += 150;
    addChoice (hmFreq, pid (veq.set[0].hmFreq), "HIGH-MID FREQ", Rect (x, y2 + 40, 110, 54), "Centre of the high-mid boost."); x += 120;
    addKnob (hmBoost, pid (veq.set[0].hmBoost), "HM BOOST", Rect (x, y2, 110, 150), "Boost.");
    bindSet();
}

void VintageEqPanel::bindSet()
{
    const auto& st = veq.set[viewSet];
    rebind (lowBoost, pid (st.lowBoost)); rebind (lowCut, pid (st.lowCut)); rebind (hiBoost, pid (st.hiBoost)); rebind (hiQ, pid (st.hiQ));
    rebind (hiCut, pid (st.hiCut)); rebind (lmBoost, pid (st.lmBoost)); rebind (midCut, pid (st.midCut)); rebind (hmBoost, pid (st.hmBoost));
    struct Pair { ChoiceDisplay* c; int idx; };
    const Pair pairs[] { { &lowFreq, st.lowFreq }, { &hiBoostFreq, st.hiBoostFreq }, { &hiCutFreq, st.hiCutFreq }, { &lmFreq, st.lmFreq }, { &midFreq, st.midFreq }, { &hmFreq, st.hmFreq } };
    for (const auto& pr : pairs)
    {
        for (auto& c : choices) if (c.first == pr.c) c.second = pid (pr.idx);
        ChoiceDisplay* cd = pr.c;
        const int idx = pr.idx;
        cd->onOpen = [this, idx, cd] { openChoiceMenu (pid (idx), cd); };
        cd->getText = [this, idx] { if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (pid (idx)))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    }
}

void VintageEqPanel::refresh()
{
    ModulePanel::refresh();
    const int chMode = (int) pv (veq.pChMode);
    const bool twoSets = chMode != Stereo;
    for (int i = 0; i < 2; ++i)
    {
        setBtn[i].setVisible (twoSets); setOnBtn[i].setVisible (twoSets);
        setBtn[i].setLit (twoSets && viewSet == i);
        setBtn[i].setLabel (chMode == MidSide ? (i == 0 ? "MID" : "SIDE") : (i == 0 ? "L" : "R"));
    }
    if (! twoSets && viewSet != 0) { viewSet = 0; bindSet(); }
    view.repaint();
}

void VintageEqPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (8, kViewArea.getY() + 258, 1088, 166).toFloat(), "PROGRAM EQUALISER");
    drawSectionLocal (g, Rect (8, kViewArea.getY() + 434, 1088, 166).toFloat(), "MID EQUALISER");
}

// ===========================================================================
// VintageLimiterPanel
// ===========================================================================
VintageLimiterPanel::VintageLimiterPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModVintageLimiter, s), vl (p.chain.vintageLimiter)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, -1);
    auto h = headerArea().withTrimmedLeft (70);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (100), i == 0 ? "The output spectrum." : "Gain reduction over time.", [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    addAndMakeVisible (specView);
    specView.setBounds (kViewArea);
    specView.source = &spectrum;
    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.lanes.push_back ({ colours::band (2), true, {} });
    trace.lanes.push_back ({ colours::brand, true, {} });
    auto c = kControlArea;
    addChoice (modeChoice, pid (vl.pMode), "MODE", c.removeFromLeft (170).withHeight (54), "ANALOG: fast, thick, tight bass. TUBE: softer, varies with the signal. MODERN: the vintage character with a modern brickwall.");
    c.removeFromLeft (20);
    addFader (threshFader, pid (vl.pThreshold), "THRESHOLD", c.removeFromLeft (70), "Lower it for more limiting and more level: make-up brings the threshold up to the ceiling.");
    addAndMakeVisible (threshMeter);
    threshMeter.setBounds (c.removeFromLeft (40).withTrimmedBottom (16).reduced (4, 8));
    threshMeter.colour = colours::band (2);
    addFader (ceilingFader, pid (vl.pCeiling), "CEILING", c.removeFromLeft (70), "The most the output can reach. -0.3 dB when dithering; -0.6 to -0.8 for lossy codecs.");
    c.removeFromLeft (20);
    auto col = c.removeFromLeft (120);
    addToggle (linkBtn, pid (vl.pLink), col.removeFromTop (40), "Move THRESHOLD and CEILING together.");
    linkBtn.setFontHeight (10.0f);
    col.removeFromTop (10);
    addToggle (truePeakBtn, pid (vl.pTruePeak), col.removeFromTop (40), "Limit the peaks a converter will produce between samples.");
    truePeakBtn.setFontHeight (10.0f);
    c.removeFromLeft (30);
    addKnob (characterKnob, pid (vl.pCharacter), "CHARACTER", c.removeFromLeft (180).withHeight (200), "Attack and release, FAST (0) to SLOW (10).", false);
    linkSliders ({ &threshFader, &ceilingFader }, pid (vl.pLink));
}

void VintageLimiterPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    specView.setVisible (view == 0); trace.setVisible (view == 1);
    threshMeter.inputDb = vl.inDb; threshMeter.gainDb = -vl.grDb; threshMeter.repaint();
    if (view == 1) { const float lanes[2] { vl.grDb, vl.brickDb }; pushTrace (trace, proc.chain.inputTap, lanes); }
    statusLine = "Program limiting " + String (vl.grDb, 1) + " dB, brickwall " + String (vl.brickDb, 1) + " dB now.";
}

void VintageLimiterPanel::paint (Graphics& g) { ModulePanel::paint (g); }

// ===========================================================================
// VintageTapePanel
// ===========================================================================
void VintageTapePanel::CurveView::paintOverlay (Graphics& g)
{
    drawCurve (g, [this] (float hz) { float o; panel.tape.curveDb (&hz, &o, 1); return o; }, colours::ink, 2.0f, true);
}

VintageTapePanel::VintageTapePanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModVintageTape, s), tape (p.chain.vintageTape), view (*this)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, -1);
    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    auto c = kControlArea.withTrimmedTop (10);
    auto col = c.removeFromLeft (120);
    for (int i = 0; i < 3; ++i)
    {
        addButton (speedBtn[i], col.removeFromTop (40), "Tape speed: slower has a higher, bigger head bump and loses more top end.", [this, i] (const ModifierKeys&) { setp (tape.pSpeed, (float) i); });
        speedBtn[i].setFontHeight (11.0f);
        col.removeFromTop (8);
    }
    c.removeFromLeft (30);
    addKnob (driveKnob, pid (tape.pDrive), "INPUT DRIVE", c.removeFromLeft (170).withHeight (200), "How hard the tape is hit. The level is compensated so only the colour changes.", false);
    driveKnob.bipolar = true;
    addKnob (biasKnob, pid (tape.pBias), "BIAS", c.removeFromLeft (150).withHeight (170).translated (0, 15), "Negative: the highs distort more. Positive: the saturation starts earlier, squeezing the dynamics.");
    biasKnob.bipolar = true;
    addKnob (harmonicsKnob, pid (tape.pHarmonics), "HARMONICS", c.removeFromLeft (150).withHeight (170).translated (0, 15), "Even-order colour from the electronics.");
    addKnob (lowEmphKnob, pid (tape.pLowEmph), "LOW EMPHASIS", c.removeFromLeft (150).withHeight (170).translated (0, 15), "The head bump: low removes it, high pushes it up to +10 dB.");
    addKnob (highEmphKnob, pid (tape.pHighEmph), "HIGH EMPHASIS", c.removeFromLeft (150).withHeight (170).translated (0, 15), "4 is close to flat; lower rolls the top off, higher adds shimmer.");
}

void VintageTapePanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 3; ++i) speedBtn[i].setLit ((int) pv (tape.pSpeed) == i);
    view.repaint();
    statusLine = "The curve is the tape's frequency response; saturation rounding now " + String (tape.satDb, 1) + " dB.";
}

void VintageTapePanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea.getX() - 6, kControlArea.getY() + 2, 130, 150).toFloat(), "SPEED");
}

// ===========================================================================
// BassControlPanel
// ===========================================================================
BassControlPanel::BassControlPanel (BurnishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModBassControl, s), bc (p.chain.bassControl)
{
    setSize (kPanelW, kPanelH);
    addStandardHeader (true, true, -1);
    auto h = headerArea().withTrimmedLeft (70);
    addButton (soloBtn, h.removeFromLeft (90), "Hear only what is below the cutoff.", [this] (const ModifierKeys&) { bc.bassSolo.store (! bc.bassSolo.load()); });
    soloBtn.setFontHeight (10.0f);
    h.removeFromLeft (10);
    addReadout (cutoffRo, pid (bc.pCutoff), "CUTOFF", h.removeFromLeft (170), "Everything below this is the bass this module works on.");
    cutoffRo.captionLeft = true; cutoffRo.fontH = 13.0f;
    h.removeFromLeft (10);
    addReadout (zoomRo, pid (bc.pZoom), "ZOOM", h.removeFromLeft (150), "Magnify the oscilloscope for quiet signals.");
    zoomRo.captionLeft = true; zoomRo.fontH = 13.0f;

    // Left: balance and punch; middle: oscilloscope; below: sustain and peak.
    const Rect left (14, 56, 300, 372);
    addToggle (balanceOnBtn, pid (bc.pBalanceOn), Rect (left.getX() + 6, left.getY() + 10, 40, 24), "Balance on or off.");
    balanceOnBtn.setFontHeight (9.0f);
    addAndMakeVisible (balanceMeter);
    balanceMeter.setBounds (left.getX() + 56, left.getY() + 8, 230, 34);
    balanceMeter.lo = -12.0f; balanceMeter.hi = 12.0f; balanceMeter.targetLo = -2.0f; balanceMeter.targetHi = 4.0f;
    balanceMeter.leftLabel = "LACKING BASS"; balanceMeter.rightLabel = "TOO MUCH";
    addKnob (balanceKnob, pid (bc.pBalance), "BALANCE", Rect (left.getX() + 90, left.getY() + 46, 130, 140), "Level of everything below the cutoff.");
    balanceKnob.bipolar = true;
    addToggle (punchOnBtn, pid (bc.pPunchOn), Rect (left.getX() + 6, left.getY() + 196, 40, 24), "Punch on or off.");
    punchOnBtn.setFontHeight (9.0f);
    addAndMakeVisible (punchMeter);
    punchMeter.setBounds (left.getX() + 56, left.getY() + 194, 230, 34);
    punchMeter.lo = 0.0f; punchMeter.hi = 24.0f; punchMeter.targetLo = 8.0f; punchMeter.targetHi = 15.0f;
    punchMeter.leftLabel = "SQUASHED"; punchMeter.rightLabel = "TOO DYNAMIC";
    addKnob (punchKnob, pid (bc.pPunch), "PUNCH", Rect (left.getX() + 90, left.getY() + 232, 130, 140), "Gain on the transients of the low band: more kick, without changing the envelope the way a compressor would.");
    punchKnob.bipolar = true;

    addAndMakeVisible (scope);
    scope.setBounds (Rect (330, 56, 760, 372));
    scope.ring = bc.scope; scope.ringSize = BassControl::kScopeSize; scope.writePos = &bc.scopeWrite;
    scope.onDragSolid = [this] (float db) { setp (bc.pPeakThresh, jlimit (-40.0f, 0.0f, db)); };
    scope.onDragDashed = [this] (float db) { setp (bc.pSustainThresh, jlimit (-60.0f, 0.0f, db)); };

    const Rect sus (14, 440, 530, 232), pk (560, 440, 530, 232);
    addToggle (sustainOnBtn, pid (bc.pSustainOn), Rect (sus.getX() + 10, sus.getY() + 8, 44, 26), "Sustain power on or off.");
    sustainOnBtn.setFontHeight (9.0f);
    addKnob (sustainThreshKnob, pid (bc.pSustainThresh), "THRESHOLD", Rect (sus.getX() + 70, sus.getY() + 30, 130, 150), "Below this the bass is raised. The dashed line in the scope: put it between the kick's peaks and the sustained bass.");
    addKnob (sustainCharKnob, pid (bc.pSustainChar), "CHARACTER", Rect (sus.getX() + 210, sus.getY() + 30, 130, 150), "DIRTY (left) adds harmonics small speakers can hear; CLEAN (right) is transparent.");
    addKnob (sustainAmountKnob, pid (bc.pSustainAmount), "AMOUNT", Rect (sus.getX() + 350, sus.getY() + 30, 130, 150), "How much the sustained bass is filled out.");
    addToggle (peakOnBtn, pid (bc.pPeakOn), Rect (pk.getX() + 10, pk.getY() + 8, 44, 26), "Peak control on or off.");
    peakOnBtn.setFontHeight (9.0f);
    addKnob (peakThreshKnob, pid (bc.pPeakThresh), "THRESHOLD", Rect (pk.getX() + 70, pk.getY() + 30, 130, 150), "Bass peaks stop here. The solid line in the scope: put it at the kick's peak level.");
    addKnob (peakCharKnob, pid (bc.pPeakChar), "CHARACTER", Rect (pk.getX() + 210, pk.getY() + 30, 130, 150), "DIRTY (left) clips, with harmonics; CLEAN (right) limits transparently.");
}

void BassControlPanel::refresh()
{
    ModulePanel::refresh();
    soloBtn.setLit (bc.bassSolo.load());
    balanceMeter.set (bc.balanceDb);
    punchMeter.set (bc.crestDb);
    scope.zoomDb = pv (bc.pZoom);
    scope.solidLineDb = pv (bc.pPeakOn) > 0.5f ? pv (bc.pPeakThresh) : -1000.0f;
    scope.dashedLineDb = pv (bc.pSustainOn) > 0.5f ? pv (bc.pSustainThresh) : -1000.0f;
    scope.repaint();
    balanceKnob.setEnabled (pv (bc.pBalanceOn) > 0.5f); punchKnob.setEnabled (pv (bc.pPunchOn) > 0.5f);
    const bool so = pv (bc.pSustainOn) > 0.5f, po = pv (bc.pPeakOn) > 0.5f;
    sustainThreshKnob.setEnabled (so); sustainCharKnob.setEnabled (so); sustainAmountKnob.setEnabled (so);
    peakThreshKnob.setEnabled (po); peakCharKnob.setEnabled (po);
    statusLine = "Below " + ids::hertz (pv (bc.pCutoff)) + ": balance " + ids::decibels (bc.balanceDb) + " against the rest, crest " + String (bc.crestDb, 1) + " dB"
               + (so ? ", sustain boost " + String (bc.sustainDb, 1) + " dB" : "") + (po ? ", peak control " + String (bc.peakGrDb, 1) + " dB" : "") + ".";
    repaint();
}

void BassControlPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (14, 56, 300, 372).toFloat(), "BALANCE AND PUNCH");
    drawSectionLocal (g, Rect (14, 440, 530, 200).toFloat(), "SUSTAIN POWER");
    drawSectionLocal (g, Rect (560, 440, 530, 200).toFloat(), "PEAK CONTROL");
    // Gain readouts.
    const auto lcd1 = Rect (14 + 490, 440 + 150, 40, 30).toFloat();
    ignoreUnused (lcd1);
    drawText (g, "+" + String (bc.sustainDb, 1) + " dB", { 14.0f + 352.0f, 440.0f + 182.0f, 130.0f, 16.0f }, 12.0f, colours::lcd, true);
    drawText (g, "-" + String (bc.peakGrDb, 1) + " dB", { 560.0f + 352.0f, 440.0f + 60.0f, 130.0f, 16.0f }, 12.0f, colours::lcd, true);
    drawText (g, "DIRTY", { 14.0f + 210.0f, 440.0f + 182.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    drawText (g, "CLEAN", { 14.0f + 300.0f, 440.0f + 182.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
    drawText (g, "DIRTY", { 560.0f + 210.0f, 440.0f + 182.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    drawText (g, "CLEAN", { 560.0f + 300.0f, 440.0f + 182.0f, 40.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
}

// ===========================================================================
// Factory
// ===========================================================================
std::unique_ptr<ModulePanel> makeModulePanel (int id, BurnishProcessor& p, SpectrumSource& s, SpectrumSource&)
{
    switch (id)
    {
        case ModEq1: case ModEq2:  return std::make_unique<EqPanel> (p, id, s);
        case ModDynamicEq:         return std::make_unique<DynamicEqPanel> (p, s);
        case ModDynamics:          return std::make_unique<DynamicsPanel> (p, s);
        case ModExciter:           return std::make_unique<ExciterPanel> (p, s);
        case ModImager:            return std::make_unique<ImagerPanel> (p, s);
        case ModImpact:            return std::make_unique<ImpactPanel> (p, s);
        case ModLowEndFocus:       return std::make_unique<LowEndFocusPanel> (p, s);
        case ModClarity:           return std::make_unique<ClarityPanel> (p, s);
        case ModStabilizer:        return std::make_unique<StabilizerPanel> (p, s);
        case ModSpectralShaper:    return std::make_unique<ShaperPanel> (p, s);
        case ModMatchEq:           return std::make_unique<MatchEqPanel> (p, s);
        case ModMaximizer:         return std::make_unique<MaximizerPanel> (p, s);
        case ModVintageCompressor: return std::make_unique<VintageCompressorPanel> (p, s);
        case ModVintageEq:         return std::make_unique<VintageEqPanel> (p, s);
        case ModVintageLimiter:    return std::make_unique<VintageLimiterPanel> (p, s);
        case ModVintageTape:       return std::make_unique<VintageTapePanel> (p, s);
        case ModBassControl:       return std::make_unique<BassControlPanel> (p, s);
        default:                   return nullptr;
    }
}
} // namespace burnish::ui
