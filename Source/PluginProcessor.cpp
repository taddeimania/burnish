#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Settings.h"

using namespace burnish;

namespace
{
constexpr const char* kPresetExtension = ".burnish";
constexpr const char* kModulePresetExtension = ".burnishmod";
constexpr const char* kOrderProperty = "order";

juce::String orderToString (const std::vector<int>& order)
{
    juce::StringArray a;
    for (int id : order) a.add (juce::String (id));
    return a.joinIntoString (",");
}

std::vector<int> orderFromString (const juce::String& s)
{
    std::vector<int> v;
    juce::StringArray a;
    a.addTokens (s, ",", "");
    for (const auto& t : a)
        if (t.trim().isNotEmpty()) v.push_back (t.getIntValue());
    return v;
}
} // namespace

BurnishProcessor::BurnishProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      assistant (chain),
      apvts (*this, nullptr, "BURNISH", createParameterLayout (chain))
{
    cacheParams();
    for (int m = 0; m < NumModules; ++m)
        chain.module (m).setDefaults();
    apvts.state.setProperty (kOrderProperty, "", nullptr);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);

    assistant.setParam = [this] (const juce::String& id, float v) { setParam (id, v); };
    assistant.setOrder = [this] (const std::vector<int>& o) { setOrder (o); };

    currentName = factoryPresets().front().name;
    loadedValues = snapshot();
    startTimerHz (20);
}

BurnishProcessor::~BurnishProcessor()
{
    stopTimer();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool BurnishProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (in) && ok (out);
}

void BurnishProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    monoScratch.setSize (1, juce::jmax (512, samplesPerBlock));
    lastLatency = chain.latency();
    setLatencySamples (lastLatency);
}

// ---------------------------------------------------------------------------
void BurnishProcessor::cacheParams()
{
    auto ptr = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    fast.inL = ptr (ids::inGainL); fast.inR = ptr (ids::inGainR);
    fast.outL = ptr (ids::outGainL); fast.outR = ptr (ids::outGainR);
    fast.bypass = ptr (ids::bypass); fast.gainMatch = ptr (ids::gainMatch);
    fast.mono = ptr (ids::mono); fast.swap = ptr (ids::swap);
    fast.meterType = ptr (ids::meterType); fast.meterMs = ptr (ids::meterMs);
    fast.ditherOn = ptr (ids::ditherOn); fast.ditherBits = ptr (ids::ditherBits); fast.ditherBlank = ptr (ids::ditherBlank);
    fast.ditherAmount = ptr (ids::ditherAmount); fast.ditherHarmonic = ptr (ids::ditherHarmonic); fast.ditherLimit = ptr (ids::ditherLimit);
    fast.ditherShaping = ptr (ids::ditherShaping); fast.dcFilter = ptr (ids::dcFilter);

    moduleParams.resize (NumModules);
    for (int m = 0; m < NumModules; ++m)
    {
        auto& v = moduleParams[(size_t) m];
        v.clear();
        for (const auto& d : chain.module (m).params())
            v.push_back (apvts.getRawParameterValue (d.id));
    }
}

void BurnishProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

float BurnishProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

// Audio thread.
void BurnishProcessor::readSettings()
{
    auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    settings.inGainDb[0] = get (fast.inL); settings.inGainDb[1] = get (fast.inR);
    settings.outGainDb[0] = get (fast.outL); settings.outGainDb[1] = get (fast.outR);
    settings.bypass = get (fast.bypass) > 0.5f;
    settings.gainMatch = get (fast.gainMatch) > 0.5f;
    settings.mono = get (fast.mono) > 0.5f;
    settings.swap = get (fast.swap) > 0.5f;
    settings.dcFilter = get (fast.dcFilter) > 0.5f;
    settings.dither.on = get (fast.ditherOn) > 0.5f;
    const int bitsIdx = juce::roundToInt (get (fast.ditherBits));
    settings.dither.bitDepth = bitsIdx == 0 ? 24 : bitsIdx == 1 ? 20 : bitsIdx == 2 ? 16 : bitsIdx == 3 ? 12 : 8;
    settings.dither.autoBlank = get (fast.ditherBlank) > 0.5f;
    settings.dither.amount = juce::roundToInt (get (fast.ditherAmount));
    settings.dither.harmonicSuppression = get (fast.ditherHarmonic) > 0.5f;
    settings.dither.limitPeaks = get (fast.ditherLimit) > 0.5f;
    settings.dither.shaping = juce::roundToInt (get (fast.ditherShaping));
    settings.soloModule = soloModule.load (std::memory_order_relaxed);
    settings.deltaModule = deltaModule.load (std::memory_order_relaxed);
    settings.analysing = analysing.load (std::memory_order_relaxed);
    settings.meterType = juce::roundToInt (get (fast.meterType));
    settings.meterMidSide = get (fast.meterMs) > 0.5f;

    for (int m = 0; m < NumModules; ++m)
    {
        auto& mod = chain.module (m);
        const auto& src = moduleParams[(size_t) m];
        float* dst = mod.values();
        for (size_t i = 0; i < src.size(); ++i)
            dst[i] = src[i]->load (std::memory_order_relaxed);
    }
}

void BurnishProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (numOut == 0 || n == 0)
        return;
    for (int c = numIn; c < numOut; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm.store (*b);
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying) meterResets.fetch_add (1);
            wasPlaying = playing;
        }
    chain.setBpm (bpm.load());

    const int mr = meterResets.load (std::memory_order_relaxed);
    if (mr != seenMeterResets) { seenMeterResets = mr; chain.resetMeters(); }
    const int cr = clipResets.load (std::memory_order_relaxed);
    if (cr != seenClipResets) { seenClipResets = cr; chain.resetMeters(); }

    readSettings();

    float* left = buffer.getWritePointer (0);
    if (numOut >= 2)
        chain.process (left, buffer.getWritePointer (1), n, settings);
    else
    {
        float* right = monoScratch.getWritePointer (0);
        for (int start = 0; start < n; start += monoScratch.getNumSamples())
        {
            const int len = juce::jmin (monoScratch.getNumSamples(), n - start);
            std::copy (left + start, left + start + len, right);
            chain.process (left + start, right, len, settings);
            for (int i = 0; i < len; ++i)
                left[start + i] = 0.5f * (left[start + i] + right[i]);
        }
    }
}

// ---------------------------------------------------------------------------
void BurnishProcessor::timerCallback()
{
    // Latency follows the chain.
    const int lat = chain.latency();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        setLatencySamples (lat);
    }

    // A finished Maximizer LEARN.
    if (chain.maximizer.learnedReady.exchange (false))
        setParam (paramId (chain.maximizer, chain.maximizer.pGain), chain.maximizer.learnedGain.load());

    // The assistant.
    if (assistant.analysing())
    {
        analysing.store (true);
        assistant.tick();
        if (! assistant.analysing())
            analysing.store (false);
    }
    else
        analysing.store (false);

    // Undo: snapshot the state a little after the last change.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (dirty.load() && now - lastPushMs > 600.0)
    {
        dirty.store (false);
        pushUndo();
        lastPushMs = now;
    }
}

void BurnishProcessor::parameterChanged (const juce::String&, float)
{
    if (! applying)
        dirty.store (true);
}

// ---------------------------------------------------------------------------
// Order
// ---------------------------------------------------------------------------
void BurnishProcessor::setOrder (const std::vector<int>& order)
{
    chain.setOrder (order);
    apvts.state.setProperty (kOrderProperty, orderToString (chain.getOrder()), nullptr);
    if (! applying) dirty.store (true);
}

void BurnishProcessor::syncOrderFromState()
{
    chain.setOrder (orderFromString (apvts.state.getProperty (kOrderProperty).toString()));
}

void BurnishProcessor::addModule (int id, int position)
{
    auto order = getOrder();
    for (int i : order) if (i == id) return;
    if (position < 0 || position > (int) order.size()) order.push_back (id);
    else order.insert (order.begin() + position, id);
    setOrder (order);
}

void BurnishProcessor::removeModule (int id)
{
    auto order = getOrder();
    order.erase (std::remove (order.begin(), order.end(), id), order.end());
    setOrder (order);
    if (soloModule.load() == id) soloModule.store (-1);
    if (deltaModule.load() == id) deltaModule.store (-1);
}

void BurnishProcessor::moveModule (int id, int newPosition)
{
    auto order = getOrder();
    order.erase (std::remove (order.begin(), order.end(), id), order.end());
    newPosition = juce::jlimit (0, (int) order.size(), newPosition);
    order.insert (order.begin() + newPosition, id);
    setOrder (order);
}

void BurnishProcessor::resetModule (int id)
{
    for (const auto& d : chain.module (id).params())
        setParam (d.id, d.def);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
int BurnishProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String BurnishProcessor::getProgramName (int index)
{
    const auto& f = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

juce::File BurnishProcessor::userPresetFolder() { return Settings::file().getParentDirectory().getChildFile ("Presets"); }
juce::File BurnishProcessor::userModulePresetFolder (int module)
{
    return Settings::file().getParentDirectory().getChildFile ("Module Presets").getChildFile (juce::String (Chain::moduleShortName (module)).replaceCharacter (' ', '_'));
}

juce::Array<juce::File> BurnishProcessor::userPresets()
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExtension);
    files.sort();
    return files;
}

