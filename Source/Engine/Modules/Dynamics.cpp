#include "Dynamics.h"

namespace burnish
{
Dynamics::Dynamics()
    : Module ({ "dyn", "DYNAMICS", "DYN", 0xffffae42u, true, false })
{
    pOn       = addBool ("on", "On", true);
    pChMode   = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pDetect   = addChoice ("detect", "Detection", { "Peak", "Env", "RMS" }, DetectEnv);
    pAutoGain = addBool ("autogain", "Auto Gain", false);
    pAdaptive = addBool ("adaptive", "Adaptive Release", false);
    pLink     = addBool ("link", "Link Bands", false);
    pDetFilter = addChoice ("detfilter", "Detection Filter", { "None", "Highpass", "Tilt" }, DetNone);
    pDetFreq  = addHz ("detfreq", "Detection Freq", 20.0f, 2000.0f, 80.0f);
    pDetSlope = addChoice ("detslope", "Detection Slope", { "6 dB/oct", "12 dB/oct", "24 dB/oct" }, 1);
    pDetTilt  = addFloat ("dettilt", "Detection Tilt", 0.0f, 6.0f, 3.0f, "dB/oct", 1);
    pGlobalGain = addDb ("gain", "Global Gain", -20.0f, 20.0f, 0.0f);
    mb.declare (*this);

    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            auto& bi = band[s][b];
            bi.compThresh = addDb (p + "cthresh", n + "Comp Threshold", -60.0f, 0.0f, -20.0f);
            bi.compRatio  = addFloat (p + "cratio", n + "Comp Ratio", -10.0f, 30.0f, 2.0f, ":1", 1);
            bi.limThresh  = addDb (p + "lthresh", n + "Limit Threshold", -60.0f, 0.0f, 0.0f);
            bi.limRatio   = addFloat (p + "lratio", n + "Limit Ratio", -2.5f, 30.0f, 10.0f, ":1", 1);
            bi.attack     = addMs (p + "attack", n + "Attack", 0.1f, 500.0f, 10.0f);
            bi.release    = addMs (p + "release", n + "Release", 1.0f, 5000.0f, 150.0f);
            bi.knee       = addDb (p + "knee", n + "Knee", 0.0f, 24.0f, 6.0f);
            bi.parallel   = addPct (p + "parallel", n + "Parallel", 0.0f, 100.0f, 0.0f);
            bi.gain       = addDb (p + "gain", n + "Gain", -20.0f, 20.0f, 0.0f);
        }
}

void Dynamics::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    for (auto& row : st)
        for (auto& b : row)
        {
            b.det.prepare ((float) sr);
            b.bal.prepare ((float) sr);
            b.ag.prepare ((float) sr);
            b.fastEnv.set (1.0f, 50.0f, (float) sr);
            b.slowEnv.set (50.0f, 400.0f, (float) sr);
        }
    for (auto& row : gainSm) for (auto& g : row) g.setTime (30.0f, (float) sr, kStep);
    globalSm.setTime (30.0f, (float) sr, kStep);
    scratch.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void Dynamics::reset()
{
    split.reset();
    for (auto& row : st)
        for (auto& b : row) { b.det.reset(); b.bal.reset(); b.ag.reset(); b.fastEnv.reset(); b.slowEnv.reset(); b.crest = 0.0f; }
    for (auto& row : detHp) for (auto& f : row) f.reset();
    for (auto& f : detTiltLo) f.reset();
    for (auto& f : detTiltHi) f.reset();
    for (auto& row : tm) for (auto& t : row) t = {};
    lastDet[0] = -1.0f;
}

float Dynamics::staticGainDb (int set, int b, float levelDb) const
{
    const auto& bi = band[set][b];
    const float cr = value (bi.compRatio), lr = value (bi.limRatio), knee = value (bi.knee);
    float g = 0.0f;
    if (cr >= 1.0f)       g += downwardGainDb (levelDb, value (bi.compThresh), cr, knee);
    else if (cr <= -1.0f) g += upwardGainDb (levelDb, value (bi.compThresh), -cr, knee);
    if (lr >= 1.0f)       g += downwardGainDb (levelDb + g, value (bi.limThresh), lr, knee * 0.5f);
    else if (lr <= -1.0f) g += upwardExpandGainDb (levelDb + g, value (bi.limThresh), -lr, knee * 0.5f);
    return g;
}

