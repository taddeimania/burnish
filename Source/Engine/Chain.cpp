#include "Chain.h"

namespace burnish
{
namespace
{
const char* kNames[NumModules] {
    "BASS CONTROL", "CLARITY", "DYNAMIC EQ", "DYNAMICS", "EQUALIZER 1", "EQUALIZER 2", "EXCITER", "IMAGER", "IMPACT",
    "LOW END FOCUS", "MATCH EQ", "MAXIMIZER", "SPECTRAL SHAPER", "STABILIZER", "VINTAGE COMPRESSOR",
    "VINTAGE EQ", "VINTAGE LIMITER", "VINTAGE TAPE" };
const char* kShort[NumModules] {
    "BASS", "CLARITY", "DYN EQ", "DYNAMICS", "EQ 1", "EQ 2", "EXCITER", "IMAGER", "IMPACT",
    "LOW END", "MATCH EQ", "MAXIMIZER", "SHAPER", "STABILIZER", "VINT COMP",
    "VINT EQ", "VINT LIM", "TAPE" };
constexpr int kMaxLatency = 8192 * 2;
}

const char* Chain::moduleName (int id) { return kNames[std::min (NumModules - 1, std::max (0, id))]; }
const char* Chain::moduleShortName (int id) { return kShort[std::min (NumModules - 1, std::max (0, id))]; }

Chain::Chain()
{
    mods[ModBassControl] = &bassControl;
    mods[ModClarity] = &clarity;
    mods[ModDynamicEq] = &dynamicEq;
    mods[ModDynamics] = &dynamics;
    mods[ModEq1] = &eq1;
    mods[ModEq2] = &eq2;
    mods[ModExciter] = &exciter;
    mods[ModImager] = &imager;
    mods[ModImpact] = &impact;
    mods[ModLowEndFocus] = &lowEndFocus;
    mods[ModMatchEq] = &matchEq;
    mods[ModMaximizer] = &maximizer;
    mods[ModSpectralShaper] = &spectralShaper;
    mods[ModStabilizer] = &stabilizer;
    mods[ModVintageCompressor] = &vintageCompressor;
    mods[ModVintageEq] = &vintageEq;
    mods[ModVintageLimiter] = &vintageLimiter;
    mods[ModVintageTape] = &vintageTape;
}

void Chain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = maxBlockSize;
    for (auto* m : mods) m->prepare (sr, maxBlock);
    for (auto& d : dryDelay) d.prepare (kMaxLatency);
    for (auto& f : dcHp) f.highPass (1.0, 0.7071, sr);
    dryL.assign ((size_t) maxBlock, 0.0f); dryR = dryL; deltaL = dryL; deltaR = dryL; dlTmp = dryL; drTmp = dryL;
    deltaHistL.assign ((size_t) kMaxLatency, 0.0f); deltaHistR = deltaHistL;
    deltaHistPos = 0;
    matchMeter.prepare (sr);
    matchMeter.setTruePeak (false);
    for (auto& s : inSm) s.setTime (30.0f, (float) sr, 32);
    for (auto& s : outSm) s.setTime (30.0f, (float) sr, 32);
    bypassSm.setTime (20.0f, (float) sr, 32);
    monoSm.setTime (20.0f, (float) sr, 32);
    matchSm.setTime (300.0f, (float) sr, 32);
    inMeter.prepare (sr);
    outMeter.prepare (sr);
    analyser.prepare (sr);
    dither.prepare (sr);
    reset();
    updateLatency();
}

void Chain::reset()
{
    for (auto* m : mods) m->reset();
    for (auto& d : dryDelay) d.reset();
    for (auto& f : dcHp) f.reset();
    inMeter.reset(); outMeter.reset(); matchMeter.reset();
    for (int i = 0; i < NumModules; ++i) { inMs[i] = outMs[i] = 0.0f; diffDb[i] = 0.0f; }
    matchDb = 0.0f;
    matchSm.snap (0.0f);
    bypassSm.snap (0.0f);
    monoSm.snap (0.0f);
    inputTap.clear(); outputTap.clear(); moduleTap.clear();
    clipIn[0] = clipIn[1] = clipOut[0] = clipOut[1] = false;
}

// ---------------------------------------------------------------------------
void Chain::setOrder (const std::vector<int>& ids)
{
    while (orderLock.exchange (true, std::memory_order_acquire)) {}
    orderSharedCount = 0;
    for (int id : ids)
    {
        if (id < 0 || id >= NumModules || orderSharedCount >= kMaxChain) continue;
        bool dup = false;
        for (int i = 0; i < orderSharedCount; ++i) if (orderShared[(size_t) i] == id) dup = true;
        if (! dup) orderShared[(size_t) orderSharedCount++] = id;
    }
    orderLock.store (false, std::memory_order_release);
}

