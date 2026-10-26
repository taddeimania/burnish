#include "Equalizer.h"

namespace burnish
{
namespace
{
std::vector<std::string> shapeChoices()
{
    std::vector<std::string> v;
    for (int i = 0; i < NumFilterShapes; ++i) v.push_back (filterShapeName (i));
    return v;
}
const char* setName (int s) { return s == 0 ? "a" : "b"; }
} // namespace

Equalizer::Equalizer (int instance)
    : Module ({ instance == 1 ? "eq1" : "eq2", instance == 1 ? "EQUALIZER 1" : "EQUALIZER 2",
                instance == 1 ? "EQ 1" : "EQ 2", instance == 1 ? 0xff5cc8f2u : 0xff7fd0f0u, true, true }),
      instanceNo (instance)
{
    pOn     = addBool ("on", "On", true);
    pMode   = addChoice ("mode", "Mode", { "Analog", "Digital" }, Analog);
    pChMode = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side", "Left/Right" }, Stereo);
    pAmount = addPct ("amount", "Amount", 0.0f, 200.0f, 100.0f);
    pChanOn[0] = addBool ("a.on", "A On", true);
    pChanOn[1] = addBool ("b.on", "B On", true);
    pLink   = addBool ("link", "Link Channels", false);

    // Default bands: four gentle ones on at 0 dB, four off.
    const int defShape[kBands] { LowShelfBaxandall, Bell, Bell, HighShelfBaxandall, Bell, Bell, Bell, Bell };
    const float defFreq[kBands] { 80.0f, 300.0f, 2000.0f, 8000.0f, 150.0f, 600.0f, 4000.0f, 12000.0f };
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (setName (s)) + ".b" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            auto& bi = band[s][b];
            bi.on    = addBool (p + "on", n + "On", b < 4);
            bi.shape = addChoice (p + "shape", n + "Shape", shapeChoices(), defShape[b]);
            bi.freq  = addHz (p + "freq", n + "Freq", 20.0f, 20000.0f, defFreq[b]);
            bi.gain  = addDb (p + "gain", n + "Gain", -30.0f, 15.0f, 0.0f);
            bi.q     = addFloat (p + "q", n + "Q", 0.1f, 40.0f, 1.0f, "", 2, 0.0f);
            bi.slope = addChoice (p + "slope", n + "Slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 3);
        }
}

void Equalizer::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    stft.prepare (2048, 512, 2);
    for (auto& g : binGain) g.assign ((size_t) stft.numBins(), 1.0f);
    scratchL.assign ((size_t) maxBlock, 0.0f);
    scratchR.assign ((size_t) maxBlock, 0.0f);
    reset();
    updateFilters (true);
}

void Equalizer::reset()
{
    for (auto& c : ch)
    {
        for (auto& b : c.bands) b.reset();
        c.solo.reset();
    }
    stft.reset();
    sinceUpdate = kStep;
    binsDirty = true;
    binsSince = 0;
}

float Equalizer::effectiveGain (int set, int b) const
{
    const auto& bi = band[set][b];
    if (! shapeHasGain ((int) value (bi.shape)))
        return 0.0f;
    return clampf (value (bi.gain) * value (pAmount) * 0.01f, -30.0f, 15.0f);
}

void Equalizer::designBand (FilterBand& f, int set, int b) const
{
    const auto& bi = band[set][b];
    f.design ((int) value (bi.shape), value (bi.freq), effectiveGain (set, b), value (bi.q),
              std::min (kNumSlopes - 1, std::max (0, (int) value (bi.slope))), sr);
}

void Equalizer::updateFilters (bool force)
{
    const int chMode = (int) value (pChMode);
    const int mode = (int) value (pMode);
    bool changed = force || chMode != lastChMode || mode != lastMode || value (pAmount) != lastAmount;
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const auto& bi = band[s][b];
            const float v[6] { value (bi.on), value (bi.shape), value (bi.freq), value (bi.gain), value (bi.q), value (bi.slope) };
            for (int k = 0; k < 6; ++k)
                if (v[k] != lastVals[s][b][k]) { changed = true; lastVals[s][b][k] = v[k]; }
        }
    if (! changed)
        return;
    lastChMode = chMode;
    lastMode = mode;
    lastAmount = value (pAmount);

    for (int c = 0; c < 2; ++c)
    {
        const int s = ChannelModes::setFor (chMode, c);
        for (int b = 0; b < kBands; ++b)
        {
            if (value (band[s][b].on) > 0.5f)
                designBand (ch[c].bands[b], s, b);
            else
                ch[c].bands[b].kind = 0;
        }
    }
    binsDirty = true;
}

