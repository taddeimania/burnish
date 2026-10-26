#include "Views.h"

namespace burnish::ui
{
using namespace juce;

// ===========================================================================
// ModulePanel
// ===========================================================================
ModulePanel::ModulePanel (BurnishProcessor& p, int moduleId, SpectrumSource& s) : proc (p), id (moduleId), spectrum (s)
{
    setInterceptsMouseClicks (false, true);
}

ModulePanel::~ModulePanel()
{
    jassert (attachments.empty());   // releaseAttachments() must run before the subclass's sliders go
}

void ModulePanel::releaseAttachments()
{
    attachments.clear();
    sliders.clear();
    for (auto& [s, v] : lastValues)
        s->onValueChange = nullptr;
    lastValues.clear();
}

void ModulePanel::paint (Graphics& g)
{
    if (statusLine.isNotEmpty())
    {
        g.setColour (colours::inkDim);
        g.setFont (panelFont (12.0f, false));
        g.drawFittedText (statusLine, getLocalBounds().withTrimmedTop (getHeight() - 24).withTrimmedLeft (16).withTrimmedRight (16).withHeight (18),
                          Justification::centredLeft, 1, 1.0f);
    }
}

void ModulePanel::drawSectionLocal (Graphics& g, Rectangle<float> r, const String& title) const
{
    drawSection (g, r, title);
}

void ModulePanel::bindSlider (Slider& s, const String& parameterId, Rectangle<int> r, const String& tip)
{
    s.setTooltip (tip);
    addAndMakeVisible (s);
    s.setBounds (r);
    rebind (s, parameterId);
}

void ModulePanel::rebind (Slider& s, const String& parameterId)
{
    for (size_t i = 0; i < sliders.size(); ++i)
        if (sliders[i].first == &s)
        {
            if (sliders[i].second == parameterId) return;
            attachments[i].reset();
            sliders[i].second = parameterId;
            if (auto* param = proc.param (parameterId))
            {
                s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
                if (auto* k = dynamic_cast<Knob*> (&s)) k->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
                if (auto* f = dynamic_cast<Fader*> (&s)) f->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
                if (auto* o = dynamic_cast<Readout*> (&s)) o->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
            }
            attachments[i] = std::make_unique<SliderAttachment> (proc.apvts, parameterId, s);
            return;
        }
    if (auto* param = proc.param (parameterId))
    {
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        if (auto* k = dynamic_cast<Knob*> (&s)) k->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
        if (auto* f = dynamic_cast<Fader*> (&s)) f->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
        if (auto* o = dynamic_cast<Readout*> (&s)) o->format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    }
    sliders.push_back ({ &s, parameterId });
    attachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, parameterId, s));
}

void ModulePanel::addKnob (Knob& k, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip, bool small)
{
    k.caption = caption;
    k.small = small;
    bindSlider (k, parameterId, r, tip);
}

void ModulePanel::addFader (Fader& f, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    f.caption = caption;
    bindSlider (f, parameterId, r, tip);
}

void ModulePanel::addReadout (Readout& o, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    o.caption = caption;
    bindSlider (o, parameterId, r, tip);
}

void ModulePanel::addToggle (PanelButton& b, const String& parameterId, Rectangle<int> r, const String& tip)
{
    addButton (b, r, tip, [this, parameterId] (const ModifierKeys&)
    {
        proc.setParam (parameterId, proc.getParam (parameterId) > 0.5f ? 0.0f : 1.0f);
    });
    toggles.push_back ({ &b, parameterId });
}

void ModulePanel::addButton (PanelButton& b, Rectangle<int> r, const String& tip, std::function<void (const ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    b.setBounds (r);
}

void ModulePanel::openChoiceMenu (const String& parameterId, Component* target)
{
    auto* param = dynamic_cast<AudioParameterChoice*> (proc.param (parameterId));
    if (param == nullptr) return;
    PopupMenu m;
    const int cur = param->getIndex();
    for (int i = 0; i < param->choices.size(); ++i)
        m.addItem (param->choices[i], true, i == cur, [this, parameterId, i] { proc.setParam (parameterId, (float) i); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void ModulePanel::addChoice (ChoiceDisplay& c, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    c.caption = caption;
    c.setTooltip (tip);
    c.getText = [this, parameterId]
    {
        if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (parameterId)))
            return p->getCurrentChoiceName().toUpperCase();
        return String();
    };
    c.onOpen = [this, parameterId, &c] { openChoiceMenu (parameterId, &c); };
    addAndMakeVisible (c);
    c.setBounds (r);
    choices.push_back ({ &c, parameterId });
}

void ModulePanel::addStandardHeader (bool delta, bool reset, int chModeParam)
{
    auto h = headerArea();
    if (delta)
    {
        addButton (deltaBtn, h.removeFromLeft (64), "Hear only what this module changes: the difference between its output and its input.",
                   [this] (const ModifierKeys&) { proc.deltaModule.store (proc.deltaModule.load() == id ? -1 : id); });
        deltaBtn.setFontHeight (11.0f);
        h.removeFromLeft (6);
    }
    if (chModeParam >= 0)
    {
        addChoice (chModeChoice, pid (chModeParam), {}, h.removeFromLeft (120), "Channel processing: STEREO uses one set of controls for both sides; "
                   "MID/SIDE and LEFT/RIGHT give each its own set (switch with A and B).");
        chModeChoice.fontH = 13.0f;
        h.removeFromLeft (6);
    }
    if (reset)
    {
        addButton (resetBtn, h.removeFromRight (60), "Every control in this module back to its default.",
                   [this] (const ModifierKeys&) { proc.resetModule (id); });
        resetBtn.setFontHeight (11.0f);
        h.removeFromRight (6);
    }
    addButton (presetBtn, h.removeFromRight (74), "Presets for this module alone.", [this] (const ModifierKeys&) { openModulePresets(); });
    presetBtn.setFontHeight (11.0f);
}

void ModulePanel::openModulePresets()
{
    PopupMenu m;
    m.addSectionHeader ("FACTORY");
    for (const auto& p : factoryModulePresets())
        if (p.module == id || (id == ModEq2 && p.module == ModEq1))
        {
            const ModulePreset* pp = &p;
            m.addItem (p.name, [this, pp]
            {
                if (id == ModEq2 && pp->module == ModEq1)
                {
                    ModulePreset copy = *pp;
                    std::vector<std::string> ids2;
                    for (auto& [pid_, v] : copy.values) ids2.push_back (String (pid_).replace ("eq1.", "eq2.").toStdString());
                    for (size_t i = 0; i < copy.values.size(); ++i) copy.values[i].first = ids2[i].c_str();
                    proc.loadModulePreset (id, copy);
                }
                else
                    proc.loadModulePreset (id, *pp);
            });
        }
    m.addSectionHeader ("YOURS");
    const auto users = BurnishProcessor::userModulePresets (id);
    if (users.isEmpty())
        m.addItem ("(none yet: save one below)", false, false, [] {});
    for (const auto& f : users)
        m.addItem (f.getFileNameWithoutExtension(), [this, f] { proc.loadUserModulePreset (id, f); });
    m.addSeparator();
    m.addItem ("Save module preset...", [this]
    {
        auto* w = new AlertWindow ("Save module preset", "Name it.", MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&getLookAndFeel());
        w->addTextEditor ("name", Chain::moduleShortName (id));
        w->addButton ("SAVE", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("CANCEL", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                String err;
                if (! proc.saveUserModulePreset (id, w->getTextEditorContents ("name"), err))
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save", err);
            }
        }), true);
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn));
}

