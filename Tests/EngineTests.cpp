// EngineTests.cpp — checks Burnish's signal path without JUCE or a host.
// Run with ctest, or directly: ./BurnishEngineTests
#include "Engine/Chain.h"
#include "Engine/Presets.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace burnish;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL: %s\n", what); }
}
void checkNear (double got, double want, double tol, const char* what)
{
    ++checks;
    if (! (std::fabs (got - want) <= tol)) { ++failures; std::printf ("FAIL: %s (got %.3f, want %.3f +- %.3f)\n", what, got, want, tol); }
}

constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

struct Sine
{
    double phase = 0, inc;
    explicit Sine (double hz) : inc (kTwoPi * hz / kSr) {}
    float next() { const float v = (float) std::sin (phase); phase += inc; if (phase > kTwoPi) phase -= kTwoPi; return v; }
};

// Runs a module on a stereo signal generator; returns RMS of the output after `settle` seconds.
template <typename Gen>
float runRms (Module& m, Gen gen, double seconds, double settle, float* peakOut = nullptr)
{
    const int n = 256;
    std::vector<float> l (n), r (n);
    double sum = 0; long cnt = 0; float pk = 0.0f;
    const long total = (long) (kSr * seconds);
    for (long pos = 0; pos < total; pos += n)
    {
        for (int i = 0; i < n; ++i) { gen (l[i], r[i]); }
        m.process (l.data(), r.data(), n);
        if (pos > kSr * settle)
            for (int i = 0; i < n; ++i) { sum += l[i] * l[i]; ++cnt; pk = std::max (pk, std::fabs (l[i])); }
    }
    if (peakOut) *peakOut = pk;
    return (float) std::sqrt (sum / std::max (1L, cnt));
}

void testBiquads()
{
    Biquad b;
    b.peak (1000.0, 1.0, 6.0, kSr);
    checkNear (b.responseDb (1000.0, kSr), 6.0, 0.05, "peak filter gain at centre");
    checkNear (b.responseDb (50.0, kSr), 0.0, 0.1, "peak filter flat far away");
    b.lowShelf1 (100.0, 6.0, kSr);
    checkNear (b.responseDb (5.0, kSr), 6.0, 0.2, "first-order low shelf at DC");
    checkNear (b.responseDb (10000.0, kSr), 0.0, 0.2, "first-order low shelf at HF");
    b.highShelf1 (5000.0, -6.0, kSr);
    checkNear (b.responseDb (20.0, kSr), 0.0, 0.2, "first-order high shelf at DC");
    checkNear (b.responseDb (20000.0, kSr), -6.0, 0.4, "first-order high shelf at HF");

    FilterCascade c;
    c.highPass (100.0, kSr, 4);
    checkNear (c.responseDb (100.0, kSr), -3.0, 0.3, "4th order Butterworth at cutoff");
    checkNear (c.responseDb (50.0, kSr), -24.0, 1.0, "24 dB/oct an octave below");
    c.lowPass (1000.0, kSr, 16);
    checkNear (c.responseDb (2000.0, kSr), -96.0, 4.0, "brickwall 96 dB/oct");

    // Crossover sums flat.
    Crossover4 x;
    x.prepare (kSr);
    const float f[3] { 120.0f, 1000.0f, 6000.0f };
    x.set (4, f);
    for (double hz : { 60.0, 120.0, 400.0, 1000.0, 3000.0, 6000.0, 12000.0 })
    {
        Sine s (hz);
        x.reset();
        double sum = 0; int cnt = 0;
        for (int i = 0; i < 24000; ++i)
        {
            float bands[4];
            x.tick (s.next(), bands);
            const float y = bands[0] + bands[1] + bands[2] + bands[3];
            if (i > 12000) { sum += y * y; ++cnt; }
        }
        checkNear (20 * std::log10 (std::sqrt (sum / cnt) * std::sqrt (2.0)), 0.0, 0.3, "crossover sums flat");
    }
}

