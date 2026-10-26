// Multiband.h — the band count and crossovers shared by Dynamics, Exciter,
// Imager and Impact: up to four bands, three crossover points, a solo and
// an on/off per band. Modules declare the parameters through it.
#pragma once

#include "Dsp.h"
#include "Module.h"

#include <atomic>

namespace burnish
{
struct MultibandParams
{
    int pNumBands = -1;
    int pXover[3] { -1, -1, -1 };
    int pBandOn[4] { -1, -1, -1, -1 };

    // Call from a module constructor. Declares numBands, three crossovers and
    // four band switches.
    template <typename M>
    void declare (M& m, float f1 = 120.0f, float f2 = 2000.0f, float f3 = 8000.0f)
    {
        pNumBands = m.addChoice ("bands", "Bands", { "1", "2", "3", "4" }, 0);
        pXover[0] = m.addHz ("xover1", "Crossover 1", 20.0f, 20000.0f, f1);
        pXover[1] = m.addHz ("xover2", "Crossover 2", 20.0f, 20000.0f, f2);
        pXover[2] = m.addHz ("xover3", "Crossover 3", 20.0f, 20000.0f, f3);
        for (int b = 0; b < 4; ++b)
            pBandOn[b] = m.addBool ("band" + std::to_string (b + 1) + ".on", "Band " + std::to_string (b + 1) + " On", true);
    }
};

class MultibandSplit
{
public:
    void prepare (double sr) { for (auto& x : xo) x.prepare (sr); last[0] = -1.0f; }
    void reset() { for (auto& x : xo) x.reset(); }

    // Recomputes the crossovers only when something changed.
    void update (int numBands, float f1, float f2, float f3)
    {
        const float v[4] { (float) numBands, f1, f2, f3 };
        bool changed = false;
        for (int i = 0; i < 4; ++i) if (v[i] != last[i]) { changed = true; last[i] = v[i]; }
        if (! changed) return;
        const float f[3] { f1, f2, f3 };
        for (auto& x : xo) x.set (numBands, f);
        bands = std::min (4, std::max (1, numBands));
    }
    int numBands() const noexcept { return bands; }
    void split (int channel, float x, float* out) { xo[(size_t) channel].tick (x, out); }

private:
    Crossover4 xo[2];
    float last[4] { -1.0f, -1.0f, -1.0f, -1.0f };
    int bands = 1;
};
} // namespace burnish
