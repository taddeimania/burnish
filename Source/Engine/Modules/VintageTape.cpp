#include "VintageTape.h"

namespace burnish
{
VintageTape::VintageTape()
    : Module ({ "tap", "VINTAGE TAPE", "TAPE", 0xffc9a77cu, false, false })
{
    pOn        = addBool ("on", "On", true);
    pSpeed     = addChoice ("speed", "Speed", { "7.5 ips", "15 ips", "30 ips" }, 1);
    pDrive     = addDb ("drive", "Input Drive", -10.0f, 10.0f, 0.0f);
    pBias      = addFloat ("bias", "Bias", -10.0f, 10.0f, 0.0f, "", 1);
    pHarmonics = addFloat ("harmonics", "Harmonics", 0.0f, 10.0f, 2.0f, "", 1);
    pLowEmph   = addFloat ("lowemph", "Low Emphasis", 0.0f, 10.0f, 4.0f, "", 1);
    pHighEmph  = addFloat ("highemph", "High Emphasis", 0.0f, 10.0f, 4.0f, "", 1);
}

void VintageTape::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    for (auto& d : offDelay) { d.prepare (16); d.setDelay (15); }
    driveSm.setTime (40.0f, (float) sr, kStep);
    reset();
    last[0] = -1.0f;
    design();
}

void VintageTape::reset()
{
    for (auto& c : ch) c.reset();
    for (auto& d : offDelay) d.reset();
    driveSm.snap (value (pDrive));
    peakIn = peakOut = 0.0f;
}

void VintageTape::design()
{
    const float v[6] { value (pSpeed), value (pDrive), value (pBias), value (pHarmonics), value (pLowEmph), value (pHighEmph) };
    bool changed = false;
    for (int i = 0; i < 6; ++i) if (v[i] != last[i]) { changed = true; last[i] = v[i]; }
    if (! changed) return;
    const int speed = (int) v[0];
    const double bumpHz = speed == 0 ? 90.0 : speed == 1 ? 55.0 : 35.0;
    const double rollHz = speed == 0 ? 11000.0 : speed == 1 ? 16000.0 : 20000.0;
    const double bumpDb = (v[4] - 2.0) * 1.25;             // 0: -2.5 dB (flattened), 4: +2.5, 10: +10
    const double emphDb = (v[5] - 4.0) * 1.2;              // 4: flat; below: roll-off; above: shimmer
    const double tiltDb = -v[2] * 0.4;                     // negative bias: highs driven harder
    for (auto& c : ch)
    {
        c.bump.peak (bumpHz, 1.1, bumpDb, sr);
        c.hfRoll.lowPass (rollHz, 0.6, sr);
        c.hfEmph.highShelf (8000.0, emphDb, sr, 0.6);
        c.preTilt.highShelf (3000.0, tiltDb, sr, 0.7);
        c.postTilt.highShelf (3000.0, -tiltDb, sr, 0.7);
        c.dcBlock.highPass (12.0, 0.7071, sr);
    }
}

void VintageTape::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
    {
        for (int i = 0; i < n; ++i) { l[i] = offDelay[0].tick (l[i]); r[i] = offDelay[1].tick (r[i]); }
        satDb = 0.0f;
        return;
    }
    const float bias = value (pBias);
    const float harm = value (pHarmonics) * 0.03f;
    // Positive bias limits dynamic range: a lower knee into the saturator.
    const float knee = 1.0f - clampf (bias, 0.0f, 10.0f) * 0.05f;
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        design();
        driveSm.target = value (pDrive);
        const float driveDb = driveSm.next();
        const float drive = dbToGain (driveDb) * 0.9f;      // 0 dB drive sits a little under the knee
        const float comp = 1.0f / drive;                     // level compensation
        auto sat = [knee, harm, drive] (float x)
        {
            const float u = x * drive / knee;
            float y = softTanh (u) * knee;
            y += harm * (u * u - 0.5f * std::min (u * u, 1.0f)) * 0.5f;   // even-order
            return y;
        };
        for (int i = start; i < start + len; ++i)
        {
            float x[2] { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                float v = s.preTilt.tick (x[c]);
                peakIn = std::max (peakIn * 0.9995f, std::fabs (v * drive));
                v = oversampled (2, s.os1, s.os2, v, sat);
                peakOut = std::max (peakOut * 0.9995f, std::fabs (v));
                v = s.dcBlock.tick (v) * comp;
                v = s.postTilt.tick (v);
                v = s.hfEmph.tick (s.hfRoll.tick (s.bump.tick (v)));
                x[c] = v;
            }
            l[i] = x[0]; r[i] = x[1];
        }
    }
    satDb = peakIn > 1e-4f ? std::max (0.0f, gainToDb (peakIn) - gainToDb (std::max (peakOut, 1e-6f))) : 0.0f;
}

void VintageTape::curveDb (const float* hz, float* outDb, int count) const
{
    const auto& c = ch[0];
    for (int i = 0; i < count; ++i)
        outDb[i] = (float) (c.bump.responseDb (hz[i], sr) + c.hfRoll.responseDb (hz[i], sr) + c.hfEmph.responseDb (hz[i], sr));
}
} // namespace burnish