void ModulePanel::linkSliders (std::vector<Slider*> group, const String& linkPid, bool inverse)
{
    for (auto* s : group)
    {
        lastValues[s] = s->getValue();
        s->onValueChange = [this, group, linkPid, s, inverse]
        {
            const double v = s->getValue();
            const double delta = v - lastValues[s];
            lastValues[s] = v;
            if (! s->isMouseButtonDown() || proc.getParam (linkPid) < 0.5f || delta == 0.0) return;
            for (auto* o : group)
                if (o != s)
                {
                    const double nv = jlimit (o->getMinimum(), o->getMaximum(), o->getValue() + (inverse ? -delta : delta));
                    lastValues[o] = nv;
                    o->setValue (nv, sendNotificationSync);
                }
        };
    }
}

void ModulePanel::refresh()
{
    for (auto& [s, v] : lastValues)
        if (! s->isMouseButtonDown()) v = s->getValue();
    for (auto& [b, parameterId] : toggles)
        b->setLit (proc.getParam (parameterId) > 0.5f);
    for (auto& [c, parameterId] : choices)
        c->refresh();
    deltaBtn.setLit (proc.deltaModule.load() == id);
}

// ===========================================================================
// CrossoverView
// ===========================================================================
CrossoverView::CrossoverView (ModulePanel& p, MultibandParams& m, std::atomic<int>& s) : panel (p), mb (m), solo (s)
{
    showLabels = true;
}

int CrossoverView::numBands() const { return (int) panel.pv (mb.pNumBands) + 1; }
float CrossoverView::xoverHz (int i) const { return panel.pv (mb.pXover[i]); }

void CrossoverView::setXover (int i, float hz)
{
    const int nb = numBands();
    const float lo = i > 0 ? xoverHz (i - 1) * 1.12f : 22.0f;
    const float hi = i < nb - 2 ? xoverHz (i + 1) / 1.12f : 19000.0f;
    panel.setp (mb.pXover[i], jlimit (lo, jmax (lo, hi), hz));
}

int CrossoverView::handleAt (Point<float> p) const
{
    for (int i = 0; i < numBands() - 1; ++i)
        if (std::abs (xForHz (xoverHz (i)) - p.x) < 7.0f) return i;
    return -1;
}

int CrossoverView::bandAt (float x) const
{
    const int nb = numBands();
    for (int i = 0; i < nb - 1; ++i)
        if (x < xForHz (xoverHz (i))) return i;
    return nb - 1;
}

Rectangle<float> CrossoverView::powerRect (int b) const
{
    const auto r = plotArea();
    const int nb = numBands();
    const float x0 = b == 0 ? r.getX() : xForHz (xoverHz (b - 1));
    const float x1 = b == nb - 1 ? r.getRight() : xForHz (xoverHz (b));
    return { (x0 + x1) * 0.5f - 24.0f, r.getY() + 6.0f, 20.0f, 16.0f };
}
Rectangle<float> CrossoverView::soloRect (int b) const { return powerRect (b).translated (24.0f, 0.0f); }
Rectangle<float> CrossoverView::removeRect (int b) const { return powerRect (b).translated (48.0f, 0.0f); }
Rectangle<float> CrossoverView::addRect() const
{
    return { hover.x - 10.0f, plotArea().getBottom() - 40.0f, 20.0f, 20.0f };
}

