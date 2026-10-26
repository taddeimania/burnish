#include "VintageCompressor.h"

namespace burnish
{
VintageCompressor::VintageCompressor()
    : Module ({ "vcp", "VINTAGE COMPRESSOR", "VINT COMP", 0xffffc46bu, true, false })
{
    pOn       = addBool ("on", "On", true);
    pChMode   = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pMode     = addChoice ("mode", "Mode", { "Sharp", "Balanced", "Smooth" }, 1);
    pAutoGain = addBool ("autogain", "Auto Gain", false);
    pDetType  = addChoice ("det.type", "Detection Filter", { "None", "Highpass", "High Shelf", "Wide Boost" }, 0);
    pDetFreq  = addHz ("det.freq", "Detection Freq", 20.0f, 10000.0f, 100.0f);
    pDetGain  = addDb ("det.gain", "Detection Gain", 0.0f, 12.0f, 6.0f);
    for (int s = 0; s < 2; ++s)
    {
        const std::string p = std::string (s == 0 ? "a." : "b.");
        const std::string n = std::string (s == 0 ? "A " : "B ");
        set[s].threshold = addDb (p + "threshold", n + "Threshold", -40.0f, 0.0f, -12.0f);
        set[s].ratio     = addFloat (p + "ratio", n + "Ratio", 1.0f, 20.0f, 3.0f, ":1", 1, 0.0f);
        set[s].attack    = addMs (p + "attack", n + "Attack", 0.1f, 200.0f, 10.0f);
        set[s].release   = addMs (p + "release", n + "Release", 10.0f, 2000.0f, 150.0f);
        set[s].gain      = addDb (p + "gain", n + "Gain", -12.0f, 12.0f, 0.0f);
    }
}

void VintageCompressor::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    for (auto& c : ch)
    {
        c.det.prepare ((float) sr);
        c.bal.prepare ((float) sr);
        c.ag.prepare ((float) sr);
        c.fast.set (1.0f, 60.0f, (float) sr);
        c.slow.set (60.0f, 500.0f, (float) sr);
    }
    for (auto& s : gainSm) s.setTime (30.0f, (float) sr, kStep);
    reset();
}

void VintageCompressor::reset()
{
    for (auto& c : ch)
    {
        c.det.reset(); c.bal.reset(); c.ag.reset(); c.fast.reset(); c.slow.reset();
        c.detF1.reset(); c.detF2.reset();
        c.lastOut = 0.0f; c.inDb = -90.0f; c.grDb = 0.0f;
    }
    lastDet[0] = -1.0f;
}

void VintageCompressor::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const int mode = (int) value (pMode);
    const bool autoGain = value (pAutoGain) > 0.5f;
    const bool dsolo = detSolo.load (std::memory_order_relaxed);
    const float knee = mode == 0 ? 2.0f : mode == 1 ? 6.0f : 12.0f;

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        const float dv[3] { value (pDetType), value (pDetFreq), value (pDetGain) };
        if (dv[0] != lastDet[0] || dv[1] != lastDet[1] || dv[2] != lastDet[2])
        {
            lastDet[0] = dv[0]; lastDet[1] = dv[1]; lastDet[2] = dv[2];
            for (auto& c : ch)
            {
                const int t = (int) dv[0];
                if (t == 1)      { c.detF1.highPass (dv[1], 0.7071, sr); c.detF2.flat(); }
                else if (t == 2) { c.detF1.highShelf (dv[1], dv[2], sr); c.detF2.flat(); }
                else if (t == 3) { c.detF1.peak (dv[1], 0.5, dv[2], sr); c.detF2.flat(); }
                else             { c.detF1.flat(); c.detF2.flat(); }
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            auto& st = ch[c];
            st.det.mode = mode == 0 ? DetectPeak : mode == 1 ? DetectEnv : DetectRms;
            // Transient release as set; sustained compression releases slower.
            const float rel = value (set[s].release) * (1.0f + 3.0f * st.sustain);
            st.bal.set (value (set[s].attack) * (mode == 2 ? 2.0f : 1.0f), rel);
            gainSm[c].target = value (set[s].gain);
        }
        float makeup[2] { dbToGain (gainSm[0].next()), dbToGain (gainSm[1].next()) };

        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            float lev[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& st = ch[c];
                // Feedback: the detector listens to the last output sample.
                const float d = st.detF2.tick (st.detF1.tick (st.lastOut));
                lev[c] = st.det.next (d);
                const float ad = std::fabs (d);
                const float f = st.fast.next (ad), sl = st.slow.next (ad);
                st.sustain = clampf (1.0f - (f / (sl + 1e-6f) - 1.0f), 0.0f, 1.0f);
                if (dsolo) x[c] = d;
            }
            if (chMode == Stereo) { const float m = std::max (lev[0], lev[1]); lev[0] = lev[1] = m; }
            for (int c = 0; c < 2; ++c)
            {
                const int s = ChannelModes::setFor (chMode, c);
                auto& st = ch[c];
                const float g = st.bal.next (downwardGainDb (lev[c], value (set[s].threshold), value (set[s].ratio), knee));
                float y = x[c] * dbToGain (g);
                if (autoGain) { st.ag.push (x[c], y); y *= dbToGain (st.ag.gainDb); }
                else st.ag.gainDb = 0.0f;
                y *= makeup[c];
                st.lastOut = dsolo ? x[c] : y;
                x[c] = dsolo ? x[c] : y;
                if (i == start) { st.inDb = lev[c]; st.grDb = g; }
            }
            ChannelModes::join (chMode, x[0], x[1], l[i], r[i]);
        }
    }
}
} // namespace burnish
