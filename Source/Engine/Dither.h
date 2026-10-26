// Dither.h — word-length reduction for delivery: TPDF dither at the
// chosen amount, noise shaping (error feedback, five profiles), harmonic
// suppression for undithered truncation, peak limiting of the shaped
// noise, auto-blanking in silence, plus the bit meter and the DC meter.
#pragma once

#include "Dsp.h"

#include <array>

namespace burnish
{
class Dither
{
public:
    enum Amount { AmountOff = 0, AmountLow, AmountMedium, AmountStrong };
    enum Shaping { ShapeOff = 0, ShapeLight, ShapeModerate, ShapeHeavy, ShapeMax };

    struct Settings
    {
        bool on = false;
        int bitDepth = 16;          // 24, 20, 16, 12, 8
        bool autoBlank = true;
        int amount = AmountMedium;
        bool harmonicSuppression = false;
        bool limitPeaks = true;
        int shaping = ShapeModerate;
    };

    void prepare (double sampleRate);
    void reset();
    void process (float* l, float* r, int n, const Settings&);

    // Bit meter: bits that toggled recently (bit 23 = MSB), per channel,
    // as 0..1 activity, and the hold.
    float bitActivity[2][24] {};
    float bitHold[2][24] {};
    void resetBitMeter() { for (auto& r : bitHold) for (auto& v : r) v = 0.0f; }

    // DC offset of the input (mean), as a fraction of full scale.
    float dcOffset[2] {};
    void resetDc() { for (auto& v : dcAcc) v = 0.0; dcCount = 0; }

private:
    double sr = 48000.0;
    Noise noise[2];
    std::array<float, 8> err[2] {};
    int errPos[2] {};
    int silentSamples[2] {};
    int lastInt[2] {};
    double dcAcc[2] {};
    long dcCount = 0;
    int meterDecay = 0;
};
} // namespace burnish