void CrossoverView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
    {
        const float x0 = b == 0 ? r.getX() : xForHz (xoverHz (b - 1));
        const float x1 = b == nb - 1 ? r.getRight() : xForHz (xoverHz (b));
        const Colour c = colours::band (b);
        const bool on = panel.pv (mb.pBandOn[b]) > 0.5f;
        const bool sel = b == selectedBand;
        g.setColour (c.withAlpha (sel ? 0.16f : 0.06f));
        g.fillRect (Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        if (bandLineDb)
        {
            const float db = bandLineDb (b);
            if (std::isfinite (db))
            {
                const float y = yForGain (db);
                g.setColour (c);
                g.drawLine (x0 + 2.0f, y, x1 - 2.0f, y, 2.0f);
            }
        }
        // Band buttons: power, solo, remove.
        auto pr = powerRect (b), sr = soloRect (b), xr = removeRect (b);
        g.setColour (on ? c : colours::socket);
        g.fillRoundedRectangle (pr, 3.0f);
        drawText (g, "ON", pr, 9.0f, on ? colours::black : colours::inkDim, true);
        const bool soloed = solo.load() == b;
        g.setColour (soloed ? colours::ledYellow : colours::black);
        g.fillRoundedRectangle (sr, 3.0f);
        g.setColour (colours::inkDim);
        g.drawRoundedRectangle (sr, 3.0f, 1.0f);
        drawText (g, "S", sr, 10.0f, soloed ? colours::black : colours::ink, true);
        if (nb > 1)
        {
            g.setColour (colours::black);
            g.fillRoundedRectangle (xr, 3.0f);
            g.setColour (colours::inkDim);
            g.drawRoundedRectangle (xr, 3.0f, 1.0f);
            drawText (g, "x", xr, 11.0f, colours::ink, true);
        }
        drawText (g, String (b + 1), { x0 + 4.0f, r.getBottom() - 30.0f, 20.0f, 14.0f }, 11.0f, c, true, Justification::centredLeft);
    }
    for (int i = 0; i < nb - 1; ++i)
    {
        const float x = xForHz (xoverHz (i));
        g.setColour (colours::ink.withAlpha (dragging == i ? 1.0f : 0.7f));
        g.drawLine (x, r.getY(), x, r.getBottom(), dragging == i ? 2.0f : 1.2f);
        const auto h = Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getBottom() - 46.0f });
        g.setColour (colours::cream);
        g.fillEllipse (h);
        drawText (g, ids::hertz (xoverHz (i)), { x - 40.0f, r.getBottom() - 70.0f, 80.0f, 14.0f }, 10.0f, colours::ink, true);
    }
    if (hovering && nb < 4 && handleAt (hover) < 0)
    {
        const auto a = addRect();
        g.setColour (colours::cream);
        g.fillEllipse (a);
        drawText (g, "+", a, 14.0f, colours::black, true);
    }
}

void CrossoverView::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
    {
        if (powerRect (b).contains (p)) { panel.setp (mb.pBandOn[b], panel.pv (mb.pBandOn[b]) > 0.5f ? 0.0f : 1.0f); return; }
        if (soloRect (b).contains (p)) { solo.store (solo.load() == b ? -1 : b); repaint(); return; }
        if (nb > 1 && removeRect (b).contains (p))
        {
            // Remove band b: drop the crossover between it and its neighbour.
            const int drop = b == nb - 1 ? b - 1 : b;
            for (int i = drop; i < nb - 2; ++i) panel.setp (mb.pXover[i], xoverHz (i + 1));
            panel.setp (mb.pNumBands, (float) (nb - 2));
            if (solo.load() >= nb - 1) solo.store (-1);
            selectedBand = jmin (selectedBand, nb - 2);
            if (onSelect) onSelect (selectedBand);
            return;
        }
    }
    dragging = handleAt (p);
    if (dragging >= 0) return;
    if (hovering && nb < 4 && addRect().contains (p))
    {
        // Insert a crossover at the hover frequency.
        const float hz = hzForX (p.x);
        float f[3];
        for (int i = 0; i < nb - 1; ++i) f[i] = xoverHz (i);
        int at = 0;
        while (at < nb - 1 && f[at] < hz) ++at;
        for (int i = nb - 1; i > at; --i) f[i] = f[i - 1];
        f[at] = hz;
        for (int i = 0; i < nb; ++i) panel.setp (mb.pXover[i], f[i]);
        panel.setp (mb.pNumBands, (float) nb);
        panel.setp (mb.pBandOn[nb], 1.0f);
        return;
    }
    selectedBand = bandAt (p.x);
    if (onSelect) onSelect (selectedBand);
    repaint();
}

void CrossoverView::mouseDrag (const MouseEvent& e)
{
    if (dragging >= 0)
        setXover (dragging, hzForX (e.position.x));
}

void CrossoverView::mouseUp (const MouseEvent&) { dragging = -1; repaint(); }
void CrossoverView::mouseMove (const MouseEvent& e) { hover = e.position; hovering = true; repaint(); }
void CrossoverView::mouseExit (const MouseEvent&) { hovering = false; repaint(); }

void CrossoverView::mouseDoubleClick (const MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    auto* w = new AlertWindow ("Crossover " + String (h + 1), "Frequency in Hz.", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&getLookAndFeel());
    w->addTextEditor ("hz", String (roundToInt (xoverHz (h))));
    w->addButton ("SET", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("CANCEL", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w, h] (int result)
    {
        if (result == 1) setXover (h, w->getTextEditorContents ("hz").getFloatValue());
    }), true);
}

void CrossoverView::refresh() { repaint(); }

// ===========================================================================
// EqCurveView
// ===========================================================================
EqCurveView::EqCurveView (ModulePanel& p) : panel (p)
{
    showGainScale = true;
    setWantsKeyboardFocus (true);
}

Point<float> EqCurveView::nodePos (const Band& b) const
{
    return { xForHz (b.hz), b.hasGain ? yForGain (b.gainDb) : yForGain (0.0f) };
}

int EqCurveView::nodeAt (Point<float> p) const
{
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < numBands; ++i)
    {
        const Band b = getBand (i);
        if (! b.on) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < 12.0f && d < bd) { bd = d; best = i; }
    }
    return best;
}

