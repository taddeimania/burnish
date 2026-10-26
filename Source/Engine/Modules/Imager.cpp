#include "Imager.h"

namespace burnish
{
Imager::Imager()
    : Module ({ "img", "IMAGER", "IMG", 0xffa98dffu, false, false })
{
    pOn      = addBool ("on", "On", true);
    pAmount  = addPct ("amount", "Amount", 0.0f, 100.0f, 100.0f);
    pLink    = addBool ("link", "Link Bands", false);
    pStereoizeOn     = addBool ("stereoize.on", "Stereoize On", false);
    pStereoizeAmount = addPct ("stereoize.amount", "Stereoize Amount", 0.0f, 100.0f, 30.0f);
    pStereoizeMode   = addChoice ("stereoize.mode", "Stereoize Mode", { "Mode I", "Mode II" }, 0);
    pRecoverOn   = addBool ("recover.on", "Recover Sides On", false);
    pRecoverGain = addDb ("recover.gain", "Recover Sides Gain", -24.0f, 12.0f, 0.0f);
    mb.declare (*this, 120.0f, 1000.0f, 6000.0f);
    for (int b = 0; b < kBands; ++b)
        pWidth[b] = addFloat ("band" + std::to_string (b + 1) + ".width", "Band " + std::to_string (b + 1) + " Width", -100.0f, 100.0f, 0.0f, "", 0);
}

void Imager::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    for (auto& s : widthSm) s.setTime (30.0f, (float) sr, kStep);
    stereoizeSm.setTime (30.0f, (float) sr, kStep);
    recoverSm.setTime (30.0f, (float) sr, kStep);
    haasM.prepare ((int) (sr * 0.03));
    apA.allPass (300.0, 0.6, sr);
    apB.allPass (1200.0, 0.6, sr);
    apC.allPass (4000.0, 0.6, sr);
    reset();
}

void Imager::reset()
{
    split.reset();
    haasM.reset();
    apA.reset(); apB.reset(); apC.reset();
    for (auto& v : midMs) v = 0.0f;
    for (auto& v : sideMs) v = 0.0f;
    ll = rr = lr = 0.0f;
}

void Imager::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int nb = (int) value (mb.pNumBands) + 1;
    const int solo = soloBand.load (std::memory_order_relaxed);
    const bool rsolo = recoverSolo.load (std::memory_order_relaxed);
    const float amount = value (pAmount) * 0.01f;
    const bool recoverOn = value (pRecoverOn) > 0.5f;
    const int stMode = (int) value (pStereoizeMode);
    const float haasSamples = (float) (sr * (stMode == 0 ? 0.012 : 0.004));

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]), value (mb.pXover[2]));
        float sideGain[kBands];
        bool anyWider = false, anyNarrower = false;
        for (int b = 0; b < nb; ++b)
        {
            const float w = value (mb.pBandOn[b]) > 0.5f ? value (pWidth[b]) * amount : 0.0f;
            widthSm[b].target = w;
            const float ws = widthSm[b].next();
            sideGain[b] = 1.0f + ws * 0.01f;
            if (ws > 0.5f) anyWider = true;
            if (ws < -0.5f) anyNarrower = true;
        }
        stereoizeSm.target = (value (pStereoizeOn) > 0.5f && anyWider) ? value (pStereoizeAmount) * 0.01f : 0.0f;
        recoverSm.target = (recoverOn && anyNarrower) ? dbToGain (value (pRecoverGain)) : 0.0f;
        const float stAmt = stereoizeSm.next();
        const float recGain = recoverSm.next();

        for (int i = start; i < start + len; ++i)
        {
            float m, s;
            toMidSide (l[i], r[i], m, s);
            float mb_[kBands], sb_[kBands];
            split.split (0, m, mb_);
            split.split (1, s, sb_);
            float outM = 0.0f, outS = 0.0f, recovered = 0.0f;
            for (int b = 0; b < nb; ++b)
            {
                const float sWide = sb_[b] * sideGain[b];
                if (sideGain[b] < 1.0f)
                    recovered += (sb_[b] - sWide);
                midMs[b] += 0.0005f * (mb_[b] * mb_[b] - midMs[b]);
                sideMs[b] += 0.0005f * (sWide * sWide - sideMs[b]);
                if (solo < 0 || solo == b)
                {
                    outM += mb_[b];
                    outS += sWide;
                }
            }
            // Recovered sides come back in the middle, as mono content.
            outM += recovered * recGain * 0.7f;
            if (rsolo)
            {
                outM = recovered * recGain * 0.7f;
                outS = 0.0f;
            }
            // Stereoize: a decorrelated copy of the mid in the side. Mono sum unaffected.
            haasM.push (m);
            if (stAmt > 0.0f)
            {
                float d = haasM.readFrac (haasSamples);
                if (stMode == 1)
                    d = apC.tick (apB.tick (apA.tick (d)));
                outS += d * stAmt * 0.5f;
            }
            float lo, ro;
            toLeftRight (outM, outS, lo, ro);
            l[i] = lo; r[i] = ro;
            ll += 0.001f * (lo * lo - ll);
            rr += 0.001f * (ro * ro - rr);
            lr += 0.001f * (lo * ro - lr);
        }
    }
    for (int b = 0; b < kBands; ++b)
    {
        const float tot = midMs[b] + sideMs[b];
        bandWidth[b] = tot > 1e-9f ? (sideMs[b] - midMs[b]) / tot : 0.0f;
    }
    const float den = std::sqrt (ll * rr);
    correlation = den > 1e-9f ? clampf (lr / den, -1.0f, 1.0f) : 1.0f;
    const float tot = ll + rr;
    balance = tot > 1e-9f ? (rr - ll) / tot : 0.0f;
}
} // namespace burnish
