#include "VintageEq.h"

namespace burnish
{
const float VintageEq::kLowFreqs[5] { 20.0f, 30.0f, 45.0f, 60.0f, 100.0f };
const float VintageEq::kHiBoostFreqs[7] { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
const float VintageEq::kHiCutFreqs[3] { 5000.0f, 10000.0f, 20000.0f };
const float VintageEq::kLmFreqs[5] { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f };
const float VintageEq::kMidFreqs[11] { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f, 1500.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 7000.0f };
const float VintageEq::kHmFreqs[5] { 1500.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f };

VintageEq::VintageEq()
    : Module ({ "veq", "VINTAGE EQ", "VINT EQ", 0xff8fd3ffu, true, true })
{
    pOn     = addBool ("on", "On", true);
    pChMode = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side", "Left/Right" }, Stereo);
    pChanOn[0] = addBool ("a.on", "A On", true);
    pChanOn[1] = addBool ("b.on", "B On", true);
    for (int s = 0; s < 2; ++s)
    {
        const std::string p = std::string (s == 0 ? "a." : "b.");
        const std::string n = std::string (s == 0 ? "A " : "B ");
        auto& x = set[s];
        x.lowFreq     = addChoice (p + "lowfreq", n + "Low Freq", { "20 Hz", "30 Hz", "45 Hz", "60 Hz", "100 Hz" }, 3);
        x.lowBoost    = addFloat (p + "lowboost", n + "Low Boost", 0.0f, 10.0f, 0.0f, "", 1);
        x.lowCut      = addFloat (p + "lowcut", n + "Low Cut", 0.0f, 10.0f, 0.0f, "", 1);
        x.hiBoostFreq = addChoice (p + "hiboostfreq", n + "High Boost Freq", { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }, 4);
        x.hiBoost     = addFloat (p + "hiboost", n + "High Boost", 0.0f, 10.0f, 0.0f, "", 1);
        x.hiQ         = addFloat (p + "hiq", n + "High Boost Bandwidth", 0.0f, 10.0f, 5.0f, "", 1);
        x.hiCutFreq   = addChoice (p + "hicutfreq", n + "High Cut Freq", { "5 kHz", "10 kHz", "20 kHz" }, 1);
        x.hiCut       = addFloat (p + "hicut", n + "High Cut", 0.0f, 10.0f, 0.0f, "", 1);
        x.lmFreq      = addChoice (p + "lmfreq", n + "Low-Mid Freq", { "200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz" }, 1);
        x.lmBoost     = addFloat (p + "lmboost", n + "Low-Mid Boost", 0.0f, 10.0f, 0.0f, "", 1);
        x.midFreq     = addChoice (p + "midfreq", n + "Mid Freq", { "200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz", "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz", "7 kHz" }, 4);
        x.midCut      = addFloat (p + "midcut", n + "Mid Cut", 0.0f, 10.0f, 0.0f, "", 1);
        x.hmFreq      = addChoice (p + "hmfreq", n + "High-Mid Freq", { "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz" }, 2);
        x.hmBoost     = addFloat (p + "hmboost", n + "High-Mid Boost", 0.0f, 10.0f, 0.0f, "", 1);
    }
}

void VintageEq::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    reset();
    for (int c = 0; c < 2; ++c) design (ch[c], 0);
    lastChMode = -1;
}

void VintageEq::reset()
{
    for (auto& c : ch) c.reset();
    for (auto& row : last) for (auto& v : row) v = -1.0f;
}

void VintageEq::design (Filters& f, int s) const
{
    const auto& x = set[s];
    auto idx = [this] (int p, int count) { return std::min (count - 1, std::max (0, (int) value (p))); };
    const double lowHz = kLowFreqs[idx (x.lowFreq, 5)];
    f.lowBoost.lowShelf (lowHz, value (x.lowBoost) * 1.35, sr, 0.55);
    f.lowCut.lowShelf (lowHz * 1.7, -value (x.lowCut) * 1.7, sr, 0.6);
    const double hbHz = kHiBoostFreqs[idx (x.hiBoostFreq, 7)];
    const double hb = value (x.hiBoost) * 1.8;
    const double q = 0.35 + (10.0 - value (x.hiQ)) * 0.28 + hb * 0.03;   // sharp .. broad, a touch tighter when boosted
    f.hiBoost.peak (hbHz, q, hb, sr);
    f.hiCut.highShelf (kHiCutFreqs[idx (x.hiCutFreq, 3)], -value (x.hiCut) * 1.6, sr, 0.7);
    const double lm = value (x.lmBoost);
    f.lm.peak (kLmFreqs[idx (x.lmFreq, 5)], 0.8 + lm * 0.12, lm, sr);
    const double mc = value (x.midCut);
    f.mid.peak (kMidFreqs[idx (x.midFreq, 11)], 0.9 + mc * 0.15, -mc, sr);
    const double hm = value (x.hmBoost) * 0.8;
    f.hm.peak (kHmFreqs[idx (x.hmFreq, 5)], 0.8 + hm * 0.12, hm, sr);
}

void VintageEq::process (float* l, float* r, int n)
{
    if (value (pOn) < 0.5f)
        return;
    const int chMode = (int) value (pChMode);
    const bool chanOn[2] { value (pChanOn[0]) > 0.5f, value (pChanOn[1]) > 0.5f };
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        for (int c = 0; c < 2; ++c)
        {
            const int s = ChannelModes::setFor (chMode, c);
            const int* idx = &set[s].lowFreq;
            bool ch_ = chMode != lastChMode;
            for (int k = 0; k < 14; ++k)
            {
                const float v = value (idx[k]);
                if (v != last[c][k]) { ch_ = true; last[c][k] = v; }
            }
            if (ch_) design (ch[c], s);
        }
        lastChMode = chMode;
        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            ChannelModes::split (chMode, l[i], r[i], x[0], x[1]);
            for (int c = 0; c < 2; ++c)
                if (chMode == Stereo || chanOn[ChannelModes::setFor (chMode, c)])
                    x[c] = ch[c].tick (x[c]);
            ChannelModes::join (chMode, x[0], x[1], l[i], r[i]);
        }
    }
}

void VintageEq::curveDb (int s, const float* hz, float* outDb, int count) const
{
    Filters f;
    design (f, s);
    for (int i = 0; i < count; ++i)
        outDb[i] = (float) f.responseDb (hz[i], sr);
}
} // namespace burnish