void EqCurveView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    if (compositeNoAmountDb)
        drawCurve (g, compositeNoAmountDb, colours::inkDim.withAlpha (0.6f), 1.2f, false);
    if (selected >= 0 && bandDb && drawSelectedFill)
    {
        const Band b = getBand (selected);
        if (b.on)
            drawCurve (g, [this] (float hz) { return bandDb (selected, hz); }, b.colour.withAlpha (0.9f), 1.5f, true);
    }
    if (compositeDb)
        drawCurve (g, compositeDb, colours::ink, 2.2f, false);
    if (dynamicDb)
        drawCurve (g, dynamicDb, colours::lcd, 1.6f, false);

    for (int i = 0; i < numBands; ++i)
    {
        const Band b = getBand (i);
        if (! b.on) continue;
        const auto p = nodePos (b);
        const bool sel = i == selected;
        g.setColour (b.colour);
        g.fillEllipse (Rectangle<float> (sel ? 14.0f : 10.0f, sel ? 14.0f : 10.0f).withCentre (p));
        g.setColour (colours::black);
        g.drawEllipse (Rectangle<float> (sel ? 14.0f : 10.0f, sel ? 14.0f : 10.0f).withCentre (p), 1.5f);
        drawText (g, String (i + 1), Rectangle<float> (16.0f, 12.0f).withCentre (p), 9.0f, colours::black, true);
        if (sel && b.hasQ)
        {
            // Q handles left and right of the node.
            const float w = jlimit (14.0f, 120.0f, 60.0f / jmax (0.2f, b.q));
            g.setColour (b.colour.withAlpha (0.8f));
            g.drawLine (p.x - w, p.y, p.x - 10.0f, p.y, 1.0f);
            g.drawLine (p.x + 10.0f, p.y, p.x + w, p.y, 1.0f);
            g.fillRect (Rectangle<float> (4.0f, 12.0f).withCentre ({ p.x - w, p.y }));
            g.fillRect (Rectangle<float> (4.0f, 12.0f).withCentre ({ p.x + w, p.y }));
        }
    }
    if (hovering && nodeAt (hover) < 0 && dragging < 0 && onAdd)
    {
        bool free = false;
        for (int i = 0; i < numBands; ++i) if (! getBand (i).on) free = true;
        if (free)
        {
            const float y = compositeDb ? yForGain (compositeDb (hzForX (hover.x))) : yForGain (0.0f);
            const auto a = Rectangle<float> (18.0f, 18.0f).withCentre ({ hover.x, y - 16.0f });
            g.setColour (colours::cream);
            g.fillEllipse (a);
            drawText (g, "+", a, 13.0f, colours::black, true);
        }
    }
    ignoreUnused (r);
}

void EqCurveView::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    if (selected >= 0 && getBand (selected).on && getBand (selected).hasQ)
    {
        const Band b = getBand (selected);
        const auto np = nodePos (b);
        const float w = jlimit (14.0f, 120.0f, 60.0f / jmax (0.2f, b.q));
        if (std::abs (p.y - np.y) < 10.0f)
        {
            if (std::abs (p.x - (np.x - w)) < 8.0f) { dragging = selected; dragQ = -1; dragStart = p; startQ = b.q; return; }
            if (std::abs (p.x - (np.x + w)) < 8.0f) { dragging = selected; dragQ = 1; dragStart = p; startQ = b.q; return; }
        }
    }
    const int n = nodeAt (p);
    if (n >= 0)
    {
        selected = n;
        if (onSelect) onSelect (n);
        if (e.mods.isAltDown() && onAltSolo) { altSoloing = true; onAltSolo (n, true); }
        dragging = n; dragQ = 0; dragStart = p;
        repaint();
        return;
    }
    // The + button: add a band.
    if (hovering && onAdd)
    {
        const float y = compositeDb ? yForGain (compositeDb (hzForX (hover.x))) : yForGain (0.0f);
        const auto a = Rectangle<float> (18.0f, 18.0f).withCentre ({ hover.x, y - 16.0f });
        if (a.contains (p))
        {
            for (int i = 0; i < numBands; ++i)
                if (! getBand (i).on) { onAdd (i, hzForX (p.x)); selected = i; if (onSelect) onSelect (i); break; }
            return;
        }
    }
    if (e.mods.isAltDown() && onAltSolo) { altSoloing = true; onAltSolo (-1, true); }
}

void EqCurveView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const Band b = getBand (dragging);
    if (dragQ != 0)
    {
        const float dx = (e.position.x - dragStart.x) * (float) dragQ;
        const float q = jlimit (0.1f, 40.0f, startQ * std::pow (2.0f, -dx / 40.0f));
        if (setBandQ) setBandQ (dragging, q);
        return;
    }
    float hz = hzForX (e.position.x), gain = gainForY (e.position.y);
    if (e.mods.isShiftDown())
    {
        if (std::abs (e.getDistanceFromDragStartX()) > std::abs (e.getDistanceFromDragStartY())) gain = b.gainDb; else hz = b.hz;
    }
    if (! b.hasGain) gain = b.gainDb;
    if (setBandFreqGain) setBandFreqGain (dragging, jlimit (20.0f, 20000.0f, hz), jlimit (gainBottom, gainTop, gain));
}

void EqCurveView::mouseUp (const MouseEvent&)
{
    dragging = -1; dragQ = 0;
    if (altSoloing && onAltSolo) { altSoloing = false; onAltSolo (-1, false); }
    repaint();
}

void EqCurveView::mouseMove (const MouseEvent& e) { hover = e.position; hovering = true; repaint(); }
void EqCurveView::mouseExit (const MouseEvent&) { hovering = false; repaint(); }

void EqCurveView::mouseDoubleClick (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n >= 0) { if (onReset) onReset (n); return; }
    if (onAdd)
        for (int i = 0; i < numBands; ++i)
            if (! getBand (i).on) { onAdd (i, hzForX (e.position.x)); selected = i; if (onSelect) onSelect (i); break; }
}

