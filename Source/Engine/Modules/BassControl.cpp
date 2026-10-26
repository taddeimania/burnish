#include "BassControl.h"

namespace burnish
{
BassControl::BassControl()
    : Module ({ "bas", "BASS CONTROL", "BASS", 0xff7cc4ffu, false, false })
{
    pOn        = addBool ("on", "On", true);
    pCutoff    = addHz ("cutoff", "Cutoff", 40.0f, 400.0f, 120.0f);
    pZoom      = addDb ("zoom", "Zoom", 0.0f, 24.0f, 0.0f);
    pBalanceOn = addBool ("balance.on", "Balance On", true);
    pBalance   = addDb ("balance", "Balance", -12.0f, 12.0f, 0.0f);
    pPunchOn   = addBool ("punch.on", "Punch On", true);
    pPunch     = addFloat ("punch", "Punch", -100.0f, 100.0f, 0.0f, "", 0);
    pSustainOn = addBool ("sustain.on", "Sustain Power On", false);
    pSustainThresh = addDb ("sustain.threshold", "Sustain Threshold", -60.0f, 0.0f, -24.0f);
    pSustainChar   = addPct ("sustain.character", "Sustain Character", 0.0f, 100.0f, 50.0f);
    pSustainAmount = addPct ("sustain.amount", "Sustain Amount", 0.0f, 100.0f, 30.0f);
    pPeakOn    = addBool ("peak.on", "Peak Control On", false);
    pPeakThresh = addDb ("peak.threshold", "Peak Threshold", -40.0f, 0.0f, -6.0f);
    pPeakChar  = addPct ("peak.character", "Peak Character", 0.0f, 100.0f, 50.0f);
}

void BassControl::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    const int win = std::max (4, (int) std::lround (0.002 * sr));
    limiter.prepare (sr, win);
    limiter.setRelease (80.0f);
    limiter.setTruePeak (false);
    for (auto& c : ch) { c.trans.prepare ((float) sr); c.restDelay.prepare (limiter.latency() + 1); c.restDelay.setDelay (limiter.latency()); }
    for (auto& d : cleanDelay) { d.prepare (limiter.latency() + 1); d.setDelay (std::max (0, limiter.latency() - 15)); }
    susDet.prepare ((float) sr);
    susDet.mode = DetectRms;
    susAtt = tauCoef (30.0f, (float) sr);
    susRel = tauCoef (250.0f, (float) sr);
    balSm.setTime (30.0f, (float) sr, kStep);
    punchSm.setTime (30.0f, (float) sr, kStep);
    reset();
}

void BassControl::reset()
{
    for (auto& c : ch) c.reset();
    for (auto& d : cleanDelay) d.reset();
    for (auto& o : os1) o.reset();
    for (auto& o : os2) o.reset();
    limiter.reset();
    susDet.reset();
    susGainDb = 0.0f;
    lowMs = restMs = lowPeak = 0.0f;
    lastCutoff = -1.0f;
    balanceDb = 0.0f; crestDb = 10.0f; sustainDb = 0.0f; peakGrDb = 0.0f;
}

