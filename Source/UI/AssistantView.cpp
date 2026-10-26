#include "AssistantView.h"
#include "../Settings.h"

namespace burnish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

namespace
{
const Rect kTargetBox   { 14, 24, 350, 690 };
const Rect kProcessBox  { 388, 24, 410, 690 };
const Rect kLoudBox     { 822, 24, 268, 690 };
const Rect kNavRow      { 14, 736, 1076, 50 };
constexpr int kRowH = 30;
}

AssistantView::AssistantView (BurnishProcessor& p, SpectrumSource& s) : proc (p), spectrum (s)
{
    setLookAndFeel (&lnf);
    setSize (1104, 804);

    // Target list and its buttons.
    addAndMakeVisible (targets);
    for (int i = 0; i < 4; ++i)
    {
        filterBtn[i].setFontHeight (9.0f);
        setToggle (filterBtn[i], {}, i == 0 ? "Every target." : i == 1 ? "The genre targets." : i == 2 ? "Targets made from your reference files." : "Your favourites.",
                   [this, i] (const ModifierKeys&) { targets.filter = i; targets.repaint(); });
    }
    addTargetBtn.setFontHeight (10.0f);
    setToggle (addTargetBtn, {}, "Make a target from an audio file: the assistant learns its loudest eight seconds.", [this] (const ModifierKeys&) { addReference(); });
    removeTargetBtn.setFontHeight (10.0f);
    setToggle (removeTargetBtn, {}, "Delete the selected custom target.", [this] (const ModifierKeys&)
    {
        proc.assistant.removeCustomTarget (proc.assistantSettings.targetIndex);
        proc.assistantSettings.targetIndex = jlimit (0, (int) proc.assistant.targets().size() - 1, proc.assistantSettings.targetIndex);
        targets.repaint();
    });

    // Processing.
    const char* moduleTips[7] {
        "Equalizer 1, set to move the tonal balance toward the target.",
        "Bass Control, for the balance and control of the low end.",
        "Clarity, to open up the spectrum.",
        "Stabilizer, aimed at the target's tonal balance.",
        "Impact, to match the target's microdynamics in four bands.",
        "Imager, to match the target's width in four bands.",
        "The Maximizer, which sets the loudness." };
    for (int i = 0; i < 7; ++i)
    {
        moduleBtn[i].setFontHeight (11.0f);
        setToggle (moduleBtn[i], {}, moduleTips[i], [this, i] (const ModifierKeys&)
        {
            auto& st = proc.assistantSettings;
            bool* flags[7] { &st.useEq, &st.useBass, &st.useClarity, &st.useStabilizer, &st.useImpact, &st.useImager, &st.useMaximizer };
            *flags[i] = ! *flags[i];
        });
    }
    enableAllBtn.setFontHeight (10.0f);
    setToggle (enableAllBtn, {}, "Every module on.", [this] (const ModifierKeys&) { auto& st = proc.assistantSettings; st.useEq = st.useBass = st.useClarity = st.useStabilizer = st.useImpact = st.useImager = st.useMaximizer = true; });
    disableAllBtn.setFontHeight (10.0f);
    setToggle (disableAllBtn, {}, "Every module off.", [this] (const ModifierKeys&) { auto& st = proc.assistantSettings; st.useEq = st.useBass = st.useClarity = st.useStabilizer = st.useImpact = st.useImager = st.useMaximizer = false; });
    tempoSyncBtn.setFontHeight (10.0f);
    setToggle (tempoSyncBtn, {}, "Count the analysis in bars of the host tempo instead of seconds.", [this] (const ModifierKeys&) { proc.assistantSettings.tempoSync = ! proc.assistantSettings.tempoSync; });
    analysisKnob.caption = "ANALYSIS";
    analysisKnob.setRange (8.0, 60.0, 1.0);
    analysisKnob.setValue (proc.assistantSettings.analysisSeconds, dontSendNotification);
    analysisKnob.format = [this] (double v) { return proc.assistantSettings.tempoSync ? String ((int) (v / 4)) + " bars" : String ((int) v) + " s"; };
    analysisKnob.onValueChange = [this]
    {
        if (proc.assistantSettings.tempoSync) proc.assistantSettings.analysisBars = jmax (2, (int) (analysisKnob.getValue() / 4));
        else proc.assistantSettings.analysisSeconds = (float) analysisKnob.getValue();
    };
    analysisKnob.setTooltip ("How long it listens, up to 60 seconds. Play the loudest, fullest part, such as a last chorus.");
    addAndMakeVisible (analysisKnob);
    for (int i = 0; i < 5; ++i)
    {
        intensityBtn[i].setFontHeight (10.0f);
        setToggle (intensityBtn[i], {}, "How strongly it processes. SUBTLE keeps the mix's character; TRANSFORMATIVE reshapes tone and dynamics. Loudness is separate.",
                   [this, i] (const ModifierKeys&) { proc.assistantSettings.intensity = i; });
    }

    // Loudness.
    loudnessKnob.caption = "LOUDNESS";
    loudnessKnob.setRange (-24.0, -6.0, 0.5);
    loudnessKnob.setValue (proc.assistantSettings.loudnessLufs, dontSendNotification);
    loudnessKnob.format = [] (double v) { return String (v, 1) + " LUFS"; };
    loudnessKnob.onValueChange = [this] { proc.assistantSettings.loudnessLufs = (float) loudnessKnob.getValue(); };
    loudnessKnob.setTooltip ("The loudness to aim for over the analysed section. Streaming services play at about -14; many releases sit louder.");
    addAndMakeVisible (loudnessKnob);
    for (int i = 0; i < 2; ++i)
    {
        destBtn[i].setFontHeight (10.0f);
        setToggle (destBtn[i], {}, i == 0 ? "A ceiling of -0.1 dB with true peak off: for a CD or a file nothing re-encodes." : "A ceiling of -1 dB with true peak on: for streaming and lossy codecs.",
                   [this, i] (const ModifierKeys&) { proc.assistantSettings.destination = i; });
        resultDestBtn[i].setFontHeight (10.0f);
        setToggle (resultDestBtn[i], {}, "Change the output level and true peak setting of the Maximizer.",
                   [this, i] (const ModifierKeys&)
                   {
                       proc.assistantSettings.destination = i;
                       auto& m = proc.chain.maximizer;
                       proc.setParam (proc.paramId (m, m.pOutput), i == 0 ? -0.1f : -1.0f);
                       proc.setParam (proc.paramId (m, m.pTruePeak), i == 0 ? 0.0f : 1.0f);
                   });
    }

    // Navigation.
    backBtn.setFontHeight (12.0f);
    setToggle (backBtn, {}, "Back to the modules.", [this] (const ModifierKeys&) { if (onClose) onClose(); });
    saveDefaultBtn.setFontHeight (10.0f);
    setToggle (saveDefaultBtn, {}, "When on, starting saves these settings as the default for next time.", [this] (const ModifierKeys&) { saveAsDefault = ! saveAsDefault; });
    oneClickBtn.setFontHeight (12.0f);
    setToggle (oneClickBtn, {}, "Start with the saved defaults, no questions asked.", [this] (const ModifierKeys&) { loadDefaults(); startRun(); });
    startBtn.setFontHeight (14.0f);
    setToggle (startBtn, {}, "Listen, then build the chain.", [this] (const ModifierKeys&) { startRun(); });

    // Result page.
    moduleViewBtn.setFontHeight (11.0f);
    setToggle (moduleViewBtn, {}, "Go to the modules the assistant built, to fine-tune them.", [this] (const ModifierKeys&) { if (onClose) onClose(); });
    relearnBtn.setFontHeight (11.0f);
    setToggle (relearnBtn, {}, "Listen again and rebuild for a new section.", [this] (const ModifierKeys&) { setPage (Setup); });
    gainMatchBtn.setFontHeight (11.0f);
    setToggle (gainMatchBtn, {}, "Match the output level to the input, so BYPASS compares fairly.", [this] (const ModifierKeys&) { proc.setParam (ids::gainMatch, proc.getParam (ids::gainMatch) > 0.5f ? 0.0f : 1.0f); });
    bypassBtn.setFontHeight (11.0f);
    setToggle (bypassBtn, {}, "Hear the unprocessed input.", [this] (const ModifierKeys&) { proc.setParam (ids::bypass, proc.getParam (ids::bypass) > 0.5f ? 0.0f : 1.0f); });

    auto bindAmount = [this] (Fader& f, const String& id, const String& tip)
    {
        f.setTooltip (tip);
        if (auto* param = proc.param (id)) f.format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
        addAndMakeVisible (f);
        attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, f));
    };
    auto& ch = proc.chain;
    bindAmount (eqAmount, proc.paramId (ch.eq1, ch.eq1.pAmount), "Scales every EQ band from 0% to 200%.");
    bindAmount (impactAmount, proc.paramId (ch.impact, ch.impact.pAmount), "Scales the Impact bands.");
    bindAmount (imagerAmount, proc.paramId (ch.imager, ch.imager.pAmount), "Scales the Imager bands.");
    bindAmount (clarityAmount, proc.paramId (ch.clarity, ch.clarity.pAmount), "Clarity amount.");
    bindAmount (stabilizerAmount, proc.paramId (ch.stabilizer, ch.stabilizer.pAmount), "Stabilizer amount.");
    maxOffset.setRange (-4.0, 4.0, 0.1);
    maxOffset.bipolar = true;
    maxOffset.format = [] (double v) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    maxOffset.setTooltip ("Trim the Maximizer gain the assistant chose, 4 dB either way.");
    maxOffset.onValueChange = [this]
    {
        auto& m = proc.chain.maximizer;
        proc.setParam (proc.paramId (m, m.pGain), jlimit (0.0f, 30.0f, proc.assistant.builtMaximizerGain() + (float) maxOffset.getValue()));
    };
    addAndMakeVisible (maxOffset);
    auto bindOn = [this] (PanelButton& b, Module& m, int onIdx, const String& tip)
    {
        b.setFontHeight (10.0f);
        const String id = proc.paramId (m, onIdx);
        setToggle (b, {}, tip, [this, id] (const ModifierKeys&) { proc.setParam (id, proc.getParam (id) > 0.5f ? 0.0f : 1.0f); });
    };
    bindOn (eqOnBtn, ch.eq1, ch.eq1.pOn, "Equalizer on or off, for a quick comparison.");
    bindOn (maxOnBtn, ch.maximizer, ch.maximizer.pOn, "Maximizer on or off.");
    bindOn (impactOnBtn, ch.impact, ch.impact.pOn, "Impact on or off.");
    bindOn (imagerOnBtn, ch.imager, ch.imager.pOn, "Imager on or off.");
    bindOn (clarityOnBtn, ch.clarity, ch.clarity.pOn, "Clarity on or off.");
    bindOn (stabilizerOnBtn, ch.stabilizer, ch.stabilizer.pOn, "Stabilizer on or off.");

    loadDefaults();
    setPage (proc.assistant.built() ? Result : Setup);
}