bool EqCurveView::keyPressed (const KeyPress& k)
{
    if (selected < 0 || ! getBand (selected).on) return false;
    const Band b = getBand (selected);
    const float coarse = k.getModifiers().isShiftDown() ? 4.0f : k.getModifiers().isCommandDown() ? 0.25f : 1.0f;
    if (k == KeyPress::upKey)    { if (setBandFreqGain) setBandFreqGain (selected, b.hz, jlimit (gainBottom, gainTop, b.gainDb + 0.5f * coarse)); return true; }
    if (k == KeyPress::downKey)  { if (setBandFreqGain) setBandFreqGain (selected, b.hz, jlimit (gainBottom, gainTop, b.gainDb - 0.5f * coarse)); return true; }
    if (k == KeyPress::leftKey)  { if (setBandFreqGain) setBandFreqGain (selected, jlimit (20.0f, 20000.0f, b.hz / std::pow (2.0f, coarse / 24.0f)), b.gainDb); return true; }
    if (k == KeyPress::rightKey) { if (setBandFreqGain) setBandFreqGain (selected, jlimit (20.0f, 20000.0f, b.hz * std::pow (2.0f, coarse / 24.0f)), b.gainDb); return true; }
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey) { if (onRemove) onRemove (selected); return true; }
    return false;
}

// ===========================================================================
// TraceView
// ===========================================================================
void TraceView::reset()
{
    wave.fill (0.0f);
    for (auto& l : lanes) l.values.fill (0.0f);
}

void TraceView::push (float wavePeak, const float* laneValues)
{
    wave[(size_t) writePos] = wavePeak;
    for (size_t i = 0; i < lanes.size(); ++i)
        lanes[i].values[(size_t) writePos] = laneValues[i];
    writePos = (writePos + 1) % kHistory;
    repaint();
}

void TraceView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (1.0f);
    const float colW = plot.getWidth() / (float) kHistory;
    // Waveform (mirrored peak), dim.
    g.setColour (colours::spectrum.withAlpha (0.55f));
    for (int i = 0; i < kHistory; ++i)
    {
        const float v = wave[(size_t) ((writePos + i) % kHistory)];
        const float h = jlimit (0.0f, 1.0f, v) * plot.getHeight() * 0.48f;
        const float x = plot.getX() + (float) i * colW;
        g.fillRect (Rectangle<float> (x, plot.getCentreY() - h, jmax (1.0f, colW), 2.0f * h));
    }
    // Lanes.
    for (size_t li = 0; li < lanes.size(); ++li)
    {
        const auto& lane = lanes[li];
        Path p;
        for (int i = 0; i < kHistory; ++i)
        {
            const float v = jlimit (0.0f, rangeDb, std::abs (lane.values[(size_t) ((writePos + i) % kHistory)]));
            const float x = plot.getX() + (float) i * colW;
            const float y = lane.fromTop ? plot.getY() + v / rangeDb * plot.getHeight() * 0.5f
                                         : plot.getBottom() - v / rangeDb * plot.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const bool dim = dimAllBut >= 0 && (int) li != dimAllBut;
        g.setColour (lane.colour.withAlpha (dim ? 0.3f : 1.0f));
        g.strokePath (p, PathStrokeType (dim ? 1.0f : 1.6f));
    }
    // Scale.
    for (float db = 0.0f; db <= rangeDb; db += rangeDb / 4.0f)
    {
        const float y = plot.getY() + db / rangeDb * plot.getHeight() * 0.5f;
        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        drawText (g, String ((int) -db), { plot.getRight() - 30.0f, y - 6.0f, 28.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
    }
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// DifferenceView
// ===========================================================================
DifferenceView::DifferenceView (ModulePanel& p) : panel (p)
{
    showGainScale = true;
    gainTop = 12.0f; gainBottom = -12.0f;
}

void DifferenceView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    if (targetDb)
        drawCurve (g, targetDb, colours::inkDim.withAlpha (0.7f), 1.2f, false);
    if (specDb != nullptr)
    {
        // The module's own smoothed spectrum, relative: drawn faintly around the centre.
        Path p;
        float mean = 0.0f; int cnt = 0;
        for (int b = 0; b < TonalTargets::kNumBands; ++b) if (specDb[b] > -100.0f) { mean += specDb[b]; ++cnt; }
        mean = cnt > 0 ? mean / (float) cnt : 0.0f;
        for (int b = 0; b < TonalTargets::kNumBands; ++b)
        {
            const float x = xForHz (TonalTargets::centreHz (b));
            const float y = jlimit (r.getY(), r.getBottom(), yForGain ((specDb[b] - mean) * 0.5f));
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colours::spectrum.withAlpha (0.5f));
        g.strokePath (p, PathStrokeType (1.0f));
    }
    if (gainDb != nullptr)
    {
        Path p, f;
        for (int b = 0; b < TonalTargets::kNumBands; ++b)
        {
            const float x = xForHz (TonalTargets::centreHz (b));
            const float y = jlimit (r.getY(), r.getBottom(), yForGain (gainDb[b]));
            if (b == 0) { p.startNewSubPath (x, y); f.startNewSubPath (x, yForGain (0.0f)); f.lineTo (x, y); }
            else { p.lineTo (x, y); f.lineTo (x, y); }
        }
        f.lineTo (xForHz (20000.0f), yForGain (0.0f));
        f.closeSubPath();
        g.setColour (colour.withAlpha (0.2f));
        g.fillPath (f);
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved));
    }
    if (lowParam >= 0 && highParam >= 0)
    {
        const float lo = panel.pv (lowParam), hi = panel.pv (highParam);
        const float x0 = xForHz (lo), x1 = xForHz (hi);
        g.setColour (colours::black.withAlpha (0.55f));
        g.fillRect (Rectangle<float> (r.getX(), r.getY(), x0 - r.getX(), r.getHeight()));
        g.fillRect (Rectangle<float> (x1, r.getY(), r.getRight() - x1, r.getHeight()));
        g.setColour (colours::ink);
        g.drawLine (x0, r.getY(), x0, r.getBottom(), 1.5f);
        g.drawLine (x1, r.getY(), x1, r.getBottom(), 1.5f);
        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? x0 : x1;
            const auto h = Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getBottom() - 30.0f });
            g.setColour (colours::cream);
            g.fillEllipse (h);
            drawText (g, ids::hertz (i == 0 ? lo : hi), { x - 40.0f, r.getBottom() - 54.0f, 80.0f, 14.0f }, 10.0f, colours::ink, true);
        }
    }
}