void BassControl::process (float* l, float* r, int n)
{
    const bool on = value (pOn) > 0.5f;
    const bool solo = bassSolo.load (std::memory_order_relaxed);
    const bool balanceOn = value (pBalanceOn) > 0.5f, punchOn = value (pPunchOn) > 0.5f;
    const bool sustainOn = value (pSustainOn) > 0.5f, peakOn = value (pPeakOn) > 0.5f;
    const float susThresh = value (pSustainThresh), susAmt = value (pSustainAmount) * 0.01f;
    const float susDirty = 1.0f - value (pSustainChar) * 0.01f;
    const float peakThresh = dbToGain (value (pPeakThresh));
    const float peakDirty = 1.0f - value (pPeakChar) * 0.01f;
    float susSum = 0.0f, grSum = 0.0f;

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        const float cutoff = value (pCutoff);
        if (cutoff != lastCutoff) { lastCutoff = cutoff; for (auto& c : ch) c.split.set (cutoff, sr); }
        balSm.target = on && balanceOn ? value (pBalance) : 0.0f;
        punchSm.target = on && punchOn ? value (pPunch) * 0.01f : 0.0f;
        const float balGain = dbToGain (balSm.next());
        const float punch = punchSm.next();

        for (int i = start; i < start + len; ++i)
        {
            float low[2], rest[2];
            for (int c = 0; c < 2; ++c)
                ch[c].split.tick (c == 0 ? l[i] : r[i], low[c], rest[c]);

            // Meters on the untouched low band.
            const float lm = 0.5f * (low[0] + low[1]);
            lowMs += 0.0005f * (lm * lm - lowMs);
            const float rm = 0.5f * (rest[0] + rest[1]);
            restMs += 0.0005f * (rm * rm - restMs);
            lowPeak = std::max (lowPeak * 0.9998f, std::fabs (lm));

            if (on)
            {
                for (int c = 0; c < 2; ++c)
                {
                    float v = low[c] * balGain;
                    if (punch != 0.0f)
                        v *= 1.0f + punch * 1.2f * ch[c].trans.next (std::fabs (low[c]));
                    low[c] = v;
                }
                // Sustain power: upward compression under the threshold.
                if (sustainOn)
                {
                    const float lev = susDet.next (0.5f * (low[0] + low[1]));
                    const float target = susAmt * clampf (susThresh - lev, 0.0f, 24.0f) * 0.6f
                                       * clampf ((50.0f - (susThresh - lev)) / 20.0f, 0.0f, 1.0f);
                    susGainDb += (target > susGainDb ? susAtt : susRel) * (target - susGainDb);
                    const float g = dbToGain (susGainDb);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float v = low[c] * g;
                        // Dirty: waveshape the boosted band into harmonics that small speakers can hear.
                        const float d = softTanh (v * 2.0f) * 0.5f;
                        ch[c].dc += 0.001f * (d - ch[c].dc);
                        low[c] = v + susDirty * (d - ch[c].dc - v * 0.5f) * clampf (susGainDb / 6.0f, 0.0f, 1.0f);
                    }
                    susSum += susGainDb;
                }
                else susGainDb = 0.0f;
            }

            // Peak control: a clean limiter and a dirty clipper, blended; the
            // rest of the mix is delayed to match either way.
            float a = low[0], b = low[1];
            float ca, cb;
            {
                auto clip = [peakThresh] (float x) { const float u = x / peakThresh; return peakThresh * softTanh (u); };
                const bool useClip = on && peakOn && peakDirty > 0.001f;
                ca = cleanDelay[0].tick (useClip ? oversampled (2, os1[0], os2[0], a, clip) : a);
                cb = cleanDelay[1].tick (useClip ? oversampled (2, os1[1], os2[1], b, clip) : b);
            }
            limiter.tick (a, b, on && peakOn ? peakThresh : 1.0e9f, 0.0f);
            if (on && peakOn)
            {
                a = lerp (a, ca, peakDirty);
                b = lerp (b, cb, peakDirty);
                grSum += limiter.gainReductionDb();
            }
            scope[(size_t) (scopeWrite.load (std::memory_order_relaxed) % kScopeSize)] = 0.5f * (a + b);
            scopeWrite.store (scopeWrite.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);

            const float ra = ch[0].restDelay.tick (rest[0]), rb = ch[1].restDelay.tick (rest[1]);
            l[i] = solo ? a : a + ra;
            r[i] = solo ? b : b + rb;
        }
    }
    balanceDb = (lowMs > 1e-9f && restMs > 1e-9f) ? 10.0f * std::log10 (lowMs / restMs) : 0.0f;
    crestDb = lowMs > 1e-9f ? gainToDb (lowPeak) - 10.0f * std::log10 (lowMs) : 10.0f;
    sustainDb = sustainOn ? susSum / (float) n : 0.0f;
    peakGrDb = (on && peakOn) ? -grSum / (float) n : 0.0f;
}
} // namespace burnish