juce::Array<juce::File> BurnishProcessor::userModulePresets (int module)
{
    auto files = userModulePresetFolder (module).findChildFiles (juce::File::findFiles, false, juce::String ("*") + kModulePresetExtension);
    files.sort();
    return files;
}

std::vector<float> BurnishProcessor::snapshot() const
{
    std::vector<float> v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! ids::notInPreset (rp->getParameterID()))
                v.push_back (rp->getValue());
    return v;
}

bool BurnishProcessor::presetEdited() const
{
    const auto now = snapshot();
    if (now.size() != loadedValues.size()) return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (std::abs (now[i] - loadedValues[i]) > 0.002f) return true;
    return getOrder() != loadedOrder;
}

void BurnishProcessor::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& preset = list[(size_t) index];
    applying = true;
    pushUndo();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (ids::notInPreset (rp->getParameterID())) continue;
            float v = rp->getDefaultValue();
            for (const auto& [id, plain] : preset.values)
                if (rp->getParameterID() == id)
                    v = rp->convertTo0to1 (plain);
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
    setOrder (preset.order);
    applying = false;
    factoryIndex = index;
    userFile = juce::File();
    currentName = preset.name;
    loadedValues = snapshot();
    loadedOrder = getOrder();
}

std::unique_ptr<juce::XmlElement> BurnishProcessor::stateXml()
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return nullptr;
    xml->setAttribute ("presetName", currentName);
    xml->setAttribute ("presetFactory", userFile == juce::File() ? factoryIndex : -1);
    xml->setAttribute ("presetFile", userFile.getFullPathName());
    xml->setAttribute (kOrderProperty, orderToString (getOrder()));
    // Match EQ snapshots.
    const auto& m = chain.matchEq;
    if (m.hasReference())
    {
        juce::MemoryBlock mb (m.referenceDb.data(), m.referenceDb.size() * sizeof (float));
        xml->setAttribute ("matchReference", mb.toBase64Encoding());
    }
    return xml;
}

void BurnishProcessor::applyStateXml (const juce::XmlElement& xml, bool includeOrder)
{
    applying = true;
    for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (ids::notInPreset (id)) continue;
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
            p->endChangeGesture();
        }
    }
    if (includeOrder)
        setOrder (orderFromString (xml.getStringAttribute (kOrderProperty)));
    if (xml.hasAttribute ("matchReference"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (xml.getStringAttribute ("matchReference")) && mb.getSize() == (size_t) MatchEq::kBins * sizeof (float))
        {
            std::vector<float> db (MatchEq::kBins);
            std::memcpy (db.data(), mb.getData(), mb.getSize());
            chain.matchEq.setReferenceDb (db);
        }
    }
    applying = false;
}

bool BurnishProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    pushUndo();
    applyStateXml (*xml, true);
    userFile = file;
    factoryIndex = -1;
    currentName = file.getFileNameWithoutExtension();
    loadedValues = snapshot();
    loadedOrder = getOrder();
    return true;
}

bool BurnishProcessor::saveUserPreset (const juce::String& rawName, juce::String& error)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) { error = "Give the preset a name."; return false; }
    const auto folder = userPresetFolder();
    if (! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }
    const auto file = folder.getChildFile (name + kPresetExtension);
    currentName = name;
    userFile = file;
    factoryIndex = -1;
    auto xml = stateXml();
    if (xml != nullptr)
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (ids::notInPreset (e->getStringAttribute ("id")))
                e->setAttribute ("value", 0.0);
    if (xml == nullptr || ! xml->writeTo (file)) { error = "Couldn't write " + file.getFullPathName(); return false; }
    loadedValues = snapshot();
    loadedOrder = getOrder();
    return true;
}

void BurnishProcessor::stepPreset (int delta)
{
    const int numFactory = (int) factoryPresets().size();
    const auto users = userPresets();
    const int total = numFactory + users.size();
    int cur = presetNumber() - 1;
    if (cur < 0) cur = 0;
    const int next = (cur + delta + total) % total;
    if (next < numFactory) loadFactory (next);
    else loadUserPreset (users[next - numFactory]);
}

