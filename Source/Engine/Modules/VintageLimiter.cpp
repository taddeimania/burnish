#include "VintageLimiter.h"

namespace burnish
{
VintageLimiter::VintageLimiter()
    : Module ({ "vlm", "VINTAGE LIMITER", "VINT LIM", 0xffff9a6bu, false, false })
{
    pOn        = addBool ("on", "On", true);
    pMode      = addChoice ("mode", "Mode", { "Analog", "Tube", "Modern" }, 0);
    pThreshold = addDb ("threshold", "Threshold", -30.0f, 0.0f, -6.0f);
    pCeiling   = addDb ("ceiling", "Ceiling", -20.0f, 0.0f, -0.3f);
    pLink      = addBool ("link", "Link", false);
    pTruePeak  = addBool ("truepeak", "True Peak", true);
    pCharacter = addFloat ("character", "Character", 0.0f, 10.0f, 5.0f, "", 1);
}

void VintageLimiter::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    for (auto& d : det) { d.prepare ((float) sr); d.mode = DetectEnv; }
    bal.prepare ((float) sr);
    fast.set (0.5f, 40.0f, (float) sr);
    slow.set (40.0f, 400.0f, (float) sr);
    brick.prepare (sr, std::max (4, (int) std::lround (0.0012 * sr)));
    threshSm.setTime (40.0f, (float) sr, kStep);
    ceilSm.setTime (40.0f, (float) sr, kStep);
    for (auto& d : offDelay) { d.prepare (16); d.setDelay (15); }
    reset();
}

void VintageLimiter::reset()
{
    for (auto& d : det) d.reset();
    bal.reset();
    fast.reset(); slow.reset();
    sustain = 0.0f;
    brick.reset();
    for (auto& o : os1) o.reset();
    for (auto& o : os2) o.reset();
    for (auto& d : offDelay) d.reset();
    threshSm.snap (value (pThreshold));
    ceilSm.snap (value (pCeiling));
}

void VintageLimiter::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] = offDelay[0].tick (l[i]);
            r[i] = offDelay[1].tick (r[i]);
            brick.tick (l[i], r[i], 1.0e9f, 0.0f);
        }
        grDb = brickDb = 0.0f;
        return;
    }
    const int mode = (int) value (pMode);
    const float c = value (pCharacter);
    const float ratio = mode == Analog ? 20.0f : mode == Tube ? 8.0f : 12.0f;
    const float knee = mode == Analog ? 3.0f : mode == Tube ? 8.0f : 5.0f;
    const float attack = mode == Analog ? 0.3f : mode == Tube ? 0.5f + c * 1.0f : 0.4f;
    const float relBase = 40.0f * std::pow (10.0f, c * 0.17f);   // 40 ms .. 2 s
    brick.setTruePeak (value (pTruePeak) > 0.5f);
    const float satDrive = mode == Analog ? 0.35f : mode == Tube ? 0.6f : 0.15f;

    float grSum = 0.0f, brickSum = 0.0f;
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        threshSm.target = value (pThreshold);
        ceilSm.target = value (pCeiling);
        const float thresh = threshSm.next(), ceilDb = ceilSm.next();
        const float ceiling = dbToGain (ceilDb);
        const float makeup = dbToGain (ceilDb - thresh);
        bal.set (attack, relBase * (0.4f + 1.2f * sustain));
        brick.setRelease (relBase * 0.25f);

        for (int i = start; i < start + len; ++i)
        {
            float a = l[i], b = r[i];
            const float lev = std::max (det[0].next (a), det[1].next (b));
            const float ad = 0.5f * (std::fabs (a) + std::fabs (b));
            const float f = fast.next (ad), s = slow.next (ad);
            sustain = clampf (1.0f - (f / (s + 1e-6f) - 1.0f), 0.0f, 1.0f);
            const float g = bal.next (downwardGainDb (lev, thresh, ratio, knee));
            const float gl = dbToGain (g) * makeup;
            a *= gl; b *= gl;
            // Rounding, oversampled, scaled to the ceiling.
            auto sat = [satDrive, ceiling] (float x)
            {
                const float u = x / ceiling;
                return ceiling * (u + satDrive * (softTanh (u) - u));
            };
            a = oversampled (2, os1[0], os2[0], a, sat);
            b = oversampled (2, os1[1], os2[1], b, sat);
            brick.tick (a, b, ceiling, 0.0f);
            l[i] = a; r[i] = b;
            grSum += g;
            brickSum += brick.gainReductionDb();
            if (i == start) inDb = lev;
        }
    }
    grDb = -grSum / (float) n;
    brickDb = -brickSum / (float) n;
}
} // namespace burnish
