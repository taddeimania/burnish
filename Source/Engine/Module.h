// Module.h — what every Burnish processing module has in common: a list of
// parameter descriptions (the plugin builds host parameters from them), a
// plain float array holding the current values, and a stereo in-place
// process call. No JUCE, so the tests drive modules directly.
#pragma once

#include "Dsp.h"

#include <string>
#include <vector>

namespace burnish
{
enum class ParamKind { Float, Bool, Choice };

struct ParamDesc
{
    std::string id;        // host id, e.g. "eq1.a.b2.freq"
    std::string name;      // host name, e.g. "EQ1 A Band 2 Freq"
    ParamKind kind = ParamKind::Float;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    float skew = 1.0f;     // >1 gives more travel to small values (log-like); 0 = true log
    std::string unit;      // "dB", "Hz", "ms", "%", ":1"
    int decimals = 1;
    std::vector<std::string> choices;
    bool automatable = true;
};

// Channel processing modes a module can offer.
enum ChannelMode { Stereo = 0, MidSide = 1, LeftRight = 2 };

// Which bits of a module to show, for the panel.
struct ModuleInfo
{
    std::string id;          // "eq1"
    std::string name;        // "EQUALIZER 1"
    std::string shortName;   // "EQ 1"
    unsigned colour = 0xffffffff;
    bool hasMidSide = false, hasLeftRight = false;
};

class Module
{
public:
    virtual ~Module() = default;

    const ModuleInfo& info() const noexcept { return meta; }
    const std::vector<ParamDesc>& params() const noexcept { return descs; }
    int numParams() const noexcept { return (int) descs.size(); }

    // The live values, in the order of params(). The plugin writes them
    // from its host parameters before each block; tests write them directly.
    float* values() noexcept { return vals.data(); }
    float  value (int i) const noexcept { return vals[(size_t) i]; }
    void   setValue (int i, float v) noexcept { vals[(size_t) i] = v; }
    int    indexOf (const std::string& id) const;

    // Resets every value to its default.
    void setDefaults();

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* l, float* r, int n) = 0;
    virtual int  latency() const { return 0; }

    // Hint for the chain's "difference meter": the gain change the module
    // applied this block, in dB, if it knows better than output/input RMS.
    virtual bool reportsGain (float&) const { return false; }

    // Only valid after prepare().
    double sampleRate() const noexcept { return sr; }

    // Declaring parameters (constructors only). Each returns the index in values().
    int addFloat (const std::string& id, const std::string& name, float lo, float hi, float def,
                  const std::string& unit = {}, int decimals = 1, float skew = 1.0f);
    int addDb (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "dB", 1); }
    int addHz (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "Hz", 0, 0.0f); }
    int addMs (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "ms", 1, 0.0f); }
    int addPct (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "%", 0); }
    int addBool (const std::string& id, const std::string& name, bool def);
    int addChoice (const std::string& id, const std::string& name, std::vector<std::string> choices, int def);

    std::string pid (const std::string& s) const { return meta.id + "." + s; }
    std::string pname (const std::string& s) const { return meta.shortName + " " + s; }

protected:
    Module (ModuleInfo m) : meta (std::move (m)) {}

    double sr = 48000.0;
    int maxBlockSize = 512;

private:
    ModuleInfo meta;
    std::vector<ParamDesc> descs;
    std::vector<float> vals;
};

// Converts between dB/octave slope and a Butterworth order.
inline int slopeToOrder (float dbPerOct) { return std::max (1, std::min (8, (int) std::lround (dbPerOct / 6.0f))); }
} // namespace burnish
