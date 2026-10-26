#include "Presets.h"
#include "Chain.h"

namespace burnish
{
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Default", "An empty chain. Add modules with +, or run ASSIST.", {}, {} },

        { "Clean Master", "Gentle EQ, light glue, true-peak limiting to -1 dBTP: a safe pass for most mixes.",
            { ModEq1, ModDynamics, ModImager, ModMaximizer },
            { { "eq1.a.b1.gain", 1.0f }, { "eq1.a.b4.gain", 1.0f },
              { "dyn.a.band1.cthresh", -18.0f }, { "dyn.a.band1.cratio", 1.5f }, { "dyn.a.band1.attack", 30.0f }, { "dyn.a.band1.release", 300.0f },
              { "img.band1.width", 10.0f },
              { "max.gain", 4.0f }, { "max.output", -1.0f } } },

        { "Loud and Clear", "Clarity and a multiband limiter for a bright, loud master.",
            { ModEq1, ModClarity, ModImager, ModMaximizer },
            { { "clr.amount", 40.0f },
              { "img.bands", 1.0f }, { "img.band1.width", -20.0f }, { "img.band2.width", 15.0f },
              { "max.mode", 5.0f }, { "max.gain", 8.0f }, { "max.output", -1.0f }, { "max.character", 4.0f } } },

        { "Warm Tape", "Tape saturation, a program limiter and a touch of vintage EQ for an analogue feel.",
            { ModVintageEq, ModVintageTape, ModVintageLimiter },
            { { "veq.a.lowboost", 2.0f }, { "veq.a.hiboost", 1.5f },
              { "tap.drive", 2.0f }, { "tap.harmonics", 3.0f },
              { "vlm.threshold", -5.0f }, { "vlm.ceiling", -0.3f } } },

        { "Hip-Hop Low End", "Bass Control keeps the low end tight and loud; Impact adds punch; Maximizer mode V pushes it.",
            { ModBassControl, ModEq1, ModImpact, ModMaximizer },
            { { "bas.punch", 30.0f }, { "bas.sustain.on", 1.0f }, { "bas.sustain.amount", 40.0f }, { "bas.peak.on", 1.0f },
              { "imp.a.band1.impact", 20.0f }, { "imp.a.band2.impact", 25.0f },
              { "max.mode", 5.0f }, { "max.gain", 9.0f }, { "max.output", -1.0f } } },

        { "Electronic Club", "Wide tops, mono bass, dense dynamics and a loud, transient-preserving limit.",
            { ModEq1, ModDynamics, ModExciter, ModImager, ModMaximizer },
            { { "dyn.bands", 2.0f }, { "dyn.a.band1.cthresh", -20.0f }, { "dyn.a.band1.cratio", 2.0f },
              { "exc.a.band1.amount", 15.0f }, { "exc.a.band1.mode", 2.0f },
              { "img.bands", 2.0f }, { "img.band1.width", -100.0f }, { "img.band2.width", 10.0f }, { "img.band3.width", 25.0f },
              { "max.mode", 2.0f }, { "max.gain", 10.0f }, { "max.output", -0.5f }, { "max.character", 3.0f },
              { "max.trans.on", 1.0f }, { "max.trans.amount", 40.0f } } },

        { "Rock Glue", "Vintage compression for glue, an exciter for bite, a limiter with some character.",
            { ModEq1, ModVintageCompressor, ModExciter, ModMaximizer },
            { { "vcp.a.threshold", -15.0f }, { "vcp.a.ratio", 2.5f }, { "vcp.a.attack", 20.0f }, { "vcp.a.release", 250.0f }, { "vcp.autogain", 1.0f },
              { "exc.a.band1.mode", 0.0f }, { "exc.a.band1.amount", 12.0f },
              { "max.mode", 3.0f }, { "max.style3", 1.0f }, { "max.gain", 7.0f }, { "max.output", -1.0f } } },

        { "Acoustic and Vocal", "Light touch: a dynamic EQ to tame harshness, Stabilizer to balance, gentle limiting.",
            { ModDynamicEq, ModStabilizer, ModMaximizer },
            { { "deq.a.b2.freq", 3000.0f }, { "deq.a.b2.gain", -4.0f }, { "deq.a.b2.threshold", -24.0f }, { "deq.a.b2.q", 2.0f },
              { "stb.amount", 35.0f }, { "stb.target", 7.0f },
              { "max.mode", 1.0f }, { "max.gain", 3.0f }, { "max.output", -1.0f }, { "max.character", 7.0f } } },

        { "Streaming Ready", "Aimed at -14 LUFS with a -1 dBTP ceiling, as streaming services expect.",
            { ModEq1, ModDynamics, ModMaximizer },
            { { "dyn.a.band1.cthresh", -16.0f }, { "dyn.a.band1.cratio", 1.3f },
              { "max.gain", 5.0f }, { "max.output", -1.0f }, { "max.target", -14.0f }, { "max.truepeak", 1.0f } } },

        { "De-harsh", "Spectral Shaper and Low End Focus to clean a mix before anything else.",
            { ModLowEndFocus, ModSpectralShaper, ModEq1 },
            { { "lef.a.contrast", 30.0f }, { "shp.amount", 50.0f }, { "shp.mode", 1.0f } } },