void testFft()
{
    Fft fft;
    fft.prepare (1024);
    std::vector<float> in (1024);
    Sine s (kSr * 8.0 / 1024.0);   // bin 8 exactly
    for (auto& v : in) v = s.next();
    std::vector<Fft::cpx> bins (513);
    fft.forwardReal (in.data(), bins.data());
    checkNear (std::abs (bins[8]), 512.0, 1.0, "FFT bin magnitude");
    checkNear (std::abs (bins[9]), 0.0, 1.0, "FFT leakage");
    std::vector<float> out (1024);
    fft.inverseReal (bins.data(), out.data());
    double err = 0;
    for (int i = 0; i < 1024; ++i) err = std::max (err, (double) std::fabs (out[(size_t) i] - in[(size_t) i]));
    check (err < 1e-4, "FFT round trip");

    // STFT identity.
    Stft st;
    st.prepare (2048, 512, 2);
    std::vector<float> l (4096), r (4096);
    Sine a (440.0), b (3000.0);
    for (int i = 0; i < 4096; ++i) { l[(size_t) i] = a.next(); r[(size_t) i] = b.next(); }
    std::vector<float> ol = l, orr = r;
    float* d[2] { ol.data(), orr.data() };
    for (int pos = 0; pos < 4096; pos += 256)
    {
        float* dd[2] { d[0] + pos, d[1] + pos };
        st.process (dd, 256, [] (Stft::cpx**, int, int) {});
    }
    double e = 0;
    for (int i = 2048 + 600; i < 4096; ++i)
        e = std::max (e, (double) std::fabs (ol[(size_t) i] - l[(size_t) (i - 2048)]));
    check (e < 1e-3, "STFT identity with one-frame latency");
}

void testLoudness()
{
    LoudnessMeter m;
    m.prepare (kSr);
    Sine s (1000.0);
    std::vector<float> l (480), r (480);
    for (int blk = 0; blk < 500; ++blk)
    {
        for (int i = 0; i < 480; ++i) { l[(size_t) i] = r[(size_t) i] = s.next() * 0.5f; }   // -6 dBFS peak sine, both sides
        m.process (l.data(), r.data(), 480);
    }
    // A full-scale sine on both channels reads -3.01 + 3.01 (two channels) = 0 LUFS;
    // at -6 dB peak it's about -6 LUFS. K-weighting at 1 kHz is ~0 dB.
    checkNear (m.integrated(), -6.0, 0.7, "integrated loudness of a -6 dBFS sine");
    checkNear (m.truePeakDb(), -6.02, 0.2, "true peak of a -6 dBFS sine");
}

void testEqualizer()
{
    Equalizer eq (1);
    eq.prepare (kSr, 256);
    eq.setDefaults();
    // Band 2: bell at 1 kHz, +6 dB.
    eq.setValue (eq.band[0][1].freq, 1000.0f);
    eq.setValue (eq.band[0][1].gain, 6.0f);
    eq.setValue (eq.band[0][1].q, 1.0f);
    eq.setValue (eq.band[0][1].shape, (float) Bell);
    Sine s (1000.0);
    const float rms = runRms (eq, [&] (float& l, float& r) { l = r = s.next() * 0.1f; }, 1.0, 0.5);
    checkNear (20 * std::log10 (rms / (0.1f / std::sqrt (2.0f))), 6.0, 0.2, "analog EQ bell +6 dB at 1 kHz");

    // Digital mode: the same, linear phase.
    eq.setValue (eq.pMode, 1.0f);
    eq.reset();
    Sine s2 (1000.0);
    const float rms2 = runRms (eq, [&] (float& l, float& r) { l = r = s2.next() * 0.1f; }, 2.0, 1.0);
    checkNear (20 * std::log10 (rms2 / (0.1f / std::sqrt (2.0f))), 6.0, 0.3, "digital EQ bell +6 dB at 1 kHz");
    check (eq.latency() == 2048, "digital EQ latency is one frame");

    // Amount scales.
    eq.setValue (eq.pMode, 0.0f);
    eq.setValue (eq.pAmount, 50.0f);
    eq.reset();
    Sine s3 (1000.0);
    const float rms3 = runRms (eq, [&] (float& l, float& r) { l = r = s3.next() * 0.1f; }, 1.0, 0.5);
    checkNear (20 * std::log10 (rms3 / (0.1f / std::sqrt (2.0f))), 3.0, 0.2, "EQ amount 50% halves the gain");

    float hz[3] { 100.0f, 1000.0f, 10000.0f }, out[3];
    eq.curveDb (0, hz, out, 3, true);
    checkNear (out[1], 3.0, 0.1, "curveDb with amount");
    eq.curveDb (0, hz, out, 3, false);
    checkNear (out[1], 6.0, 0.1, "curveDb without amount");
}

