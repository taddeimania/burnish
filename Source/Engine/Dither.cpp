#include "Dither.h"

namespace burnish
{
namespace
{
// Error-feedback noise shaping coefficients (applied to past errors).
const float kShapeLight[]    { 1.0f };
const float kShapeModerate[] { 1.5f, -0.75f };
const float kShapeHeavy[]    { 1.623f, -0.982f, 0.109f };
const float kShapeMax[]      { 2.033f, -2.165f, 1.959f, -1.590f, 0.6149f };
}

void Dither::prepare (double sampleRate)
{
    sr = sampleRate;
    noise[0].s = 0x9e3779b9u;
    noise[1].s = 0x7f4a7c15u;
    reset();
}

void Dither::reset()
{
    for (auto& e : err) e.fill (0.0f);
    errPos[0] = errPos[1] = 0;
    silentSamples[0] = silentSamples[1] = 0;
    lastInt[0] = lastInt[1] = 0;
    for (auto& r : bitActivity) for (auto& v : r) v = 0.0f;
    resetBitMeter();
    resetDc();
}

void Dither::process (float* l, float* r, int n, const Settings& s)
{
    float* ch[2] { l, r };
    const int bits = std::min (24, std::max (8, s.bitDepth));
    const float scale = (float) (1 << (bits - 1));
    const float inv = 1.0f / scale;
    const float ditherLsb = s.amount == AmountLow ? 0.5f : s.amount == AmountMedium ? 1.0f : s.amount == AmountStrong ? 2.0f : 0.0f;
    const float* h = s.shaping == ShapeLight ? kShapeLight : s.shaping == ShapeModerate ? kShapeModerate
                   : s.shaping == ShapeHeavy ? kShapeHeavy : s.shaping == ShapeMax ? kShapeMax : nullptr;
    const int taps = s.shaping == ShapeLight ? 1 : s.shaping == ShapeModerate ? 2 : s.shaping == ShapeHeavy ? 3 : s.shaping == ShapeMax ? 5 : 0;
    const int blankAfter = (int) (0.7 * sr);
    const float silence24 = 1.0f / 8388608.0f;   // one 24-bit LSB

    for (int c = 0; c < 2; ++c)
    {
        float* x = ch[c];
        auto& e = err[(size_t) c];
        int& ep = errPos[c];
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];
            dcAcc[c] += in;

            // Bit meter on the 24-bit input word.
            const int word = (int) std::lround (clampf (std::fabs (in), 0.0f, 1.0f) * 8388607.0f);
            const int toggled = word ^ lastInt[c];
            lastInt[c] = word;
            if (toggled != 0)
                for (int b = 0; b < 24; ++b)
                    if (toggled & (1 << b)) { bitActivity[c][b] = 1.0f; bitHold[c][b] = 1.0f; }

            if (! s.on)
                continue;

            // Auto-blank: silence for 0.7 s mutes the dither noise.
            if (std::fabs (in) < silence24) ++silentSamples[c]; else silentSamples[c] = 0;
            const bool blank = s.autoBlank && silentSamples[c] > blankAfter;

            // Shaped error feedback.
            float shaped = 0.0f;
            for (int k = 0; k < taps; ++k)
                shaped += h[k] * e[(size_t) ((ep + 8 - 1 - k) % 8)];
            const float v = in * scale + shaped;

            float d = 0.0f;
            if (! blank)
            {
                if (ditherLsb > 0.0f)
                    d = 0.5f * ditherLsb * (noise[c].next() + noise[c].next());   // TPDF
                else if (s.harmonicSuppression)
                    d = (i & 1) ? 0.25f : -0.25f;                                   // breaks up truncation patterns
            }
            float q = std::round (v + d);
            if (s.limitPeaks)
                q = clampf (q, -scale, scale - 1.0f);
            e[(size_t) ep] = v - q;   // error of the quantiser (without the dither)
            ep = (ep + 1) % 8;
            x[i] = q * inv;
        }
    }
    dcCount += n;
    if (dcCount > 0)
        for (int c = 0; c < 2; ++c) dcOffset[c] = (float) (dcAcc[c] / (double) dcCount);
    if (++meterDecay >= 2)
    {
        meterDecay = 0;
        for (auto& row : bitActivity) for (auto& v : row) v *= 0.8f;
    }
}
} // namespace burnish