void Equalizer::buildBinGains()
{
    const int chMode = (int) value (pChMode);
    const int nb = stft.numBins();
    for (int c = 0; c < 2; ++c)
    {
        const int s = ChannelModes::setFor (chMode, c);
        if (c == 1 && s == 0) { binGain[1] = binGain[0]; break; }
        auto& g = binGain[(size_t) c];
        for (int k = 0; k < nb; ++k)
        {
            const double hz = std::max (1.0, (double) Stft::binHz (k, stft.frameSize(), sr));
            double db = 0.0;
            if (value (pChanOn[s]) > 0.5f)
                for (int b = 0; b < kBands; ++b)
                    db += ch[c].bands[b].responseDb (hz, sr);
            g[(size_t) k] = dbToGain ((float) db);
        }
    }
    binsDirty = false;
}

void Equalizer::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
    {
        // DIGITAL keeps its latency when off, so the chain delay stays put.
        if (value (pMode) > 0.5f)
        {
            float* d[2] { l, r };
            stft.process (d, n, [] (Stft::cpx**, int, int) {});
        }
        return;
    }

    const int chMode = (int) value (pChMode);
    const int mode = (int) value (pMode);
    const int sBand = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    const int sChan = soloChannel.load (std::memory_order_relaxed);
    const bool chanOn[2] { value (pChanOn[0]) > 0.5f, value (pChanOn[1]) > 0.5f };

    if (mode == Digital)
    {
        updateFilters (false);
        binsSince += n;
        if (binsDirty && binsSince >= 1024) { buildBinGains(); binsSince = 0; }

        // Split into the processing channels, filter in the frequency domain, join.
        for (int i = 0; i < n; ++i)
            ChannelModes::split (chMode, l[i], r[i], l[i], r[i]);
        float* d[2] { l, r };
        stft.process (d, n, [this, chanOn, chMode] (Stft::cpx** bins, int chans, int nb)
        {
            for (int c = 0; c < chans; ++c)
            {
                const int s = ChannelModes::setFor (chMode, c);
                if (! chanOn[s] && chMode != Stereo) continue;
                const float* g = binGain[(size_t) c].data();
                for (int k = 0; k < nb; ++k) bins[c][k] *= g[k];
            }
        });
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            if (chMode != Stereo && sChan >= 0) { if (sChan == 0) b = 0.0f; else a = 0.0f; }
            ChannelModes::join (chMode, a, b, l[i], r[i]);
        }
        return;
    }

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        updateFilters (false);
        if (sBand >= 0)
            for (int c = 0; c < 2; ++c)
            {
                const int s = ChannelModes::setFor (chMode, c);
                const auto& bi = band[s][sBand];
                ch[c].solo.design ((int) value (bi.shape), value (bi.freq), value (bi.q), sr);
            }

        for (int i = start; i < start + len; ++i)
        {
            float a, b;
            ChannelModes::split (chMode, l[i], r[i], a, b);
            float x[2] { a, b };
            for (int c = 0; c < 2; ++c)
            {
                const int s = ChannelModes::setFor (chMode, c);
                if (chMode != Stereo && ! chanOn[s]) continue;
                float v = x[c];
                for (int bnd = 0; bnd < kBands; ++bnd)
                    v = ch[c].bands[bnd].tick (v);
                if (sBand >= 0 && (sSet < 0 || sSet == s || chMode == Stereo))
                    v = ch[c].solo.tick (v);
                x[c] = v;
            }
            if (chMode != Stereo && sChan >= 0) { if (sChan == 0) x[1] = 0.0f; else x[0] = 0.0f; }
            ChannelModes::join (chMode, x[0], x[1], l[i], r[i]);
        }
    }
}

void Equalizer::bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool withAmount) const
{
    const auto& bi = band[set][b];
    if (value (bi.on) < 0.5f)
    {
        for (int i = 0; i < count; ++i) outDb[i] = 0.0f;
        return;
    }
    FilterBand f;
    const int shape = (int) value (bi.shape);
    const float g = withAmount ? effectiveGain (set, b) : (shapeHasGain (shape) ? value (bi.gain) : 0.0f);
    f.design (shape, value (bi.freq), g, value (bi.q), std::min (kNumSlopes - 1, std::max (0, (int) value (bi.slope))), sr);
    for (int i = 0; i < count; ++i)
        outDb[i] = (float) f.responseDb (hz[i], sr);
}

void Equalizer::curveDb (int set, const float* hz, float* outDb, int count, bool withAmount) const
{
    for (int i = 0; i < count; ++i) outDb[i] = 0.0f;
    std::vector<float> tmp ((size_t) count);
    for (int b = 0; b < kBands; ++b)
    {
        bandCurveDb (set, b, hz, tmp.data(), count, withAmount);
        for (int i = 0; i < count; ++i) outDb[i] += tmp[(size_t) i];
    }
}
} // namespace burnish
