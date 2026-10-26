// Filters.h — one EQ band in every shape the Equalizer offers, as a small
// cascade of biquads, plus its frequency response for drawing and for the
// linear-phase mode.
#pragma once

#include "Dsp.h"

namespace burnish
{
enum FilterShape
{
    Bell = 0, ProportionalQ, BandShelf,
    LowShelfAnalog, LowShelfBaxandall, LowShelfVintage, LowShelfResonant,
    HighShelfAnalog, HighShelfBaxandall, HighShelfVintage, HighShelfResonant,
    HighPassFlat, HighPassResonant, HighPassBrickwall,
    LowPassFlat, LowPassResonant, LowPassBrickwall,
    NumFilterShapes
};

inline const char* filterShapeName (int s)
{
    static const char* names[NumFilterShapes] {
        "Bell", "Proportional Q", "Band Shelf",
        "Low Shelf", "Low Shelf Baxandall", "Low Shelf Vintage", "Low Shelf Resonant",
        "High Shelf", "High Shelf Baxandall", "High Shelf Vintage", "High Shelf Resonant",
        "High Pass", "High Pass Resonant", "High Pass Brickwall",
        "Low Pass", "Low Pass Resonant", "Low Pass Brickwall" };
    return names[std::min (NumFilterShapes - 1, std::max (0, s))];
}
inline const char* filterShapeShort (int s)
{
    static const char* names[NumFilterShapes] {
        "BELL", "PROP Q", "BAND SHELF",
        "LOW SHELF", "LOW BAX", "LOW VINT", "LOW RESO",
        "HIGH SHELF", "HIGH BAX", "HIGH VINT", "HIGH RESO",
        "HP FLAT", "HP RESO", "HP BRICK",
        "LP FLAT", "LP RESO", "LP BRICK" };
    return names[std::min (NumFilterShapes - 1, std::max (0, s))];
}
inline bool shapeIsPass (int s)      { return s >= HighPassFlat; }
inline bool shapeIsHighPass (int s)  { return s >= HighPassFlat && s <= HighPassBrickwall; }
inline bool shapeIsLowPass (int s)   { return s >= LowPassFlat; }
inline bool shapeIsLowShelf (int s)  { return s >= LowShelfAnalog && s <= LowShelfResonant; }
inline bool shapeIsHighShelf (int s) { return s >= HighShelfAnalog && s <= HighShelfResonant; }
inline bool shapeHasGain (int s)     { return ! shapeIsPass (s); }
inline bool shapeHasQ (int s)        { return s != LowShelfBaxandall && s != HighShelfBaxandall && s != HighPassBrickwall && s != LowPassBrickwall; }
inline bool shapeUsesSlope (int s)   { return s == HighPassFlat || s == HighPassResonant || s == LowPassFlat || s == LowPassResonant; }

// Slope choices for pass filters, dB/oct.
inline const float kSlopes[] { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f };
inline constexpr int kNumSlopes = 6;

struct FilterBand
{
    Biquad f1, f2;          // most shapes use one; vintage/band-shelf use two
    FilterCascade cascade;  // pass filters
    int kind = 0;           // 0 none, 1 one biquad, 2 two biquads, 3 cascade
    double gainLin = 1.0;   // for the Baxandall first-order shapes the biquad holds it

    void reset() { f1.reset(); f2.reset(); cascade.reset(); }