void testDynamics()
{
    Dynamics d;
    d.prepare (kSr, 256);
    d.setDefaults();
    d.setValue (d.band[0][0].compThresh, -20.0f);
    d.setValue (d.band[0][0].compRatio, 4.0f);
    d.setValue (d.band[0][0].knee, 0.0f);
    d.setValue (d.band[0][0].attack, 1.0f);
    d.setValue (d.band[0][0].release, 50.0f);
    d.setValue (d.pDetect, (float) DetectRms);
    Sine s (1000.0);
    // Input RMS -10 dBFS (peak -7): 10 dB over the threshold at 4:1 -> 7.5 dB reduction.
    const float amp = (float) (std::pow (10.0, -10.0 / 20.0) * std::sqrt (2.0));
    const float rms = runRms (d, [&] (float& l, float& r) { l = r = s.next() * amp; }, 1.0, 0.5);
    checkNear (20 * std::log10 (rms), -17.5, 1.0, "compressor 4:1 reduces 10 dB over by 7.5 dB");

    // Upward: ratio (4):1 raises a quiet signal toward the threshold.
    d.setValue (d.band[0][0].compRatio, -4.0f);
    d.reset();
    Sine s2 (1000.0);
    const float quiet = (float) (std::pow (10.0, -30.0 / 20.0) * std::sqrt (2.0));   // -30 dBFS RMS, 10 under
    const float rms2 = runRms (d, [&] (float& l, float& r) { l = r = s2.next() * quiet; }, 1.0, 0.5);
    checkNear (20 * std::log10 (rms2), -22.5, 1.0, "upward compression (4):1 raises 10 dB under by 7.5 dB");
}

void testMaximizer()
{
    Maximizer m;
    m.prepare (kSr, 256);
    m.setDefaults();
    m.setValue (m.pGain, 12.0f);
    m.setValue (m.pOutput, -1.0f);
    m.setValue (m.pTruePeak, 1.0f);
    for (int mode = 0; mode < Maximizer::NumModes; ++mode)
    {
        m.setValue (m.pMode, (float) mode);
        m.reset();
        // Dense noise-ish signal: sum of sines, peaks near 0 dBFS before gain.
        Sine a (110.0), b (440.0), c (2000.0), e (7000.0);
        float peak = 0.0f;
        const float rms = runRms (m, [&] (float& l, float& r) { l = 0.3f * (a.next() + b.next() + c.next() + e.next()); r = l * 0.8f; }, 1.0, 0.3, &peak);
        char what[64];
        std::snprintf (what, sizeof (what), "maximizer mode %d keeps peaks under -1 dB", mode);
        check (20 * std::log10 (peak) <= -0.9f, what);
        std::snprintf (what, sizeof (what), "maximizer mode %d passes signal", mode);
        check (rms > 0.1f, what);
    }
    check (m.latency() > 0, "maximizer reports latency");

    // Soft clip reduces peaks.
    m.setValue (m.pMode, 1.0f);
    m.setValue (m.pGain, 0.0f);
    m.setValue (m.pOutput, 0.0f);
    m.setValue (m.pClipOn, 1.0f);
    m.setValue (m.pClipAmount, 100.0f);
    m.setValue (m.pClipMode, 2.0f);   // heavy
    m.reset();
    Sine s (1000.0);
    float pk = 0.0f;
    runRms (m, [&] (float& l, float& r) { l = r = s.next() * 0.9f; }, 0.5, 0.2, &pk);
    check (pk < 0.85f, "heavy soft clip rounds a -1 dB sine");
}

