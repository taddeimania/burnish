// Equalizer.h — up to eight bands in seventeen shapes, ANALOG (minimum
// phase biquads) or DIGITAL (linear phase, through the STFT), in STEREO,
// MID/SIDE or LEFT/RIGHT, with an AMOUNT that scales every band's gain.
#pragma once

#include "../Channels.h"
#include "../Filters.h"
#include "../Module.h"
#include "../Stft.h"

#include <atomic>

namespace burnish
{
class Equalizer final : public Module
{
public:
    static constexpr int kBands = 8;
    enum { Analog = 0, Digital = 1 };

    // instance 1 or 2
    explicit Equalizer (int instance);

    // Parameter indices.
    int pOn, pMode, pChMode, pAmount, pChanOn[2], pLink;
    struct BandIdx { int on, shape, freq, gain, q, slope; };
    BandIdx band[2][kBands];   // [set][band]

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return value (pMode) > 0.5f ? stft.latency() : 0; }

    // UI: solo one band of a set (-1 none), and solo a whole channel (-1 none).
    std::atomic<int> soloBand { -1 }, soloSet { -1 }, soloChannel { -1 };

    // Response of a set at the given frequencies, from the current values
    // (message thread safe: designs scratch filters).
    void curveDb (int set, const float* hz, float* outDb, int count, bool withAmount) const;
    void bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool withAmount) const;

    float effectiveGain (int set, int b) const;   // after AMOUNT, clamped

private:
    static constexpr int kStep = 32;
    void updateFilters (bool force);
    void designBand (FilterBand& f, int set, int b) const;
    void buildBinGains();

    int instanceNo = 1;
    struct ChannelState
    {
        FilterBand bands[kBands];
        SoloFilter solo;
    };
    ChannelState ch[2];
    float lastVals[2][kBands][6] {};
    float lastAmount = -1.0f;
    int   lastMode = -1, lastChMode = -1;
    int   sinceUpdate = 0;

    // Digital mode.
    Stft stft;
    std::vector<float> binGain[2];
    bool binsDirty = true;
    int  binsSince = 0;
    std::vector<float> scratchL, scratchR;
};
} // namespace burnish