void DifferenceView::mouseDown (const MouseEvent& e)
{
    if (lowParam < 0) return;
    const float x0 = xForHz (panel.pv (lowParam)), x1 = xForHz (panel.pv (highParam));
    if (std::abs (e.position.x - x0) < 8.0f) dragging = 0;
    else if (std::abs (e.position.x - x1) < 8.0f) dragging = 1;
    else if (e.position.x > x0 && e.position.x < x1) { dragging = 2; dragOffsetOct = std::log2 (hzForX (e.position.x) / panel.pv (lowParam)); }
    else dragging = -1;
}

void DifferenceView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const float hz = jlimit (regionMinHz, regionMaxHz, hzForX (e.position.x));
    const float lo = panel.pv (lowParam), hi = panel.pv (highParam);
    if (dragging == 0) panel.setp (lowParam, jmin (hz, hi / 1.1f));
    else if (dragging == 1) panel.setp (highParam, jmax (hz, lo * 1.1f));
    else
    {
        const float span = std::log2 (hi / lo);
        float newLo = hz / std::pow (2.0f, dragOffsetOct);
        newLo = jlimit (regionMinHz, regionMaxHz / std::pow (2.0f, span), newLo);
        panel.setp (lowParam, newLo);
        panel.setp (highParam, newLo * std::pow (2.0f, span));
    }
}

// ===========================================================================
// Vectorscope
// ===========================================================================
void Vectorscope::refresh()
{
    if (tap == nullptr) return;
    const int count = 2048;
    if ((int) l.size() < count) { l.resize (count); r.resize (count); }
    tap->latest (l.data(), r.data(), count);
    const int side = jmin (getWidth() - 40, getHeight() - 24);
    if (side < 10) return;
    if (! image.isValid() || image.getWidth() != side)
        image = Image (Image::ARGB, side, side, true);
    // Fade the history.
    image.multiplyAllAlphas (mode == Lissajous ? 0.6f : 0.85f);
    Graphics g (image);
    const float c = side * 0.5f, rad = c - 4.0f;
    if (mode == PolarLevel)
    {
        for (auto& v : rays) v *= 0.8f;
        for (int i = 0; i < count; ++i)
        {
            const float x = l[(size_t) i], y = r[(size_t) i];
            const float amp = std::sqrt (x * x + y * y);
            if (amp < 1e-4f) continue;
            // Angle: mono = up (0), full left = -45°, full right = +45°, out of phase beyond.
            const float ang = std::atan2 (y - x, x + y);   // -pi..pi
            const int ri = jlimit (0, 63, (int) ((ang + MathConstants<float>::pi) / MathConstants<float>::twoPi * 64.0f));
            rays[(size_t) ri] = jmax (rays[(size_t) ri], amp);
        }
        g.setColour (colours::lcd.withAlpha (0.9f));
        for (int i = 0; i < 64; ++i)
        {
            const float ang = (float) i / 64.0f * MathConstants<float>::twoPi - MathConstants<float>::pi;
            const float len = jlimit (0.0f, 1.0f, rays[(size_t) i]) * rad;
            g.drawLine (c, c, c + std::sin (ang) * len, c - std::cos (ang) * len, 2.0f);
        }
    }
    else
    {
        g.setColour (colours::lcd.withAlpha (0.5f));
        for (int i = 0; i < count; i += 2)
        {
            const float x = l[(size_t) i], y = r[(size_t) i];
            if (std::abs (x) > 1.0f || std::abs (y) > 1.0f) clipped = true;
            float px, py;
            if (mode == Lissajous) { px = c + (y - x) * 0.7071f * rad; py = c - (x + y) * 0.7071f * rad; }
            else
            {
                const float amp = jmin (1.0f, std::sqrt (x * x + y * y));
                const float ang = std::atan2 (y - x, x + y);
                px = c + std::sin (ang) * amp * rad; py = c - std::cos (ang) * amp * rad;
            }
            g.fillRect (Rectangle<float> (px - 1.0f, py - 1.0f, 2.0f, 2.0f));
        }
    }
    repaint();
}