void testChain()
{
    Chain chain;
    chain.prepare (kSr, 256);
    for (int i = 0; i < NumModules; ++i) chain.module (i).setDefaults();
    ChainSettings s;
    chain.setOrder ({ ModEq1, ModMaximizer });
    std::vector<float> l (256), r (256);
    Sine a (1000.0);
    for (int blk = 0; blk < 200; ++blk)
    {
        for (int i = 0; i < 256; ++i) l[(size_t) i] = r[(size_t) i] = a.next() * 0.5f;
        chain.process (l.data(), r.data(), 256, s);
    }
    check (chain.latency() == chain.maximizer.latency(), "chain latency sums the modules");
    check (chain.getOrder().size() == 2, "order kept");

    // Bypass returns the input, delayed.
    s.bypass = true;
    Sine b (1000.0);
    double err = 0;
    std::vector<float> ref;
    for (int blk = 0; blk < 100; ++blk)
    {
        for (int i = 0; i < 256; ++i) { l[(size_t) i] = r[(size_t) i] = b.next() * 0.5f; ref.push_back (l[(size_t) i]); }
        chain.process (l.data(), r.data(), 256, s);
        if (blk > 50)
            for (int i = 0; i < 256; ++i)
                err = std::max (err, (double) std::fabs (l[(size_t) i] - ref[(size_t) (blk * 256 + i - chain.latency())]));
    }
    check (err < 1e-3, "bypass plays the delayed input");

    // Every module runs on noise without blowing up, on and off, every order position.
    for (int id = 0; id < NumModules; ++id)
    {
        chain.setOrder ({ id });
        s.bypass = false;
        Noise nz;
        bool finite = true;
        for (int blk = 0; blk < 60; ++blk)
        {
            for (int i = 0; i < 256; ++i) { l[(size_t) i] = nz.next() * 0.5f; r[(size_t) i] = nz.next() * 0.5f; }
            chain.process (l.data(), r.data(), 256, s);
            for (int i = 0; i < 256; ++i) if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) finite = false;
        }
        char what[64];
        std::snprintf (what, sizeof (what), "module %s stays finite on noise", Chain::moduleShortName (id));
        check (finite, what);
    }

    // Factory presets refer to real parameter ids.
    for (const auto& p : factoryPresets())
        for (const auto& [id, v] : p.values)
        {
            bool found = false;
            for (int m = 0; m < NumModules && ! found; ++m)
                if (chain.module (m).indexOf (id) >= 0) found = true;
            char what[128];
            std::snprintf (what, sizeof (what), "preset '%s' id '%s' exists", p.name, id);
            check (found, what);
        }
    for (const auto& p : factoryModulePresets())
        for (const auto& [id, v] : p.values)
        {
            char what[128];
            std::snprintf (what, sizeof (what), "module preset '%s' id '%s' exists", p.name, id);
            check (chain.module (p.module).indexOf (id) >= 0, what);
        }

    // Parameter ids are unique across modules.
    std::vector<std::string> ids;
    for (int m = 0; m < NumModules; ++m)
        for (const auto& d : chain.module (m).params()) ids.push_back (d.id);
    std::sort (ids.begin(), ids.end());
    check (std::adjacent_find (ids.begin(), ids.end()) == ids.end(), "parameter ids unique");
    std::printf ("%d parameters across %d modules\n", (int) ids.size(), (int) NumModules);
}

void testDither()
{
    Dither d;
    d.prepare (kSr);
    Dither::Settings s;
    s.on = true; s.bitDepth = 16; s.amount = Dither::AmountMedium; s.shaping = Dither::ShapeMax;
    std::vector<float> l (4800), r (4800);
    Sine a (1000.0);
    for (auto& v : l) v = a.next() * 0.5f;
    r = l;
    d.process (l.data(), r.data(), 4800, s);
    // Output is on a 16-bit grid.
    bool onGrid = true;
    for (float v : l) if (std::fabs (v * 32768.0f - std::round (v * 32768.0f)) > 1e-3f) onGrid = false;
    check (onGrid, "dither output quantised to 16 bits");
    float pk = 0.0f;
    for (float v : l) pk = std::max (pk, std::fabs (v));
    check (pk < 0.52f, "shaped dither noise stays small");
}

void testAnalysis()
{
    Analyzer an;
    an.prepare (kSr);
    Noise nz;
    Biquad pinkish; pinkish.lowShelf1 (200.0, 12.0, kSr);
    std::vector<float> l (480), r (480);
    for (int blk = 0; blk < 1000; ++blk)   // 10 s
    {
        for (int i = 0; i < 480; ++i) { const float v = pinkish.tick (nz.next()) * 0.2f; l[(size_t) i] = v; r[(size_t) i] = v * 0.5f; }
        an.push (l.data(), r.data(), 480);
    }
    const auto res = an.result();
    check (res.valid, "analysis valid after 10 s");
    check (res.integratedLufs > -40.0f && res.integratedLufs < 0.0f, "analysis loudness plausible");
    check (res.widthDb[0] < 0.0f, "analysis width: mostly mid");
    check (res.spectrumDb[0] > res.spectrumDb[60] - 40.0f, "analysis spectrum filled");
    const Target t = Target::genre (1);
    check (t.name == std::string ("Pop"), "genre target name");
}
} // namespace

int main()
{
    testBiquads();
    testFft();
    testLoudness();
    testEqualizer();
    testDynamics();
    testMaximizer();
    testChain();
    testDither();
    testAnalysis();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