    // gain in dB; q is Q for bells/shelves, resonance for resonant passes;
    // slope index 0..5 for flat/resonant passes.
    void design (int shape, double hz, double gainDb, double q, int slopeIdx, double sr)
    {
        q = std::max (0.05, q);
        kind = 1;
        switch (shape)
        {
            case Bell:          f1.peak (hz, q, gainDb, sr); break;
            case ProportionalQ:
            {
                const double qe = q * clampf (0.45f + (float) std::fabs (gainDb) / 9.0f, 0.45f, 3.5f);
                f1.peak (hz, qe, gainDb, sr);
                break;
            }
            case BandShelf:
            {
                // Flat top: a high shelf up at f_lo and the complement down at f_hi.
                const double w = 1.6 / q;   // width in octaves
                const double lo = hz / std::pow (2.0, w * 0.5), hi = hz * std::pow (2.0, w * 0.5);
                f1.highShelf (lo, gainDb, sr, 0.9);
                f2.highShelf (hi, -gainDb, sr, 0.9);
                kind = 2;
                break;
            }
            case LowShelfAnalog:    f1.lowShelf (hz, gainDb, sr, 0.7071); break;
            case LowShelfBaxandall: f1.lowShelf1 (hz, gainDb, sr); break;
            case LowShelfVintage:
                f1.lowShelf (hz, gainDb, sr, 0.7071);
                f2.peak (hz * 2.8, 0.9, -gainDb * 0.4, sr);
                kind = 2;
                break;
            case LowShelfResonant:  f1.lowShelf (hz, gainDb, sr, 0.7071 + q * 0.6); break;
            case HighShelfAnalog:    f1.highShelf (hz, gainDb, sr, 0.7071); break;
            case HighShelfBaxandall: f1.highShelf1 (hz, gainDb, sr); break;
            case HighShelfVintage:
                f1.highShelf (hz, gainDb, sr, 0.7071);
                f2.peak (hz / 2.8, 0.9, -gainDb * 0.4, sr);
                kind = 2;
                break;
            case HighShelfResonant: f1.highShelf (hz, gainDb, sr, 0.7071 + q * 0.6); break;
            case HighPassFlat:      cascade.highPass (hz, sr, slopeToOrder (kSlopes[slopeIdx])); kind = 3; break;
            case HighPassResonant:  cascade.highPass (hz, sr, std::max (2, slopeToOrder (kSlopes[slopeIdx])), 0.5 + q); kind = 3; break;
            case HighPassBrickwall: cascade.highPass (hz, sr, 16); kind = 3; break;
            case LowPassFlat:       cascade.lowPass (hz, sr, slopeToOrder (kSlopes[slopeIdx])); kind = 3; break;
            case LowPassResonant:   cascade.lowPass (hz, sr, std::max (2, slopeToOrder (kSlopes[slopeIdx])), 0.5 + q); kind = 3; break;
            case LowPassBrickwall:  cascade.lowPass (hz, sr, 16); kind = 3; break;
            default: kind = 0; break;
        }
    }

    float tick (float x)
    {
        switch (kind)
        {
            case 1: return f1.tick (x);
            case 2: return f2.tick (f1.tick (x));
            case 3: return cascade.tick (x);
            default: return x;
        }
    }

    double responseDb (double hz, double sr) const
    {
        switch (kind)
        {
            case 1: return f1.responseDb (hz, sr);
            case 2: return f1.responseDb (hz, sr) + f2.responseDb (hz, sr);
            case 3: return cascade.responseDb (hz, sr);
            default: return 0.0;
        }
    }
    void response (double hz, double sr, double& mag2, double& phase) const
    {
        mag2 = 1; phase = 0;
        double m, p;
        switch (kind)
        {
            case 1: f1.response (hz, sr, mag2, phase); break;
            case 2: f1.response (hz, sr, m, p); mag2 = m; phase = p; f2.response (hz, sr, m, p); mag2 *= m; phase += p; break;
            case 3: cascade.response (hz, sr, mag2, phase); break;
            default: break;
        }
    }
};

// Bandpass used to solo a band: the region the band affects.
struct SoloFilter
{
    Biquad bp1, bp2;
    FilterCascade pass;
    int mode = 0;   // 0 bandpass, 1 lowpass, 2 highpass
    void design (int shape, double hz, double q, double sr)
    {
        if (shapeIsLowShelf (shape) || shapeIsLowPass (shape)) { mode = 1; pass.lowPass (hz, sr, 4); }
        else if (shapeIsHighShelf (shape) || shapeIsHighPass (shape)) { mode = 2; pass.highPass (hz, sr, 4); }
        else { mode = 0; bp1.bandPass (hz, std::max (0.3, q), sr); bp2 = bp1; }
    }
    void reset() { bp1.reset(); bp2.reset(); pass.reset(); }
    float tick (float x) { return mode == 0 ? bp2.tick (bp1.tick (x)) : pass.tick (x); }
};
} // namespace burnish