void Vectorscope::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const int side = jmin (getWidth() - 40, getHeight() - 24);
    const auto sq = Rectangle<float> ((float) side, (float) side).withPosition (4.0f, 4.0f);
    const float c = side * 0.5f, rad = c - 4.0f;
    g.setColour (colours::grid);
    g.drawEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (sq.getCentre()), 1.0f);
    // 45-degree safe lines and the centre line.
    const auto ctr = sq.getCentre();
    g.drawLine (ctr.x, ctr.y, ctr.x - rad * 0.7071f, ctr.y - rad * 0.7071f, 1.0f);
    g.drawLine (ctr.x, ctr.y, ctr.x + rad * 0.7071f, ctr.y - rad * 0.7071f, 1.0f);
    g.drawLine (ctr.x, ctr.y, ctr.x, ctr.y - rad, 1.0f);
    drawText (g, "L", { sq.getX(), sq.getY(), 14.0f, 12.0f }, 10.0f, colours::inkDim, true);
    drawText (g, "R", { sq.getRight() - 14.0f, sq.getY(), 14.0f, 12.0f }, 10.0f, colours::inkDim, true);
    if (image.isValid())
        g.drawImageAt (image, (int) sq.getX(), (int) sq.getY());
    if (clipped)
    {
        g.setColour (colours::ledRed);
        g.drawRect (sq, 2.0f);
    }
    // Correlation bar (right) and balance bar (bottom).
    const auto corr = Rectangle<float> (sq.getRight() + 10.0f, sq.getY(), 14.0f, sq.getHeight());
    drawLcdBox (g, corr);
    const float cy = corr.getBottom() - (correlation + 1.0f) * 0.5f * corr.getHeight();
    g.setColour (correlation < 0.0f ? colours::ledRed : colours::ledGreen);
    g.fillRect (Rectangle<float> (corr.getX() + 2.0f, cy - 2.0f, corr.getWidth() - 4.0f, 4.0f));
    drawText (g, "+1", { corr.getRight() + 2.0f, corr.getY() - 2.0f, 20.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    drawText (g, "-1", { corr.getRight() + 2.0f, corr.getBottom() - 10.0f, 20.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    const auto bal = Rectangle<float> (sq.getX(), sq.getBottom() + 6.0f, sq.getWidth(), 12.0f);
    drawLcdBox (g, bal);
    const float bx = bal.getX() + (balance + 1.0f) * 0.5f * bal.getWidth();
    g.setColour (colours::lcd);
    g.fillRect (Rectangle<float> (bx - 2.0f, bal.getY() + 2.0f, 4.0f, bal.getHeight() - 4.0f));
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// DynamicsCurveView
// ===========================================================================
void DynamicsCurveView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (4.0f);
    auto xFor = [&] (float db) { return plot.getX() + (db + rangeDb) / rangeDb * plot.getWidth(); };
    auto yFor = [&] (float db) { return plot.getBottom() - (db + rangeDb) / rangeDb * plot.getHeight(); };
    g.setColour (colours::grid);
    for (float db = -rangeDb; db <= 0.0f; db += rangeDb / 6.0f)
    {
        g.drawVerticalLine ((int) xFor (db), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
    }
    g.setColour (colours::grid.brighter (0.5f));
    g.drawLine (xFor (-rangeDb), yFor (-rangeDb), xFor (0.0f), yFor (0.0f), 1.0f);
    if (curve)
    {
        Path p, f;
        const int steps = 80;
        for (int i = 0; i <= steps; ++i)
        {
            const float in = -rangeDb + (float) i / (float) steps * rangeDb;
            const float out = jlimit (-rangeDb - 20.0f, 12.0f, in + curve (in));
            const float x = xFor (in), y = jlimit (plot.getY(), plot.getBottom(), yFor (out));
            if (i == 0) { p.startNewSubPath (x, y); f.startNewSubPath (x, plot.getBottom()); }
            f.lineTo (x, y);
            p.lineTo (x, y);
        }
        f.lineTo (plot.getRight(), plot.getBottom());
        f.closeSubPath();
        // Signal activity under the curve, up to the input level.
        if (inputDb > -rangeDb)
        {
            g.saveState();
            g.reduceClipRegion (Rectangle<int> ((int) plot.getX(), (int) plot.getY(), (int) (xFor (inputDb) - plot.getX()), (int) plot.getHeight()));
            g.setColour (colour.withAlpha (0.25f));
            g.fillPath (f);
            g.restoreState();
        }
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (2.0f));
        if (inputDb > -rangeDb)
        {
            const float x = xFor (inputDb), y = yFor (inputDb + gainDb);
            g.setColour (colours::ink);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));
        }
    }
    drawText (g, "IN", { plot.getRight() - 24.0f, plot.getBottom() - 14.0f, 22.0f, 12.0f }, 9.0f, colours::inkDim, false);
    drawText (g, "OUT", { plot.getX() + 2.0f, plot.getY(), 30.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// ThresholdMeter
// ===========================================================================
void ThresholdMeter::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto yFor = [&] (float db) { return r.getBottom() - jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * h; };
    // Outer: input level.
    g.setColour (colours::black);
    g.fillRect (r);
    g.setColour (colours::inkDim.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (r.getX(), yFor (inputDb), 4.0f, r.getBottom() - yFor (inputDb)));
    g.fillRect (Rectangle<float> (r.getRight() - 4.0f, yFor (inputDb), 4.0f, r.getBottom() - yFor (inputDb)));
    // Inner: gain change. Reduction from the top, boost from the bottom.
    g.setColour (colour);
    if (gainDb < 0.0f)
        g.fillRect (Rectangle<float> (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, jlimit (0.0f, h, -gainDb / -floorDb * h)));
    else if (gainDb > 0.0f)
        g.fillRect (Rectangle<float> (r.getX() + 6.0f, r.getBottom() - jlimit (0.0f, h, gainDb / -floorDb * h), r.getWidth() - 12.0f, jlimit (0.0f, h, gainDb / -floorDb * h)));
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// ScopeView
// ===========================================================================
float ScopeView::yForDb (float db) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float lin = std::pow (10.0f, (db + zoomDb) * 0.05f);
    return r.getCentreY() - jlimit (-1.0f, 1.0f, lin) * r.getHeight() * 0.5f;
}
float ScopeView::dbForY (float y) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float lin = jlimit (0.001f, 1.0f, (r.getCentreY() - y) / (r.getHeight() * 0.5f));
    return 20.0f * std::log10 (lin) - zoomDb;
}

void ScopeView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (2.0f);
    g.setColour (colours::grid);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    if (ring != nullptr && writePos != nullptr && ringSize > 0)
    {
        const int w = (int) plot.getWidth();
        const int count = jmin (ringSize, 4096);
        const int end = writePos->load();
        const float gain = std::pow (10.0f, zoomDb * 0.05f);
        Path p;
        for (int x = 0; x < w; ++x)
        {
            // Each column shows the min/max of its slice.
            const int i0 = x * count / w, i1 = (x + 1) * count / w;
            float mn = 1.0f, mx = -1.0f;
            for (int i = i0; i < jmax (i0 + 1, i1); ++i)
            {
                const float v = ring[(size_t) (((end - count + i) % ringSize + ringSize) % ringSize)] * gain;
                mn = jmin (mn, v); mx = jmax (mx, v);
            }
            const float y0 = plot.getCentreY() - jlimit (-1.0f, 1.0f, mx) * plot.getHeight() * 0.5f;
            const float y1 = plot.getCentreY() - jlimit (-1.0f, 1.0f, mn) * plot.getHeight() * 0.5f;
            g.setColour (colours::lcd.withAlpha (0.8f));
            g.fillRect (Rectangle<float> (plot.getX() + (float) x, y0, 1.0f, jmax (1.0f, y1 - y0)));
        }
    }
    if (solidLineDb > -500.0f)
    {
        const float y = yForDb (solidLineDb), y2 = plot.getCentreY() * 2.0f - y;
        g.setColour (colours::ink);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.drawHorizontalLine ((int) y2, plot.getX(), plot.getRight());
    }
    if (dashedLineDb > -500.0f)
    {
        const float y = yForDb (dashedLineDb), y2 = plot.getCentreY() * 2.0f - y;
        g.setColour (colours::ink.withAlpha (0.8f));
        const float dash[] { 5.0f, 4.0f };
        g.drawDashedLine (Line<float> (plot.getX(), y, plot.getRight(), y), dash, 2, 1.0f);
        g.drawDashedLine (Line<float> (plot.getX(), y2, plot.getRight(), y2), dash, 2, 1.0f);
    }
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

void ScopeView::mouseDown (const MouseEvent& e)
{
    dragging = 0;
    const float dSolid = solidLineDb > -500.0f ? jmin (std::abs (e.position.y - yForDb (solidLineDb)), std::abs (e.position.y - (getHeight() - yForDb (solidLineDb)))) : 1e9f;
    const float dDash = dashedLineDb > -500.0f ? jmin (std::abs (e.position.y - yForDb (dashedLineDb)), std::abs (e.position.y - (getHeight() - yForDb (dashedLineDb)))) : 1e9f;
    if (dSolid < 10.0f && dSolid <= dDash) dragging = 1;
    else if (dDash < 10.0f) dragging = 2;
}

void ScopeView::mouseDrag (const MouseEvent& e)
{
    const float y = e.position.y < getHeight() * 0.5f ? e.position.y : (float) getHeight() - e.position.y;
    const float db = jlimit (-60.0f, 0.0f, dbForY (y));
    if (dragging == 1 && onDragSolid) onDragSolid (db);
    if (dragging == 2 && onDragDashed) onDragDashed (db);
}

// ===========================================================================
// SweetSpotMeter
// ===========================================================================
void SweetSpotMeter::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    auto bar = r.reduced (2.0f);
    bar = bar.withHeight (14.0f).withCentre ({ bar.getCentreX(), r.getCentreY() });
    drawLcdBox (g, bar);
    auto xFor = [&] (float v) { return bar.getX() + (v - lo) / (hi - lo) * bar.getWidth(); };
    g.setColour (colours::ledGreen.withAlpha (0.35f));
    g.fillRect (Rectangle<float> (xFor (targetLo), bar.getY() + 2.0f, xFor (targetHi) - xFor (targetLo), bar.getHeight() - 4.0f));
    const bool inTarget = value >= targetLo && value <= targetHi;
    g.setColour (inTarget ? colours::ledGreen : colours::ledYellow);
    g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ xFor (value), bar.getCentreY() }));
    drawText (g, leftLabel, { r.getX(), bar.getBottom() + 1.0f, r.getWidth() * 0.5f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    drawText (g, rightLabel, { r.getCentreX(), bar.getBottom() + 1.0f, r.getWidth() * 0.5f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
}

// ===========================================================================
// BitMeterView
// ===========================================================================
void BitMeterView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const float cell = (r.getWidth() - 8.0f) / 24.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float y0 = r.getY() + 4.0f + (float) c * (r.getHeight() - 8.0f) * 0.5f;
        const float rowH = (r.getHeight() - 8.0f) * 0.5f;
        for (int b = 0; b < 24; ++b)
        {
            const int bit = 23 - b;   // MSB first
            const float x = r.getX() + 4.0f + (float) b * cell;
            const bool relevant = bit >= 24 - bitDepth;
            const float act = activity[c] != nullptr ? activity[c][bit] : 0.0f;
            const float hold_ = hold[c] != nullptr ? hold[c][bit] : 0.0f;
            // Hold on top, live below.
            g.setColour (hold_ > 0.5f ? (relevant ? colours::lcd : colours::ledRed).withAlpha (0.5f) : colours::socket);
            g.fillRect (Rectangle<float> (x + 1.0f, y0, cell - 2.0f, rowH * 0.4f - 2.0f));
            g.setColour (act > 0.05f ? (relevant ? colours::lcd : colours::ledRed).withAlpha (0.3f + 0.7f * act) : colours::socket);
            g.fillRect (Rectangle<float> (x + 1.0f, y0 + rowH * 0.4f, cell - 2.0f, rowH * 0.6f - 2.0f));
        }
    }
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}
} // namespace burnish::ui