AssistantView::~AssistantView() { setLookAndFeel (nullptr); }

void AssistantView::setToggle (PanelButton& b, Rect r, const String& tip, std::function<void (const ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    if (! r.isEmpty()) b.setBounds (r);
}

void AssistantView::showSetup() { setPage (Setup); }
void AssistantView::showResult() { setPage (proc.assistant.analysing() ? Listening : proc.assistant.built() ? Result : Setup); }

void AssistantView::setPage (Page pg)
{
    currentPage = pg;
    const bool setup = pg == Setup, result = pg == Result;
    showAll (setup, enableAllBtn, disableAllBtn, tempoSyncBtn, analysisKnob, loudnessKnob, truePeakBtn, backBtn, saveDefaultBtn, startBtn, oneClickBtn);
    for (auto& b : moduleBtn) b.setVisible (setup);
    for (auto& b : intensityBtn) b.setVisible (setup);
    for (auto& b : destBtn) b.setVisible (setup);
    showAll (result, moduleViewBtn, relearnBtn, gainMatchBtn, bypassBtn, eqOnBtn, maxOnBtn, impactOnBtn, imagerOnBtn, clarityOnBtn, stabilizerOnBtn, eqAmount, maxOffset, impactAmount, imagerAmount, clarityAmount, stabilizerAmount);
    for (auto& b : resultDestBtn) b.setVisible (result);
    targets.setVisible (pg != Listening);
    for (auto& b : filterBtn) b.setVisible (pg != Listening);
    addTargetBtn.setVisible (pg != Listening);
    removeTargetBtn.setVisible (pg != Listening);
    resized();
    repaint();
}

void AssistantView::resized()
{
    const bool result = currentPage == Result;
    // Target list, both pages.
    const Rect tb = result ? Rect (14, 60, 300, 654) : kTargetBox;
    auto t = tb.reduced (10);
    auto fr = t.removeFromTop (26);
    for (int i = 0; i < 4; ++i) filterBtn[i].setBounds (fr.removeFromLeft (t.getWidth() / 4));
    t.removeFromTop (8);
    auto br = t.removeFromBottom (30);
    addTargetBtn.setBounds (br.removeFromLeft (t.getWidth() / 2 - 4));
    br.removeFromLeft (8);
    removeTargetBtn.setBounds (br);
    t.removeFromBottom (8);
    targets.setBounds (t);

    if (! result)
    {
        auto pbox = kProcessBox.reduced (16);
        pbox.removeFromTop (6);
        for (int i = 0; i < 7; ++i) { moduleBtn[i].setBounds (pbox.removeFromTop (34).withWidth (190)); pbox.removeFromTop (6); }
        auto ea = Rect (kProcessBox.getX() + 220, kProcessBox.getY() + 22, 170, 34);
        enableAllBtn.setBounds (ea);
        disableAllBtn.setBounds (ea.translated (0, 40));
        analysisKnob.setBounds (kProcessBox.getX() + 230, kProcessBox.getY() + 120, 150, 150);
        tempoSyncBtn.setBounds (kProcessBox.getX() + 240, kProcessBox.getY() + 276, 130, 30);
        pbox.removeFromTop (20);
        for (int i = 0; i < 5; ++i) { intensityBtn[i].setBounds (pbox.removeFromTop (34).withWidth (190)); pbox.removeFromTop (6); }
        auto lb = kLoudBox.reduced (16);
        lb.removeFromTop (6);
        loudnessKnob.setBounds (lb.removeFromTop (180).withSizeKeepingCentre (170, 180));
        lb.removeFromTop (30);
        for (int i = 0; i < 2; ++i) { destBtn[i].setBounds (lb.removeFromTop (34)); lb.removeFromTop (6); }
        auto nav = kNavRow;
        backBtn.setBounds (nav.removeFromLeft (100));
        nav.removeFromLeft (12);
        saveDefaultBtn.setBounds (nav.removeFromLeft (160));
        startBtn.setBounds (nav.removeFromRight (260));
        nav.removeFromRight (12);
        oneClickBtn.setBounds (nav.removeFromRight (140));
    }
    else
    {
        auto hdr = Rect (14, 14, 1076, 34);
        moduleViewBtn.setBounds (hdr.removeFromLeft (120));
        hdr.removeFromLeft (10);
        gainMatchBtn.setBounds (hdr.removeFromLeft (110));
        hdr.removeFromLeft (10);
        bypassBtn.setBounds (hdr.removeFromLeft (90));
        relearnBtn.setBounds (hdr.removeFromRight (110));
        // Tonal balance: meter is painted; the controls sit under it.
        const Rect tonal (330, 60, 460, 380);
        eqOnBtn.setBounds (tonal.getX() + 16, tonal.getBottom() - 46, 44, 30);
        eqAmount.setBounds (tonal.getX() + 70, tonal.getBottom() - 50, tonal.getWidth() - 90, 38);
        // Loudness.
        const Rect loud (330, 452, 460, 262);
        maxOnBtn.setBounds (loud.getX() + 16, loud.getY() + 20, 44, 30);
        maxOffset.setBounds (loud.getX() + 70, loud.getY() + 16, loud.getWidth() - 90, 38);
        resultDestBtn[0].setBounds (loud.getX() + 16, loud.getY() + 70, 150, 30);
        resultDestBtn[1].setBounds (loud.getX() + 176, loud.getY() + 70, 150, 30);
        // Extras.
        const Rect extras (806, 270, 284, 444);
        int y = extras.getY() + 40;
        PanelButton* ons[4] { &impactOnBtn, &imagerOnBtn, &clarityOnBtn, &stabilizerOnBtn };
        Fader* fds[4] { &impactAmount, &imagerAmount, &clarityAmount, &stabilizerAmount };
        for (int i = 0; i < 4; ++i)
        {
            ons[i]->setBounds (extras.getX() + 16, y + 4, 44, 30);
            fds[i]->setBounds (extras.getX() + 70, y, extras.getWidth() - 90, 38);
            y += 96;
        }
    }
}

void AssistantView::loadDefaults()
{
    Settings s;
    const auto xmlText = s.assistantDefaults();
    if (xmlText.isEmpty()) return;
    auto xml = XmlDocument::parse (xmlText);
    if (xml == nullptr) return;
    auto& st = proc.assistantSettings;
    st.targetIndex = xml->getIntAttribute ("target", st.targetIndex);
    st.intensity = xml->getIntAttribute ("intensity", st.intensity);
    st.loudnessLufs = (float) xml->getDoubleAttribute ("loudness", st.loudnessLufs);
    st.destination = xml->getIntAttribute ("destination", st.destination);
    st.analysisSeconds = (float) xml->getDoubleAttribute ("seconds", st.analysisSeconds);
    st.tempoSync = xml->getBoolAttribute ("tempoSync", st.tempoSync);
    st.analysisBars = xml->getIntAttribute ("bars", st.analysisBars);
    st.useEq = xml->getBoolAttribute ("eq", true); st.useBass = xml->getBoolAttribute ("bass", true);
    st.useClarity = xml->getBoolAttribute ("clarity", true); st.useStabilizer = xml->getBoolAttribute ("stabilizer", true);
    st.useImpact = xml->getBoolAttribute ("impact", true); st.useImager = xml->getBoolAttribute ("imager", true);
    st.useMaximizer = xml->getBoolAttribute ("maximizer", true);
    analysisKnob.setValue (st.tempoSync ? st.analysisBars * 4.0 : st.analysisSeconds, dontSendNotification);
    loudnessKnob.setValue (st.loudnessLufs, dontSendNotification);
}

void AssistantView::saveDefaults()
{
    const auto& st = proc.assistantSettings;
    XmlElement xml ("ASSISTANT");
    xml.setAttribute ("target", st.targetIndex);
    xml.setAttribute ("intensity", st.intensity);
    xml.setAttribute ("loudness", st.loudnessLufs);
    xml.setAttribute ("destination", st.destination);
    xml.setAttribute ("seconds", st.analysisSeconds);
    xml.setAttribute ("tempoSync", st.tempoSync);
    xml.setAttribute ("bars", st.analysisBars);
    xml.setAttribute ("eq", st.useEq); xml.setAttribute ("bass", st.useBass); xml.setAttribute ("clarity", st.useClarity);
    xml.setAttribute ("stabilizer", st.useStabilizer); xml.setAttribute ("impact", st.useImpact); xml.setAttribute ("imager", st.useImager);
    xml.setAttribute ("maximizer", st.useMaximizer);
    Settings s;
    s.setAssistantDefaults (xml.toString());
}

void AssistantView::startRun()
{
    if (saveAsDefault) saveDefaults();
    proc.startAssistant();
    setPage (Listening);
}

void AssistantView::addReference()
{
    chooser = std::make_unique<FileChooser> ("Choose a reference track", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File()) return;
        String err;
        if (! proc.assistant.addReferenceTarget (f, err))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't add the reference", err);
        else
            proc.assistantSettings.targetIndex = proc.assistant.settings().targetIndex;
        targets.repaint();
    });
}

