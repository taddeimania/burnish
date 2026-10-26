#include "LowEndFocus.h"

namespace burnish
{
LowEndFocus::LowEndFocus()
    : Module ({ "lef", "LOW END FOCUS", "LOW END", 0xff6bb8ffu, true, false })
{
    pOn     = addBool ("on", "On", true);
    pChMode = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pMode   = addChoice ("mode", "Mode", { "Punchy", "Smooth" }, 0);
    pContrast[0] = addFloat ("a.contrast", "A Contrast", -100.0f, 100.0f, 0.0f, "", 0);
    pGain[0]     = addDb ("a.gain", "A Gain", -12.0f, 12.0f, 0.0f);
    pContrast[1] = addFloat ("b.contrast", "B Contrast", -100.0f, 100.0f, 0.0f, "", 0);
    pGain[1]     = addDb ("b.gain", "B Gain", -12.0f, 12.0f, 0.0f);
    pLow    = addHz ("low", "Region Low", 20.0f, 300.0f, 30.0f);
    pHigh   = addHz ("high", "Region High", 20.0f, 300.0f, 150.0f);
}

void LowEndFocus::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    for (auto& s : gainSm) s.setTime (30.0f, (float) sr, kStep);
    reset();
}

void LowEndFocus::reset()
{
    for (auto& c : ch) c.reset();
    lastLo = lastHi = -1.0f;
    regionGainDb[0] = regionGainDb[1] = 0.0f;
}

void LowEndFocus::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const bool punchy = (int) value (pMode) == 0;
    const bool solo = regionSolo.load (std::memory_order_relaxed);
    fastCoef = tauCoef (punchy ? 2.0f : 10.0f, (float) sr);
    slowCoef = tauCoef (punchy ? 60.0f : 250.0f, (float) sr);
    gainCoef = tauCoef (punchy ? 3.0f : 15.0f, (float) sr);

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        const float lo = value (pLow), hi = std::max (value (pLow) * 1.1f, value (pHigh));
        if (lo != lastLo || hi != lastHi)
        {
            lastLo = lo; lastHi = hi;
            for (auto& c : ch) { c.lo.set (lo, sr); c.hi.set (hi, sr); c.apLow.set (hi, sr); }
        }
        float contrast[2], makeup[2];
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            contrast[c] = value (pContrast[s]) * 0.01f;
            gainSm[c].target = value (pGain[s]);
            makeup[c] = dbToGain (gainSm[c].next());
        }
        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                float below, above, region, rest;
                s.lo.tick (x[c], below, above);
                s.hi.tick (above, region, rest);
                below = s.apLow.tick (below);
                const float ax = std::fabs (region);
                s.fast += (ax > s.fast ? fastCoef * 3.0f : fastCoef) * (ax - s.fast);
                s.slow += slowCoef * (ax - s.slow);
                const float dev = clampf (gainToDb (s.fast + 1e-6f) - gainToDb (s.slow + 1e-6f), -18.0f, 18.0f);
                const float target = clampf (dev * contrast[c] * 0.7f, -12.0f, 12.0f);
                s.gDb += gainCoef * (target - s.gDb);
                const float g = dbToGain (s.gDb) * makeup[c];
                x[c] = solo ? region : below + region * g + rest;
                if (i == start) regionGainDb[c] = s.gDb + gainSm[c].value;
            }
            ChannelModes::join (chMode, x[0], x[1], l[i], r[i]);
        }
    }
}
} // namespace burnish