        { "Vinyl Pre-Master", "Mono bass, no clipping, moderate level: something a cutting engineer can work with.",
            { ModEq1, ModImager, ModVintageLimiter },
            { { "img.bands", 1.0f }, { "img.xover1", 150.0f }, { "img.band1.width", -100.0f },
              { "vlm.mode", 2.0f }, { "vlm.threshold", -3.0f }, { "vlm.ceiling", -1.0f }, { "vlm.character", 6.0f } } },

        { "Reference Match", "Match EQ against a captured reference, then a clean limit.",
            { ModMatchEq, ModMaximizer },
            { { "mtc.amount", 40.0f }, { "mtc.smoothing", 60.0f }, { "max.gain", 4.0f }, { "max.output", -1.0f } } },
    };
    return list;
}

const std::vector<ModulePreset>& factoryModulePresets()
{
    static const std::vector<ModulePreset> list {
        { ModEq1, "Flat", "Four bands on, nothing moved.", {} },
        { ModEq1, "Smile", "A little low end and air, a dip in the low mids.",
            { { "eq1.a.b1.gain", 1.5f }, { "eq1.a.b2.gain", -1.0f }, { "eq1.a.b2.freq", 300.0f }, { "eq1.a.b4.gain", 1.5f } } },
        { ModEq1, "Clear the Mud", "Cut 200-400 Hz, lift presence.",
            { { "eq1.a.b2.gain", -2.0f }, { "eq1.a.b2.freq", 280.0f }, { "eq1.a.b2.q", 0.8f }, { "eq1.a.b3.gain", 1.0f }, { "eq1.a.b3.freq", 3000.0f } } },
        { ModEq1, "Rumble Filter", "A 30 Hz high-pass and nothing else.",
            { { "eq1.a.b1.shape", (float) HighPassFlat }, { "eq1.a.b1.freq", 30.0f }, { "eq1.a.b1.slope", 3.0f } } },
        { ModDynamics, "Gentle Glue", "One band, 1.5:1, slow.", { { "dyn.a.band1.cthresh", -18.0f }, { "dyn.a.band1.cratio", 1.5f }, { "dyn.a.band1.attack", 30.0f }, { "dyn.a.band1.release", 300.0f } } },
        { ModDynamics, "Three Band Control", "Bass firm, mids open, highs smooth.",
            { { "dyn.bands", 2.0f }, { "dyn.xover1", 150.0f }, { "dyn.xover2", 3000.0f },
              { "dyn.a.band1.cthresh", -20.0f }, { "dyn.a.band1.cratio", 3.0f }, { "dyn.a.band1.attack", 20.0f }, { "dyn.a.band1.release", 200.0f },
              { "dyn.a.band2.cthresh", -16.0f }, { "dyn.a.band2.cratio", 1.5f },
              { "dyn.a.band3.cthresh", -18.0f }, { "dyn.a.band3.cratio", 2.0f }, { "dyn.a.band3.attack", 5.0f }, { "dyn.a.band3.release", 80.0f } } },
        { ModMaximizer, "Transparent", "Mode III balanced, true peak, -1 dB.", { { "max.mode", 3.0f }, { "max.style3", 2.0f }, { "max.character", 5.0f }, { "max.output", -1.0f } } },
        { ModMaximizer, "Loud", "Mode V, soft clip, fast character.", { { "max.mode", 5.0f }, { "max.character", 2.5f }, { "max.clip.on", 1.0f }, { "max.clip.amount", 40.0f }, { "max.output", -0.5f }, { "max.gain", 8.0f } } },
        { ModMaximizer, "Low Latency", "For tracking through it.", { { "max.mode", 0.0f }, { "max.character", 4.0f } } },
        { ModImager, "Mono Bass", "Everything under 150 Hz in the middle.", { { "img.bands", 1.0f }, { "img.xover1", 150.0f }, { "img.band1.width", -100.0f } } },
        { ModImager, "Wide Highs", "A little more air out to the sides.", { { "img.bands", 1.0f }, { "img.xover1", 4000.0f }, { "img.band2.width", 25.0f } } },
        { ModExciter, "Tape Warmth", "Tape mode, light, on the whole band.", { { "exc.a.band1.mode", 2.0f }, { "exc.a.band1.amount", 15.0f } } },
        { ModExciter, "Tube Sparkle", "Triode on the highs only.", { { "exc.bands", 1.0f }, { "exc.xover1", 3000.0f }, { "exc.a.band1.amount", 0.0f }, { "exc.a.band2.mode", 5.0f }, { "exc.a.band2.amount", 20.0f } } },
        { ModVintageTape, "15 ips Gentle", "A touch of tape.", { { "tap.speed", 1.0f }, { "tap.drive", 1.0f } } },
        { ModVintageTape, "7.5 ips Thick", "Slow tape, big head bump.", { { "tap.speed", 0.0f }, { "tap.drive", 4.0f }, { "tap.lowemph", 7.0f }, { "tap.harmonics", 4.0f } } },
        { ModBassControl, "Tighten", "Peak control and a little punch.", { { "bas.punch", 25.0f }, { "bas.peak.on", 1.0f }, { "bas.peak.threshold", -6.0f } } },
        { ModBassControl, "Fill Out", "Sustain power for a thin low end.", { { "bas.sustain.on", 1.0f }, { "bas.sustain.amount", 50.0f }, { "bas.sustain.threshold", -20.0f } } },
    };
    return list;
}
} // namespace burnish
