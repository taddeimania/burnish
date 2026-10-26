#include "Assistant.h"
#include "Settings.h"

namespace burnish
{
namespace
{
const char* kIntensity[5] { "SUBTLE", "TRANSPARENT", "BALANCED", "BOLD", "TRANSFORMATIVE" };

juce::File targetFile (const juce::String& name)
{
    return Assistant::targetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".target");
}
} // namespace

const char* Assistant::intensityName (int i) { return kIntensity[juce::jlimit (0, 4, i)]; }
float Assistant::intensityFactor (int i) { const float f[5] { 0.3f, 0.5f, 0.7f, 0.9f, 1.15f }; return f[juce::jlimit (0, 4, i)]; }

juce::File Assistant::targetsFolder()
{
    return Settings::file().getParentDirectory().getChildFile ("Targets");
}

Assistant::Assistant (Chain& c) : chain (c)
{
    for (int t = 0; t < TonalTargets::kNumTargets; ++t)
        all.push_back (Target::genre (t));
    favourites.assign (all.size(), false);
    loadCustomTargets();
}

// ---------------------------------------------------------------------------
// Custom targets
// ---------------------------------------------------------------------------
void Assistant::loadCustomTargets()
{
    all.resize ((size_t) TonalTargets::kNumTargets);
    favourites.resize (all.size(), false);
    auto files = targetsFolder().findChildFiles (juce::File::findFiles, false, "*.target");
    files.sort();
    for (const auto& f : files)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("TARGET")) continue;
        Target t;
        t.name = f.getFileNameWithoutExtension().toStdString();
        t.custom = true;
        t.lufs = (float) xml->getDoubleAttribute ("lufs", -14.0);
        juce::StringArray sp; sp.addTokens (xml->getStringAttribute ("spectrum"), ",", "");
        for (int b = 0; b < AnalysisResult::kBands && b < sp.size(); ++b) t.spectrumDb[(size_t) b] = sp[b].getFloatValue();
        juce::StringArray w; w.addTokens (xml->getStringAttribute ("width"), ",", "");
        juce::StringArray cr; cr.addTokens (xml->getStringAttribute ("crest"), ",", "");
        for (int i = 0; i < 4; ++i)
        {
            if (i < w.size()) t.widthDb[(size_t) i] = w[i].getFloatValue();
            if (i < cr.size()) t.crestDb[(size_t) i] = cr[i].getFloatValue();
        }
        all.push_back (t);
        favourites.push_back (xml->getBoolAttribute ("favourite", false));
    }
}

bool Assistant::addReferenceTarget (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Couldn't read " + file.getFileName(); return false; }
    const double sr = reader->sampleRate;
    const juce::int64 total = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (sr * 600.0));   // ten minutes at most
    if (total < (juce::int64) (sr * 2.0)) { error = "The file is too short to learn from."; return false; }

    // Pass 1: short-term loudness over the file, to find the loudest 8 s.
    LoudnessMeter meter;
    meter.prepare (sr);
    meter.setTruePeak (false);
    const int block = 4096;
    juce::AudioBuffer<float> buf (2, block);
    float bestLufs = -200.0f;
    juce::int64 bestEnd = 0;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int len = (int) std::min<juce::int64> (block, total - pos);
        reader->read (&buf, 0, len, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = reader->numChannels > 1 ? buf.getReadPointer (1) : l;
        meter.process (l, r, len);
        const float st = meter.shortTerm();
        if (st > bestLufs) { bestLufs = st; bestEnd = pos + len; }
    }
    // Centre an 8 s window on the loudest 3 s.
    const juce::int64 win = (juce::int64) (sr * 8.0);
    juce::int64 start = std::max<juce::int64> (0, bestEnd - (juce::int64) (sr * 1.5) - win / 2);
    if (start + win > total) start = std::max<juce::int64> (0, total - win);

    // Pass 2: analyse the window.
    Analyzer an;
    an.prepare (sr);
    for (juce::int64 pos = start; pos < start + win; pos += block)
    {
        const int len = (int) std::min<juce::int64> (block, start + win - pos);
        reader->read (&buf, 0, len, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = reader->numChannels > 1 ? buf.getReadPointer (1) : l;
        an.push (l, r, len);
    }
    const auto res = an.result();
    if (! res.valid) { error = "Couldn't analyse the file."; return false; }
    Target t = Target::fromAnalysis (file.getFileNameWithoutExtension().toStdString(), res);

    // Save.
    targetsFolder().createDirectory();
    juce::XmlElement xml ("TARGET");
    xml.setAttribute ("lufs", t.lufs);
    juce::StringArray sp, w, cr;
    for (float v : t.spectrumDb) sp.add (juce::String (v, 2));
    for (float v : t.widthDb) w.add (juce::String (v, 2));
    for (float v : t.crestDb) cr.add (juce::String (v, 2));
    xml.setAttribute ("spectrum", sp.joinIntoString (","));
    xml.setAttribute ("width", w.joinIntoString (","));
    xml.setAttribute ("crest", cr.joinIntoString (","));
    if (! xml.writeTo (targetFile (t.name))) { error = "Couldn't save the target."; return false; }
    loadCustomTargets();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].name == t.name) current.targetIndex = (int) i;
    return true;
}