int BurnishProcessor::presetNumber() const
{
    if (userFile == juce::File())
        return factoryIndex >= 0 ? factoryIndex + 1 : 0;
    const auto users = userPresets();
    const int i = users.indexOf (userFile);
    return i >= 0 ? (int) factoryPresets().size() + i + 1 : 0;
}

void BurnishProcessor::loadModulePreset (int module, const ModulePreset& preset)
{
    pushUndo();
    applying = true;
    for (const auto& d : chain.module (module).params())
    {
        float v = d.def;
        for (const auto& [id, plain] : preset.values)
            if (d.id == id) v = plain;
        setParam (d.id, v);
    }
    applying = false;
}

bool BurnishProcessor::loadUserModulePreset (int module, const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MODULE")) return false;
    pushUndo();
    applying = true;
    const juce::String prefix = juce::String (chain.module (module).info().id) + ".";
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (! id.startsWith (prefix)) continue;
        setParam (id, (float) e->getDoubleAttribute ("value"));
    }
    applying = false;
    return true;
}

bool BurnishProcessor::saveUserModulePreset (int module, const juce::String& rawName, juce::String& error)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) { error = "Give the preset a name."; return false; }
    const auto folder = userModulePresetFolder (module);
    if (! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }
    juce::XmlElement xml ("MODULE");
    xml.setAttribute ("module", Chain::moduleName (module));
    for (const auto& d : chain.module (module).params())
    {
        auto* e = xml.createNewChildElement ("PARAM");
        e->setAttribute ("id", d.id);
        e->setAttribute ("value", getParam (d.id));
    }
    if (! xml.writeTo (folder.getChildFile (name + kModulePresetExtension))) { error = "Couldn't write the preset."; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Undo and snapshots
// ---------------------------------------------------------------------------
void BurnishProcessor::pushUndo()
{
    if (auto xml = stateXml())
    {
        // Skip if nothing changed since the last entry.
        if (! undoStack.empty() && undoStack.back()->isEquivalentTo (xml.get(), false))
            return;
        undoStack.push_back (std::move (xml));
        if (undoStack.size() > 60)
            undoStack.erase (undoStack.begin());
    }
}

void BurnishProcessor::undo()
{
    if (undoStack.empty()) return;
    // The top entry is the state after the last change; restore the one before.
    auto current = stateXml();
    if (undoStack.size() >= 2 && current != nullptr && undoStack.back()->isEquivalentTo (current.get(), false))
        undoStack.pop_back();
    auto xml = std::move (undoStack.back());
    undoStack.pop_back();
    applyStateXml (*xml, true);
    dirty.store (false);
}

void BurnishProcessor::setSnapshot (int slot)
{
    if (slot < 0 || slot > 3) return;
    snapshots[(size_t) slot] = stateXml();
    activeSnap = slot;
}

void BurnishProcessor::recallSnapshot (int slot)
{
    if (slot < 0 || slot > 3 || snapshots[(size_t) slot] == nullptr) return;
    pushUndo();
    applyStateXml (*snapshots[(size_t) slot], true);
    activeSnap = slot;
}

// ---------------------------------------------------------------------------
void BurnishProcessor::startAssistant()
{
    pushUndo();
    assistant.start (assistantSettings, bpm.load());
    analysing.store (true);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void BurnishProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateXml())
    {
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id == ids::bypass || id == ids::mono || id == ids::swap)
                e->setAttribute ("value", 0.0);
        }
        copyXmlToBinary (*xml, destData);
    }
}

void BurnishProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    applying = true;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    applying = false;
    if (xml->hasAttribute (kOrderProperty))
        apvts.state.setProperty (kOrderProperty, xml->getStringAttribute (kOrderProperty), nullptr);
    syncOrderFromState();
    if (xml->hasAttribute ("matchReference"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (xml->getStringAttribute ("matchReference")) && mb.getSize() == (size_t) MatchEq::kBins * sizeof (float))
        {
            std::vector<float> db (MatchEq::kBins);
            std::memcpy (db.data(), mb.getData(), mb.getSize());
            chain.matchEq.setReferenceDb (db);
        }
    }
    currentName = xml->getStringAttribute ("presetName", currentName);
    factoryIndex = xml->getIntAttribute ("presetFactory", -1);
    userFile = juce::File (xml->getStringAttribute ("presetFile"));
    if (userFile != juce::File() && ! userFile.existsAsFile())
        userFile = juce::File();
    loadedValues = snapshot();
    loadedOrder = getOrder();
}

juce::AudioProcessorEditor* BurnishProcessor::createEditor()
{
    return new BurnishEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BurnishProcessor();
}