void Dynamics::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;

    const int chMode = (int) value (pChMode);
    const int nb = (int) value (mb.pNumBands) + 1;
    const int detMode = (int) value (pDetect);
    const bool autoGain = value (pAutoGain) > 0.5f, adaptive = value (pAdaptive) > 0.5f;
    const int solo = soloBand.load (std::memory_order_relaxed);
    const bool dsolo = detSolo.load (std::memory_order_relaxed);
    const int detFilter = (int) value (pDetFilter);

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);

        // Control-rate updates.
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]), value (mb.pXover[2]));
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            for (int b = 0; b < nb; ++b)
            {
                auto& bs = st[c][b];
                bs.det.mode = detMode;
                float rel = value (band[s][b].release);
                if (adaptive)
                    rel *= clampf (1.6f - bs.crest * 1.2f, 0.35f, 1.6f);   // transients: shorter; sustained: longer
                bs.bal.set (value (band[s][b].attack), rel);
                gainSm[c][b].target = value (band[s][b].gain);
            }
        }
        globalSm.target = value (pGlobalGain);
        {
            const float dv[4] { (float) detFilter, value (pDetFreq), value (pDetSlope), value (pDetTilt) };
            bool ch = false;
            for (int i = 0; i < 4; ++i) if (dv[i] != lastDet[i]) { ch = true; lastDet[i] = dv[i]; }
            if (ch)
                for (int c = 0; c < 2; ++c)
                {
                    const int order = (int) dv[2] == 0 ? 1 : (int) dv[2] == 1 ? 2 : 4;
                    if (order == 1) { detHp[c][0].highPass1 (dv[1], sr); detHp[c][1].flat(); }
                    else if (order == 2) { detHp[c][0].highPass (dv[1], 0.7071, sr); detHp[c][1].flat(); }
                    else { detHp[c][0].highPass (dv[1], 0.5412, sr); detHp[c][1].highPass (dv[1], 1.3066, sr); }
                    detTiltLo[c].lowShelf (1000.0, -dv[3] * 1.6, sr);
                    detTiltHi[c].highShelf (1000.0, dv[3] * 1.6, sr);
                }
        }
        const float gGlobal = dbToGain (globalSm.next());
        float gBand[2][kBands];
        for (int c = 0; c < 2; ++c) for (int b = 0; b < nb; ++b) gBand[c][b] = dbToGain (gainSm[c][b].next());

        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            float bands[2][kBands];
            for (int c = 0; c < 2; ++c)
                split.split (c, x[c], bands[c]);

            float out[2] { 0.0f, 0.0f };
            for (int b = 0; b < nb; ++b)
            {
                const bool bandOn = value (mb.pBandOn[b]) > 0.5f;
                // Detection input: filtered band, per channel; linked in STEREO.
                float detIn[2];
                for (int c = 0; c < 2; ++c)
                {
                    float d = bands[c][b];
                    if (detFilter == DetHighpass) d = detHp[c][1].tick (detHp[c][0].tick (d));
                    else if (detFilter == DetTilt) d = detTiltHi[c].tick (detTiltLo[c].tick (d));
                    detIn[c] = d;
                }
                if (dsolo)
                {
                    for (int c = 0; c < 2; ++c) out[c] += detIn[c];
                    continue;
                }
                float lev[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& bs = st[c][b];
                    lev[c] = bs.det.next (detIn[c]);
                    if (adaptive)
                    {
                        const float ad = std::fabs (detIn[c]);
                        const float f = bs.fastEnv.next (ad), sl = bs.slowEnv.next (ad);
                        bs.crest = clampf (f / (sl + 1e-6f) - 1.0f, 0.0f, 1.0f);
                    }
                }
                if (chMode == Stereo) { const float m = std::max (lev[0], lev[1]); lev[0] = lev[1] = m; }

                for (int c = 0; c < 2; ++c)
                {
                    const int s = ChannelModes::setFor (chMode, c);
                    auto& bs = st[c][b];
                    const float dry = bands[c][b];
                    float wet = dry;
                    float gDb = 0.0f;
                    if (bandOn)
                    {
                        gDb = bs.bal.next (staticGainDb (s, b, lev[c]));
                        wet = dry * dbToGain (gDb);
                        const float par = value (band[s][b].parallel) * 0.01f;
                        wet = wet * (1.0f - par) + dry * par;
                        if (autoGain) { bs.ag.push (dry, wet); wet *= dbToGain (bs.ag.gainDb); }
                        else bs.ag.gainDb = 0.0f;
                        wet *= gBand[c][b];
                    }
                    if (solo < 0 || solo == b)
                        out[c] += wet;
                    if (i == start)
                    {
                        tm[c][b].inDb = lev[c];
                        tm[c][b].gainDb = gDb;
                        tm[c][b].autoDb = bs.ag.gainDb;
                    }
                }
            }
            for (int c = 0; c < 2; ++c) out[c] *= gGlobal;
            ChannelModes::join (chMode, out[0], out[1], l[i], r[i]);
        }
    }
}
} // namespace burnish