void Assistant::removeCustomTarget (int index)
{
    if (index < numGenreTargets() || index >= (int) all.size()) return;
    targetFile (all[(size_t) index].name).deleteFile();
    loadCustomTargets();
    current.targetIndex = juce::jlimit (0, (int) all.size() - 1, current.targetIndex);
}

bool Assistant::isFavourite (int index) const
{
    return index >= 0 && index < (int) favourites.size() && favourites[(size_t) index];
}

void Assistant::setFavourite (int index, bool on)
{
    if (index < 0 || index >= (int) favourites.size()) return;
    favourites[(size_t) index] = on;
    if (index >= numGenreTargets())
    {
        const auto f = targetFile (all[(size_t) index].name);
        if (auto xml = juce::XmlDocument::parse (f)) { xml->setAttribute ("favourite", on); xml->writeTo (f); }
    }
    else
    {
        Settings s;
        s.setGenreFavourite (index, on);
    }
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------
void Assistant::start (const AssistantSettings& s, double hostBpm)
{
    current = s;
    bpm = hostBpm;
    chain.analyser.reset();
    running = true;
}

void Assistant::cancel() { running = false; }

float Assistant::progress() const
{
    if (! running) return 0.0f;
    const float want = current.tempoSync ? (float) (current.analysisBars * 4.0 * 60.0 / std::max (20.0, bpm)) : current.analysisSeconds;
    return juce::jlimit (0.0f, 1.0f, chain.analyser.seconds() / std::max (1.0f, want));
}

void Assistant::tick()
{
    if (! running) return;
    if (progress() >= 1.0f)
    {
        running = false;
        lastResult = chain.analyser.result();
        if (lastResult.valid)
            build();
    }
}

void Assistant::retarget (int targetIndex)
{
    current.targetIndex = juce::jlimit (0, (int) all.size() - 1, targetIndex);
    if (lastResult.valid)
        build();
}

void Assistant::build()
{
    const Target& t = target();
    const AnalysisResult& a = lastResult;
    const float k = intensityFactor (current.intensity);
    std::vector<int> order;
    juce::StringArray lines;

    // Tonal balance -> Equalizer 1: eight bands at fixed centres, each the
    // mean of the needed correction around it.
    if (current.useEq)
    {
        auto& eq = chain.eq1;
        const float centres[8] { 45.0f, 110.0f, 260.0f, 620.0f, 1500.0f, 3500.0f, 8000.0f, 14000.0f };
        const int shapes[8] { LowShelfBaxandall, Bell, Bell, Bell, Bell, Bell, Bell, HighShelfBaxandall };
        float totalMove = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            double sum = 0; int cnt = 0;
            for (int i = 0; i < AnalysisResult::kBands; ++i)
            {
                const float hz = TonalTargets::centreHz (i);
                if (hz >= centres[b] / 1.414f && hz <= centres[b] * 1.414f)
                {
                    sum += t.spectrumDb[(size_t) i] - a.spectrumDb[(size_t) i];
                    ++cnt;
                }
            }
            const float need = cnt > 0 ? (float) (sum / cnt) : 0.0f;
            const float gain = juce::jlimit (-6.0f, 6.0f, need * 0.6f * k);
            totalMove += std::fabs (gain);
            const auto& bi = eq.band[0][b];
            setParam (eq.params()[(size_t) bi.on].id, 1.0f);
            setParam (eq.params()[(size_t) bi.shape].id, (float) shapes[b]);
            setParam (eq.params()[(size_t) bi.freq].id, centres[b]);
            setParam (eq.params()[(size_t) bi.gain].id, gain);
            setParam (eq.params()[(size_t) bi.q].id, 0.9f);
        }
        setParam (eq.params()[(size_t) eq.pAmount].id, 100.0f);
        setParam (eq.params()[(size_t) eq.pMode].id, 0.0f);
        setParam (eq.params()[(size_t) eq.pChMode].id, 0.0f);
        setParam (eq.params()[(size_t) eq.pOn].id, 1.0f);
        order.push_back (ModEq1);
        lines.add ("EQ: " + juce::String (totalMove, 1) + " dB of movement across eight bands toward the " + juce::String (t.name) + " balance.");
    }

    if (current.useBass)
    {
        // Bass heavier than the target: peak control; lighter: a touch of sustain.
        const float lowDiff = t.spectrumDb[6] - a.spectrumDb[6];   // around 40 Hz
        auto& bc = chain.bassControl;
        setParam (bc.params()[(size_t) bc.pOn].id, 1.0f);
        setParam (bc.params()[(size_t) bc.pBalance].id, juce::jlimit (-3.0f, 3.0f, lowDiff * 0.4f * k));
        setParam (bc.params()[(size_t) bc.pPeakOn].id, a.crestDb[0] > t.crestDb[0] + 2.0f ? 1.0f : 0.0f);
        setParam (bc.params()[(size_t) bc.pPeakThresh].id, -6.0f);
        setParam (bc.params()[(size_t) bc.pSustainOn].id, a.crestDb[0] > t.crestDb[0] + 2.0f ? 1.0f : 0.0f);
        setParam (bc.params()[(size_t) bc.pSustainAmount].id, 25.0f * k);
        order.push_back (ModBassControl);
    }

    if (current.useClarity)
    {
        auto& c = chain.clarity;
        // Tilt follows how bright the target is above 1 kHz compared with the all-purpose curve.
        const float bright = (t.spectrumDb[55] - t.spectrumDb[33]) - (TonalTargets::curveDb (0, 55) - TonalTargets::curveDb (0, 33));
        setParam (c.params()[(size_t) c.pOn].id, 1.0f);
        setParam (c.params()[(size_t) c.pAmount].id, juce::jlimit (0.0f, 100.0f, 25.0f + 45.0f * k));
        setParam (c.params()[(size_t) c.pTilt].id, juce::jlimit (-2.0f, 2.0f, bright * 0.25f));
        order.push_back (ModClarity);
    }

    if (current.useStabilizer)
    {
        auto& s = chain.stabilizer;
        setParam (s.params()[(size_t) s.pOn].id, 1.0f);
        setParam (s.params()[(size_t) s.pAmount].id, juce::jlimit (0.0f, 100.0f, 15.0f + 45.0f * k));
        setParam (s.params()[(size_t) s.pMode].id, 0.0f);
        if (t.custom)
        {
            s.assistantTarget = t.spectrumDb;
            s.hasAssistantTarget.store (true);
            setParam (s.params()[(size_t) s.pTarget].id, (float) TonalTargets::kNumTargets);
        }
        else
            setParam (s.params()[(size_t) s.pTarget].id, (float) juce::jlimit (0, TonalTargets::kNumTargets - 1, current.targetIndex));
        order.push_back (ModStabilizer);
    }

    if (current.useImpact)
    {
        auto& im = chain.impact;
        setParam (im.params()[(size_t) im.pOn].id, 1.0f);
        setParam (im.params()[(size_t) im.mb.pNumBands].id, 3.0f);
        setParam (im.params()[(size_t) im.mb.pXover[0]].id, 120.0f);
        setParam (im.params()[(size_t) im.mb.pXover[1]].id, 1000.0f);
        setParam (im.params()[(size_t) im.mb.pXover[2]].id, 6000.0f);
        setParam (im.params()[(size_t) im.pAmount].id, 100.0f);
        float sum = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            const float v = juce::jlimit (-50.0f, 50.0f, (t.crestDb[(size_t) b] - a.crestDb[(size_t) b]) * 8.0f * k);
            setParam (im.params()[(size_t) im.pImpact[0][b]].id, v);
            sum += v;
        }
        order.push_back (ModImpact);
        lines.add (juce::String ("Impact: ") + (sum >= 0 ? "opening up" : "tightening") + " the microdynamics in four bands.");
    }

    if (current.useImager)
    {
        auto& img = chain.imager;
        setParam (img.params()[(size_t) img.pOn].id, 1.0f);
        setParam (img.params()[(size_t) img.mb.pNumBands].id, 3.0f);
        setParam (img.params()[(size_t) img.mb.pXover[0]].id, 120.0f);
        setParam (img.params()[(size_t) img.mb.pXover[1]].id, 1000.0f);
        setParam (img.params()[(size_t) img.mb.pXover[2]].id, 6000.0f);
        setParam (img.params()[(size_t) img.pAmount].id, 100.0f);
        for (int b = 0; b < 4; ++b)
        {
            float v = juce::jlimit (-60.0f, 40.0f, (t.widthDb[(size_t) b] - a.widthDb[(size_t) b]) * 6.0f * k);
            if (b == 0 && a.widthDb[0] < -12.0f && v > 0.0f) v = 0.0f;   // a narrow bass stays narrow
            setParam (img.params()[(size_t) img.pWidth[b]].id, v);
        }
        setParam (img.params()[(size_t) img.pStereoizeOn].id, 0.0f);
        order.push_back (ModImager);
    }

    if (current.useMaximizer)
    {
        auto& m = chain.maximizer;
        // The other modules are roughly loudness neutral, so the gain is the
        // distance from what was heard to the wanted loudness, less a little
        // that limiting will take back.
        maxGainBuilt = juce::jlimit (0.0f, 24.0f, current.loudnessLufs - a.integratedLufs + 1.0f);
        setParam (m.params()[(size_t) m.pOn].id, 1.0f);
        setParam (m.params()[(size_t) m.pMode].id, current.intensity >= 3 ? (float) Maximizer::Irc5 : (float) Maximizer::Irc3);
        setParam (m.params()[(size_t) m.pStyle3].id, 2.0f);
        setParam (m.params()[(size_t) m.pGain].id, maxGainBuilt);
        setParam (m.params()[(size_t) m.pOutput].id, current.destination == 0 ? -0.1f : -1.0f);
        setParam (m.params()[(size_t) m.pTruePeak].id, current.destination == 0 ? 0.0f : 1.0f);
        setParam (m.params()[(size_t) m.pCharacter].id, 5.0f);
        setParam (m.params()[(size_t) m.pTargetLufs].id, current.loudnessLufs);
        setParam (m.params()[(size_t) m.pClipOn].id, current.intensity >= 3 ? 1.0f : 0.0f);
        setParam (m.params()[(size_t) m.pClipAmount].id, 30.0f);
        setParam (m.params()[(size_t) m.pUpward].id, current.intensity >= 2 ? 1.5f * k : 0.0f);
        order.push_back (ModMaximizer);
        lines.add ("Maximizer: heard " + juce::String (a.integratedLufs, 1) + " LUFS, gain +" + juce::String (maxGainBuilt, 1)
                   + " dB toward " + juce::String (current.loudnessLufs, 1) + " LUFS.");
    }

    setOrder (order);
    // The other modules change the level a little; let the Maximizer listen
    // to the processed signal for five seconds and trim its gain to the target.
    if (current.useMaximizer)
        chain.maximizer.learnRequests.fetch_add (1);
    hasBuilt = true;
    summaryText = lines.joinIntoString ("\n");
}
} // namespace burnish