String AssistantView::analysisText() const
{
    const auto& st = proc.assistantSettings;
    return st.tempoSync ? String (st.analysisBars) + " bars at " + String (proc.hostBpm(), 0) + " bpm" : String ((int) st.analysisSeconds) + " seconds";
}

void AssistantView::refresh()
{
    const auto& st = proc.assistantSettings;
    for (int i = 0; i < 4; ++i) filterBtn[i].setLit (targets.filter == i);
    const bool* flags[7] { &st.useEq, &st.useBass, &st.useClarity, &st.useStabilizer, &st.useImpact, &st.useImager, &st.useMaximizer };
    for (int i = 0; i < 7; ++i) moduleBtn[i].setLit (*flags[i]);
    for (int i = 0; i < 5; ++i) intensityBtn[i].setLit (st.intensity == i);
    for (int i = 0; i < 2; ++i) { destBtn[i].setLit (st.destination == i); resultDestBtn[i].setLit (st.destination == i); }
    tempoSyncBtn.setLit (st.tempoSync);
    saveDefaultBtn.setLit (saveAsDefault);
    removeTargetBtn.setEnabled (st.targetIndex >= proc.assistant.numGenreTargets());
    if (currentPage != Listening && proc.assistant.analysing())
        setPage (Listening);
    if (currentPage == Listening)
    {
        if (! proc.assistant.analysing())
        {
            setPage (proc.assistant.built() ? Result : Setup);
            if (proc.assistant.built())
                maxOffset.setValue (0.0, dontSendNotification);
        }
        repaint();
    }
    if (currentPage == Result)
    {
        auto& ch = proc.chain;
        gainMatchBtn.setLit (proc.getParam (ids::gainMatch) > 0.5f);
        bypassBtn.setLit (proc.getParam (ids::bypass) > 0.5f);
        auto onOf = [this] (const Module& m, int idx) { return proc.getParam (proc.paramId (m, idx)) > 0.5f; };
        if (! maxOffset.isMouseButtonDown())
            maxOffset.setValue (jlimit (-4.0f, 4.0f, proc.getParam (proc.paramId (ch.maximizer, ch.maximizer.pGain)) - proc.assistant.builtMaximizerGain()), dontSendNotification);
        eqOnBtn.setLit (onOf (ch.eq1, ch.eq1.pOn));
        maxOnBtn.setLit (onOf (ch.maximizer, ch.maximizer.pOn));
        impactOnBtn.setLit (onOf (ch.impact, ch.impact.pOn));
        imagerOnBtn.setLit (onOf (ch.imager, ch.imager.pOn));
        clarityOnBtn.setLit (onOf (ch.clarity, ch.clarity.pOn));
        stabilizerOnBtn.setLit (onOf (ch.stabilizer, ch.stabilizer.pOn));
        // Live tonal balance from the analyser: deviation from its mean (pink reads flat there).
        double sum = 0; int cnt = 0;
        for (int b = 0; b < TonalTargets::kNumBands; ++b)
        {
            liveCurve[b] = spectrum.dbAtHz (TonalTargets::centreHz (b));
            const float hz = TonalTargets::centreHz (b);
            if (hz >= 100.0f && hz <= 8000.0f && liveCurve[b] > -100.0f) { sum += liveCurve[b]; ++cnt; }
        }
        const float mean = cnt > 0 ? (float) (sum / cnt) : 0.0f;
        for (auto& v : liveCurve) v -= mean;
        repaint();
    }
}