std::vector<int> Chain::getOrder() const
{
    while (orderLock.exchange (true, std::memory_order_acquire)) {}
    std::vector<int> v (orderShared.begin(), orderShared.begin() + orderSharedCount);
    orderLock.store (false, std::memory_order_release);
    return v;
}

bool Chain::inChain (int id) const
{
    for (int i : getOrder()) if (i == id) return true;
    return false;
}

void Chain::updateLatency()
{
    int lat = 0;
    for (int i = 0; i < orderCount; ++i)
        lat += mods[(size_t) order[(size_t) i]]->latency();
    latencyNow.store (std::min (lat, kMaxLatency), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
MeterReadings Chain::takeInputMeters()
{
    MeterReadings r = inRead;
    r.lufsShort = inMeter.shortTerm();
    r.lufsMomentary = inMeter.momentary();
    r.lufsIntegrated = inMeter.integrated();
    r.truePeakDb = inMeter.truePeakDb();
    r.clipped[0] = clipIn[0]; r.clipped[1] = clipIn[1];
    inRead.peak[0] = inRead.peak[1] = 0.0f;
    return r;
}

MeterReadings Chain::takeOutputMeters()
{
    MeterReadings r = outRead;
    r.lufsShort = outMeter.shortTerm();
    r.lufsMomentary = outMeter.momentary();
    r.lufsIntegrated = outMeter.integrated();
    r.truePeakDb = outMeter.truePeakDb();
    r.clipped[0] = clipOut[0]; r.clipped[1] = clipOut[1];
    outRead.peak[0] = outRead.peak[1] = 0.0f;
    return r;
}

void Chain::resetMeters()
{
    inMeter.resetIntegrated();
    outMeter.resetIntegrated();
    clipIn[0] = clipIn[1] = clipOut[0] = clipOut[1] = false;
}

// ---------------------------------------------------------------------------
void Chain::process (float* l, float* r, int n, const ChainSettings& s)
{
    // Pick up a new order if the panel changed it.
    if (! orderLock.exchange (true, std::memory_order_acquire))
    {
        order = orderShared;
        orderCount = orderSharedCount;
        orderLock.store (false, std::memory_order_release);
    }
    updateLatency();
    const int lat = latencyNow.load (std::memory_order_relaxed);

    // Input gain, metering, taps.
    inSm[0].target = s.inGainDb[0]; inSm[1].target = s.inGainDb[1];
    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float g0 = dbToGain (inSm[0].next()), g1 = dbToGain (inSm[1].next());
        for (int i = start; i < start + len; ++i) { l[i] *= g0; r[i] *= g1; }
    }
    inputTap.push (l, r, n);
    inMeter.process (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float al = std::fabs (l[i]), ar = std::fabs (r[i]);
        inRead.peak[0] = std::max (inRead.peak[0], al);
        inRead.peak[1] = std::max (inRead.peak[1], ar);
        inMsM[0] += 0.0003f * (l[i] * l[i] - inMsM[0]);
        inMsM[1] += 0.0003f * (r[i] * r[i] - inMsM[1]);
        if (al > 1.0f) clipIn[0] = true;
        if (ar > 1.0f) clipIn[1] = true;
    }
    inRead.rms[0] = std::sqrt (inMsM[0]); inRead.rms[1] = std::sqrt (inMsM[1]);
    if (s.analysing)
        analyser.push (l, r, n);

    // Dry copy for BYPASS, delayed to line up with the processed signal.
    for (auto& d : dryDelay) d.setDelay (lat);
    for (int i = 0; i < n; ++i)
    {
        dryL[(size_t) i] = dryDelay[0].tick (l[i]);
        dryR[(size_t) i] = dryDelay[1].tick (r[i]);
    }

    // Modules.
    const int tap = tapModule.load (std::memory_order_relaxed);
    bool pastDelta = false, dcDone = false;
    for (int k = 0; k < orderCount; ++k)
    {
        const int id = order[(size_t) k];
        Module& m = *mods[(size_t) id];
        const bool active = (s.soloModule < 0 || s.soloModule == id) && ! pastDelta;

        // DC filter before the first limiter.
        if (s.dcFilter && ! dcDone && (id == ModMaximizer || id == ModVintageLimiter))
        {
            dcDone = true;
            for (int i = 0; i < n; ++i) { l[i] = dcHp[0].tick (l[i]); r[i] = dcHp[1].tick (r[i]); }
        }

        float inSum = 0.0f;
        for (int i = 0; i < n; ++i) inSum += l[i] * l[i] + r[i] * r[i];
        const bool delta = s.deltaModule == id && active;
        if (delta)
        {
            std::copy (l, l + n, deltaL.begin());
            std::copy (r, r + n, deltaR.begin());
        }

        if (active)
            m.process (l, r, n);
        else
        {
            const float was = m.value (0);
            m.setValue (0, 0.0f);
            m.process (l, r, n);
            m.setValue (0, was);
        }

        float outSum = 0.0f;
        for (int i = 0; i < n; ++i) outSum += l[i] * l[i] + r[i] * r[i];
        inMs[id] += 0.05f * (inSum / (float) n - inMs[id]);
        outMs[id] += 0.05f * (outSum / (float) n - outMs[id]);
        diffDb[id] = (active && inMs[id] > 1e-9f && outMs[id] > 1e-9f) ? clampf (10.0f * std::log10 (outMs[id] / inMs[id]), -30.0f, 30.0f) : 0.0f;

        if (delta)
        {
            // The module's own latency separates its input from its output.
            const int ml = std::min (m.latency(), kMaxLatency - 1);
            auto& dl = dlTmp;
            auto& dr = drTmp;
            for (int i = 0; i < n; ++i)
            {
                const int j = i - ml;
                dl[(size_t) i] = j >= 0 ? deltaL[(size_t) j] : deltaHistL[(size_t) ((deltaHistPos + j + 2 * kMaxLatency) % kMaxLatency)];
                dr[(size_t) i] = j >= 0 ? deltaR[(size_t) j] : deltaHistR[(size_t) ((deltaHistPos + j + 2 * kMaxLatency) % kMaxLatency)];
            }
            for (int i = 0; i < n; ++i)
            {
                deltaHistL[(size_t) deltaHistPos] = deltaL[(size_t) i];
                deltaHistR[(size_t) deltaHistPos] = deltaR[(size_t) i];
                deltaHistPos = (deltaHistPos + 1) % kMaxLatency;
                l[i] -= dl[(size_t) i];
                r[i] -= dr[(size_t) i];
            }
            pastDelta = true;
        }
        if (tap == id)
            moduleTap.push (l, r, n);
    }

    // Output gain.
    outSm[0].target = s.outGainDb[0]; outSm[1].target = s.outGainDb[1];
    bypassSm.target = s.bypass ? 1.0f : 0.0f;
    monoSm.target = s.mono ? 1.0f : 0.0f;
    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float g0 = dbToGain (outSm[0].next()), g1 = dbToGain (outSm[1].next());
        for (int i = start; i < start + len; ++i) { l[i] *= g0; r[i] *= g1; }
    }

    // Gain match: bring the processed signal to the input's loudness.
    matchMeter.process (l, r, n);
    {
        const float inL = inMeter.shortTerm(), outL = matchMeter.shortTerm();
        if (inL > -60.0f && outL > -60.0f)
            matchDb = clampf (inL - outL, -24.0f, 24.0f);
    }
    matchSm.target = s.gainMatch ? matchDb : 0.0f;

    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float match = dbToGain (matchSm.next());
        const float byp = bypassSm.next();
        const float mono = monoSm.next();
        for (int i = start; i < start + len; ++i)
        {
            float a = l[i] * match, b = r[i] * match;
            a = lerp (a, dryL[(size_t) i], byp);
            b = lerp (b, dryR[(size_t) i], byp);
            if (s.swap) std::swap (a, b);
            if (mono > 0.0f)
            {
                const float m = 0.5f * (a + b);
                a = lerp (a, m, mono); b = lerp (b, m, mono);
            }
            l[i] = a; r[i] = b;
        }
    }

    // Dither, metering, taps.
    dither.process (l, r, n, s.dither);
    outMeter.process (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float al = std::fabs (l[i]), ar = std::fabs (r[i]);
        outRead.peak[0] = std::max (outRead.peak[0], al);
        outRead.peak[1] = std::max (outRead.peak[1], ar);
        outMsM[0] += 0.0003f * (l[i] * l[i] - outMsM[0]);
        outMsM[1] += 0.0003f * (r[i] * r[i] - outMsM[1]);
        if (al > 1.0f) clipOut[0] = true;
        if (ar > 1.0f) clipOut[1] = true;
    }
    outRead.rms[0] = std::sqrt (outMsM[0]); outRead.rms[1] = std::sqrt (outMsM[1]);
    outputTap.push (l, r, n);
    if (tap < 0)
        moduleTap.push (l, r, n);
}
} // namespace burnish
