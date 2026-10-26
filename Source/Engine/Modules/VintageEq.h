// VintageEq.h — two rows of passive-program-style equalisation: a low
// shelf that can boost and cut at once (the boost sits below the cut, so
// both together give a bump then a dip), a high peak with its own
// bandwidth, a high cut shelf, and three mid peaks whose width follows
// their gain.
#pragma once

#include "../Channels.h"
#include "../Module.h"

namespace burnish
{
class VintageEq final : public Module
{
public:
    VintageEq();

    int pOn, pChMode, pChanOn[2];
    struct SetIdx { int lowFreq, lowBoost, lowCut, hiBoostFreq, hiBoost, hiQ, hiCutFreq, hiCut, lmFreq, lmBoost, midFreq, midCut, hmFreq, hmBoost; };
    SetIdx set[2];

    static const float kLowFreqs[5], kHiBoostFreqs[7], kHiCutFreqs[3], kLmFreqs[5], kMidFreqs[11], kHmFreqs[5];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    // Response of a set, for the curve.
    void curveDb (int s, const float* hz, float* outDb, int count) const;

private:
    static constexpr int kStep = 32;
    struct Filters
    {
        Biquad lowBoost, lowCut, hiBoost, hiCut, lm, mid, hm;
        void reset() { lowBoost.reset(); lowCut.reset(); hiBoost.reset(); hiCut.reset(); lm.reset(); mid.reset(); hm.reset(); }
        float tick (float x) { return hm.tick (mid.tick (lm.tick (hiCut.tick (hiBoost.tick (lowCut.tick (lowBoost.tick (x))))))); }
        double responseDb (double hz, double sr) const
        {
            return lowBoost.responseDb (hz, sr) + lowCut.responseDb (hz, sr) + hiBoost.responseDb (hz, sr) + hiCut.responseDb (hz, sr)
                 + lm.responseDb (hz, sr) + mid.responseDb (hz, sr) + hm.responseDb (hz, sr);
        }
    };
    void design (Filters& f, int s) const;
    Filters ch[2];
    float last[2][14] {};
    int lastChMode = -1;
};
} // namespace burnish