void AssistantView::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::ink.withAlpha (0.85f));
    g.drawRect (getLocalBounds().toFloat().withTrimmedTop (-4.0f).withTrimmedBottom (12.0f), 1.5f);
    const auto& a = proc.assistant;
    if (currentPage == Setup)
    {
        drawSection (g, kTargetBox.toFloat(), "TARGET");
        drawSection (g, kProcessBox.toFloat(), "PROCESSING");
        drawSection (g, kLoudBox.toFloat(), "LOUDNESS");
        g.setColour (colours::inkDim);
        g.setFont (panelFont (12.0f, false));
        g.drawFittedText ("Pick a target, choose what to build and how boldly, then START and play the loudest part of the song for " + analysisText() + ".",
                          kNavRow.withY (kNavRow.getY() - 22).withHeight (18), Justification::centredLeft, 1, 1.0f);
        drawText (g, "INTENSITY", { (float) kProcessBox.getX() + 16.0f, (float) kProcessBox.getY() + 300.0f, 200.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, "OUTPUT LEVEL", { (float) kLoudBox.getX() + 16.0f, (float) kLoudBox.getY() + 214.0f, 200.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        g.setColour (colours::inkDim);
        g.setFont (panelFont (11.0f, false));
        g.drawFittedText ("FULL SCALE: -0.1 dB, true peak off. STREAMING: -1 dB, true peak on.", Rect (kLoudBox.getX() + 16, kLoudBox.getY() + 330, kLoudBox.getWidth() - 32, 50), Justification::topLeft, 3, 1.0f);
        g.drawFittedText ("Vocal balance needs a source-separation model, which Burnish does not have; the assistant leaves vocals as they are.",
                          Rect (kLoudBox.getX() + 16, kLoudBox.getY() + 400, kLoudBox.getWidth() - 32, 80), Justification::topLeft, 5, 1.0f);
    }
    else if (currentPage == Listening)
    {
        const auto r = getLocalBounds().toFloat();
        drawText (g, "LISTENING", { r.getX(), r.getCentreY() - 90.0f, r.getWidth(), 40.0f }, 34.0f, colours::brand, true);
        drawText (g, "Play the loudest, fullest part of the song: " + analysisText() + ".", { r.getX(), r.getCentreY() - 40.0f, r.getWidth(), 24.0f }, 15.0f, colours::ink, false);
        const auto bar = Rectangle<float> (500.0f, 24.0f).withCentre ({ r.getCentreX(), r.getCentreY() + 10.0f });
        drawLcdBox (g, bar);
        g.setColour (colours::lcd);
        g.fillRect (bar.reduced (3.0f).withWidth ((bar.getWidth() - 6.0f) * a.progress()));
        drawText (g, "Target: " + String (a.target().name) + "   Intensity: " + String (Assistant::intensityName (a.settings().intensity)), { r.getX(), r.getCentreY() + 50.0f, r.getWidth(), 20.0f }, 13.0f, colours::inkDim, false);
    }
    else
    {
        drawSection (g, Rect (14, 60, 300, 654).toFloat(), "TARGET LIBRARY");
        const Rect tonal (330, 60, 460, 380);
        drawSection (g, tonal.toFloat(), "TONAL BALANCE");
        // The tunnel: target +-3 dB, and the live curve.
        const auto plot = Rect (tonal.getX() + 16, tonal.getY() + 20, tonal.getWidth() - 32, 290).toFloat();
        drawLcdBox (g, plot);
        drawFrequencyGrid (g, plot.reduced (1.0f), true, 12.0f, -12.0f, 6.0f);
        const Target& t = a.target();
        auto xFor = [&] (float hz) { return plot.getX() + freqToNorm (hz) * plot.getWidth(); };
        auto yFor = [&] (float db) { return plot.getY() + (12.0f - jlimit (-12.0f, 12.0f, db)) / 24.0f * plot.getHeight(); };
        Path tunnel;
        for (int b = 0; b < TonalTargets::kNumBands; ++b)
        {
            const auto p = Point<float> (xFor (TonalTargets::centreHz (b)), yFor (t.spectrumDb[(size_t) b] + 3.0f));
            if (b == 0) tunnel.startNewSubPath (p); else tunnel.lineTo (p);
        }
        for (int b = TonalTargets::kNumBands - 1; b >= 0; --b)
            tunnel.lineTo (xFor (TonalTargets::centreHz (b)), yFor (t.spectrumDb[(size_t) b] - 3.0f));
        tunnel.closeSubPath();
        g.setColour (Colour (0xff3a6aa0).withAlpha (0.45f));
        g.fillPath (tunnel);
        Path live;
        for (int b = 0; b < TonalTargets::kNumBands; ++b)
        {
            const auto p = Point<float> (xFor (TonalTargets::centreHz (b)), yFor (liveCurve[b]));
            if (b == 0) live.startNewSubPath (p); else live.lineTo (p);
        }
        g.setColour (colours::ink);
        g.strokePath (live, PathStrokeType (2.0f, PathStrokeType::curved));
        drawText (g, "EQUALIZER", { (float) tonal.getX() + 70.0f, (float) tonal.getBottom() - 64.0f, 200.0f, 12.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);

        const Rect loud (330, 452, 460, 262);
        drawSection (g, loud.toFloat(), "LOUDNESS");
        drawText (g, "MAXIMIZER", { (float) loud.getX() + 70.0f, (float) loud.getY() + 2.0f, 200.0f, 12.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        const auto out = proc.chain.takeOutputMeters();
        drawText (g, String (out.lufsShort, 1) + " LUFS short-term   " + String (out.lufsIntegrated, 1) + " LUFS integrated", { (float) loud.getX() + 16.0f, (float) loud.getY() + 112.0f, 420.0f, 20.0f }, 13.0f, colours::lcd, true, Justification::centredLeft);
        g.setColour (colours::inkDim);
        g.setFont (panelFont (11.0f, false));
        g.drawFittedText (a.summary(), Rect (loud.getX() + 16, loud.getY() + 140, loud.getWidth() - 32, 110), Justification::topLeft, 6, 1.0f);

        const Rect vocal (806, 60, 284, 196);
        drawSection (g, vocal.toFloat(), "VOCAL BALANCE");
        g.setColour (colours::inkDim);
        g.setFont (panelFont (11.0f, false));
        g.drawFittedText ("Not measured: separating vocals from the music takes a trained model that Burnish does not include. Use Equalizer 2 or a Dynamic EQ on the presence region instead.",
                          vocal.reduced (16), Justification::topLeft, 8, 1.0f);
        const Rect extras (806, 270, 284, 444);
        drawSection (g, extras.toFloat(), "EXTRAS");
        const char* names[4] { "DYNAMICS MATCH", "WIDTH MATCH", "CLARITY", "STABILIZER" };
        for (int i = 0; i < 4; ++i)
            drawText (g, names[i], { (float) extras.getX() + 70.0f, (float) extras.getY() + 26.0f + 96.0f * (float) i, 200.0f, 12.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------
// TargetList
// ---------------------------------------------------------------------------
std::vector<int> AssistantView::TargetList::visibleRows() const
{
    std::vector<int> rows;
    const auto& a = view.proc.assistant;
    for (int i = 0; i < (int) a.targets().size(); ++i)
    {
        const bool genre = i < a.numGenreTargets();
        if (filter == 1 && ! genre) continue;
        if (filter == 2 && genre) continue;
        if (filter == 3 && ! a.isFavourite (i)) continue;
        rows.push_back (i);
    }
    return rows;
}

int AssistantView::TargetList::rowAt (float y) const
{
    const int r = (int) ((y + scroll) / kRowH);
    const auto rows = visibleRows();
    return r >= 0 && r < (int) rows.size() ? rows[(size_t) r] : -1;
}

void AssistantView::TargetList::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    drawLcdBox (g, r);
    const auto rows = visibleRows();
    const auto& a = view.proc.assistant;
    const int sel = view.proc.assistantSettings.targetIndex;
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (1));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int idx = rows[i];
        const auto row = Rectangle<float> (r.getX() + 2.0f, r.getY() + 2.0f + (float) i * kRowH - scroll, r.getWidth() - 4.0f, kRowH - 2.0f);
        if (row.getBottom() < r.getY() || row.getY() > r.getBottom()) continue;
        const bool selected = idx == sel;
        if (selected) { g.setColour (colours::cream); g.fillRect (row); }
        else if (idx == hoverRow) { g.setColour (colours::inkFaint.withAlpha (0.4f)); g.fillRect (row); }
        const Target& t = a.targets()[(size_t) idx];
        drawText (g, String (t.name).toUpperCase(), row.withTrimmedLeft (30.0f), 12.0f, selected ? colours::black : colours::lcd, true, Justification::centredLeft);
        drawText (g, String (t.lufs, 0) + " LUFS", row.withTrimmedRight (6.0f), 10.0f, selected ? colours::black : colours::inkDim, false, Justification::centredRight);
        // Favourite heart as a diamond LED.
        drawLed (g, { row.getX() + 14.0f, row.getCentreY() }, 5.0f, colours::ledRed, a.isFavourite (idx));
        if (t.custom)
            drawText (g, "REF", row.withTrimmedRight (70.0f), 9.0f, selected ? colours::black : colours::inkDim, true, Justification::centredRight);
    }
    g.restoreState();
}

void AssistantView::TargetList::mouseDown (const MouseEvent& e)
{
    const int idx = rowAt (e.position.y);
    if (idx < 0) return;
    if (e.position.x < 26.0f)
    {
        view.proc.assistant.setFavourite (idx, ! view.proc.assistant.isFavourite (idx));
        repaint();
        return;
    }
    view.proc.assistantSettings.targetIndex = idx;
    if (view.currentPage == Result)
        view.proc.assistant.retarget (idx);
    repaint();
}

void AssistantView::TargetList::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float maxScroll = jmax (0.0f, (float) visibleRows().size() * kRowH - (float) getHeight() + 4.0f);
    scroll = jlimit (0.0f, maxScroll, scroll - w.deltaY * 60.0f);
    repaint();
}
} // namespace burnish::ui
