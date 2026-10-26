#include "Spectral.h"

namespace burnish
{
// ===========================================================================
// Tonal targets
// ===========================================================================
namespace
{
// Control points at 20, 40, 80, 160, 315, 630, 1.25k, 2.5k, 5k, 10k, 20k Hz,
// as dB from pink noise (flat per sixth-octave band). Shapes typical of
// released music in each style; the assistant and Stabilizer aim at them.
const float kTargetPoints[TonalTargets::kNumTargets][11] {
    { -6.0f,  1.0f,  3.0f,  1.0f,  0.0f, -1.0f, -2.0f, -3.0f, -5.0f,  -8.0f, -16.0f },   // All-purpose
    { -8.0f,  0.0f,  2.0f,  1.0f,  0.0f, -1.0f, -1.5f, -2.0f, -3.5f,  -6.0f, -14.0f },   // Pop
    { -10.0f, -2.0f, 1.0f,  1.0f,  0.5f,  0.0f, -1.0f, -2.0f, -3.0f,  -6.0f, -14.0f },   // Rock
    { -3.0f,  3.0f,  4.0f,  1.0f, -0.5f, -2.0f, -3.0f, -4.0f, -5.5f,  -8.0f, -16.0f },   // Hip-Hop
    { -4.0f,  2.0f,  4.0f,  1.0f, -1.0f, -2.0f, -3.0f, -3.5f, -4.5f,  -6.5f, -13.0f },   // Electronic
    { -5.0f,  2.0f,  3.0f,  1.0f, -0.5f, -1.5f, -2.5f, -3.5f, -5.0f,  -8.0f, -15.0f },   // R&B
    { -9.0f, -1.0f,  1.0f,  1.0f,  0.0f, -0.5f, -1.0f, -2.0f, -3.5f,  -6.5f, -14.0f },   // Indie
    { -12.0f, -4.0f, 0.0f,  1.0f,  1.0f,  0.0f, -1.0f, -2.0f, -4.0f,  -7.0f, -15.0f },   // Folk / Acoustic
    { -12.0f, -3.0f, 1.0f,  1.0f,  0.5f,  0.0f, -1.5f, -3.0f, -5.0f,  -9.0f, -17.0f },   // Jazz
    { -14.0f, -5.0f, -1.0f, 0.0f,  1.0f,  1.0f,  0.0f, -2.0f, -5.0f, -10.0f, -20.0f },   // Classical
    { -4.0f,  1.0f,  2.0f,  0.0f,  0.0f, -0.5f, -1.5f, -3.0f, -5.0f,  -8.0f, -16.0f },   // Cinematic
};
const char* kTargetNames[TonalTargets::kNumTargets] {
    "All-purpose", "Pop", "Rock", "Hip-Hop", "Electronic", "R&B", "Indie", "Folk", "Jazz", "Classical", "Cinematic" };
} // namespace

const char* TonalTargets::name (int t) { return kTargetNames[std::min (kNumTargets - 1, std::max (0, t))]; }

float TonalTargets::curveDb (int target, int band)
{
    target = std::min (kNumTargets - 1, std::max (0, target));
    band = std::min (kNumBands - 1, std::max (0, band));
    const float pos = (float) band / 6.0f;        // octaves above 20 Hz
    const int i = std::min (9, (int) pos);
    const float f = pos - (float) i;
    const float* p = kTargetPoints[target];
    return p[i] + (p[i + 1] - p[i]) * f;
}

// ===========================================================================
// SpectralBase
// ===========================================================================
void SpectralBase::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    stft.prepare (kFrame, kHop, 2);
    bands.prepare (TonalTargets::kBandsPerOctave, kFrame, sr);
    for (auto& g : binGain) g.assign ((size_t) stft.numBins(), 1.0f);
    energies.assign ((size_t) std::max (bands.count, kBands), 0.0f);
    reset();
}

void SpectralBase::reset()
{
    stft.reset();
    for (auto& g : binGain) std::fill (g.begin(), g.end(), 1.0f);
    for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
    for (auto& row : specDb) for (auto& v : row) v = -60.0f;
}

void SpectralBase::onFrame (Stft::cpx** bins, int chans, int nb)
{
    const int chMode = pChMode >= 0 ? (int) value (pChMode) : Stereo;
    frameUpdate();
    float frameDb[2][kBands];
    const int nbands = std::min (bands.count, kBands);
    for (int c = 0; c < chans; ++c)
    {
        bands.energies (bins[c], energies.data());
        for (int b = 0; b < nbands; ++b)
            frameDb[c][b] = 10.0f * std::log10 (energies[(size_t) b] + 1e-12f);
        for (int b = nbands; b < kBands; ++b) frameDb[c][b] = -120.0f;
    }
    if (chMode == Stereo)
    {
        // One analysis of both sides, applied to both.
        for (int b = 0; b < kBands; ++b)
            frameDb[0][b] = 10.0f * std::log10 (0.5f * (std::pow (10.0f, frameDb[0][b] * 0.1f) + std::pow (10.0f, frameDb[1][b] * 0.1f)) + 1e-12f);
        computeGains (0, frameDb[0], gainDb[0]);
        for (int b = 0; b < kBands; ++b) gainDb[1][b] = gainDb[0][b];
    }
    else
    {
        computeGains (0, frameDb[0], gainDb[0]);
        computeGains (1, frameDb[1], gainDb[1]);
    }

    // Band gains -> bin gains, interpolated in log frequency between band centres.
    for (int c = 0; c < chans; ++c)
    {
        auto& g = binGain[(size_t) c];
        const float* gd = gainDb[c];
        for (int k = 0; k < nb; ++k)
        {
            const float hz = Stft::binHz (k, kFrame, sr);
            float db;
            if (hz <= 20.0f) db = gd[0];
            else if (hz >= 20000.0f) db = gd[kBands - 1];
            else
            {
                const float pos = std::log2 (hz / 20.0f) * (float) TonalTargets::kBandsPerOctave;
                const int i = std::min (kBands - 2, (int) pos);
                const float f = pos - (float) i;
                db = gd[i] + (gd[i + 1] - gd[i]) * f;
            }
            g[(size_t) k] = dbToGain (db);
        }
        for (int k = 0; k < nb; ++k)
            bins[c][k] *= g[(size_t) k];
    }
}

void SpectralBase::process (float* l, float* r, int n)
{
    const int chMode = pChMode >= 0 ? (int) value (pChMode) : Stereo;
    const bool on = value (pOn) > 0.5f;
    for (int i = 0; i < n; ++i)
        ChannelModes::split (chMode, l[i], r[i], l[i], r[i]);
    float* d[2] { l, r };
    if (on)
        stft.process (d, n, [this] (Stft::cpx** bins, int chans, int nb) { onFrame (bins, chans, nb); });
    else
    {
        stft.process (d, n, [] (Stft::cpx**, int, int) {});
        for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
    }
    for (int i = 0; i < n; ++i)
        ChannelModes::join (chMode, l[i], r[i], l[i], r[i]);
}

// ===========================================================================
// Clarity
// ===========================================================================
Clarity::Clarity()
    : SpectralBase ({ "clr", "CLARITY", "CLR", 0xfff2d35cu, true, false })
{
    pOn      = addBool ("on", "On", true);
    pChMode  = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pAmount  = addPct ("amount", "Amount", 0.0f, 100.0f, 50.0f);
    pTilt    = addFloat ("tilt", "Tilt", -6.0f, 6.0f, 0.0f, "dB/oct", 1);
    pAttack  = addMs ("attack", "Attack", 1.0f, 500.0f, 30.0f);
    pRelease = addMs ("release", "Release", 10.0f, 3000.0f, 300.0f);
    pLow     = addHz ("low", "Action Low", 300.0f, 20000.0f, 300.0f);
    pHigh    = addHz ("high", "Action High", 300.0f, 20000.0f, 20000.0f);
}

void Clarity::computeGains (int c, const float* frameDb, float* out)
{
    const float dt = frameSeconds();
    const float aC = 1.0f - std::exp (-dt / (value (pAttack) * 0.001f));
    const float rC = 1.0f - std::exp (-dt / (value (pRelease) * 0.001f));
    float* sm = smooth[c];
    if (! primed[c]) { for (int b = 0; b < kBands; ++b) sm[b] = frameDb[b]; primed[c] = true; }

    // Smooth the spectrum in time (attack when rising) and keep it for display.
    for (int b = 0; b < kBands; ++b)
    {
        sm[b] += (frameDb[b] > sm[b] ? aC : rC) * (frameDb[b] - sm[b]);
        specDb[c][b] = sm[b];
    }

    // Target: a line with the chosen slope. Per-bin band energy of pink noise
    // falls 3 dB/oct, so TILT 0 is a -3 dB/oct line through the spectrum's
    // mean level in the action region.
    const float lo = value (pLow), hi = std::max (value (pLow) * 1.01f, value (pHigh));
    const float slope = -3.0f + value (pTilt);
    double sumDev = 0; int cnt = 0;
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = bandHz (b);
        if (hz < lo || hz > hi) continue;
        sumDev += sm[b] - slope * std::log2 (hz / 1000.0f);
        ++cnt;
    }
    const float level = cnt > 0 ? (float) (sumDev / cnt) : 0.0f;
    const float amount = value (pAmount) * 0.01f;
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = bandHz (b);
        float g = 0.0f;
        if (hz >= lo && hz <= hi && sm[b] > -80.0f)
        {
            const float target = level + slope * std::log2 (hz / 1000.0f);
            g = clampf ((target - sm[b]) * 0.5f, -6.0f, 6.0f) * amount;
        }
        // Soft edges of the action region.
        const float edge = std::min (clampf (std::log2 (hz / lo) * 3.0f + 1.0f, 0.0f, 1.0f),
                                     clampf (std::log2 (hi / hz) * 3.0f + 1.0f, 0.0f, 1.0f));
        out[b] = g * edge;
    }
    // Smooth across neighbouring bands to avoid comb-like gain.
    float tmp[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        const float a = out[std::max (0, b - 1)], m = out[b], z = out[std::min (kBands - 1, b + 1)];
        tmp[b] = 0.25f * a + 0.5f * m + 0.25f * z;
    }
    for (int b = 0; b < kBands; ++b) out[b] = tmp[b];
}

// ===========================================================================
// Stabilizer
// ===========================================================================
Stabilizer::Stabilizer()
    : SpectralBase ({ "stb", "STABILIZER", "STB", 0xff9ad75cu, true, false })
{
    pOn      = addBool ("on", "On", true);
    pChMode  = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    std::vector<std::string> targets;
    for (int t = 0; t < TonalTargets::kNumTargets; ++t) targets.push_back (TonalTargets::name (t));
    targets.push_back ("Assistant");
    pTarget  = addChoice ("target", "Target", targets, 0);
    pMode    = addChoice ("mode", "Mode", { "Shape", "Cut" }, 0);
    pAmount  = addPct ("amount", "Amount", 0.0f, 100.0f, 50.0f);
    pSpeed   = addPct ("speed", "Speed", 0.0f, 100.0f, 50.0f);
    pSmoothing = addPct ("smoothing", "Smoothing", 0.0f, 100.0f, 50.0f);
    pSensitivity = addPct ("sensitivity", "Sensitivity", 0.0f, 100.0f, 50.0f);
    pTame    = addBool ("tame", "Tame Transients", false);
    pLow     = addPct ("low", "Low", 0.0f, 100.0f, 100.0f);
    pMid     = addPct ("mid", "Mid", 0.0f, 100.0f, 100.0f);
    pHigh    = addPct ("high", "High", 0.0f, 100.0f, 100.0f);
}

float Stabilizer::targetDb (int band) const
{
    const int t = (int) value (pTarget);
    if (t >= TonalTargets::kNumTargets)
        return hasAssistantTarget.load (std::memory_order_relaxed) ? assistantTarget[(size_t) band] : TonalTargets::curveDb (0, band);
    return TonalTargets::curveDb (t, band);
}

void Stabilizer::computeGains (int c, const float* frameDb, float* out)
{
    const float dt = frameSeconds();
    const float speed = value (pSpeed) * 0.01f;
    const float tauMs = 2000.0f * std::pow (0.02f, speed);   // 2 s .. 40 ms
    const float coef = 1.0f - std::exp (-dt / (tauMs * 0.001f));
    float* sm = smooth[c];
    if (! primed[c]) { for (int b = 0; b < kBands; ++b) sm[b] = frameDb[b]; primed[c] = true; }

    // Frame energy jump = transient: react at once when TAME is on.
    float frameLevel = 0.0f; int n = 0;
    for (int b = 0; b < kBands; ++b) if (frameDb[b] > -90.0f) { frameLevel += frameDb[b]; ++n; }
    frameLevel = n > 0 ? frameLevel / (float) n : -90.0f;
    const bool transient = value (pTame) > 0.5f && frameLevel > lastLevel[c] + 6.0f;
    lastLevel[c] += 0.3f * (frameLevel - lastLevel[c]);

    for (int b = 0; b < kBands; ++b)
    {
        sm[b] += (transient ? 1.0f : coef) * (frameDb[b] - sm[b]);
        specDb[c][b] = sm[b];
    }

    // Pink per-bin energy falls 3 dB/oct; the targets are flat-per-band shapes.
    // Fit the target's level to the spectrum (least squares offset).
    double sumDev = 0; int cnt = 0;
    float target[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        target[b] = targetDb (b) - 3.0f * std::log2 (bandHz (b) / 1000.0f);
        if (sm[b] > -80.0f) { sumDev += sm[b] - target[b]; ++cnt; }
    }
    const float level = cnt > 0 ? (float) (sumDev / cnt) : 0.0f;
    float meanLevel = 0.0f; int mcnt = 0;
    for (int b = 0; b < kBands; ++b) if (sm[b] > -80.0f) { meanLevel += sm[b]; ++mcnt; }
    meanLevel = mcnt > 0 ? meanLevel / (float) mcnt : -80.0f;

    const float amount = value (pAmount) * 0.01f;
    const float sens = value (pSensitivity) * 0.01f;
    const bool cutMode = (int) value (pMode) == 1;
    float raw[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        float dev = sm[b] > -80.0f ? sm[b] - (target[b] + level) : 0.0f;   // + means too much
        // Nothing to boost where there is nothing: well below the mean, only cuts.
        if (sm[b] < meanLevel - 30.0f && dev < 0.0f) dev = 0.0f;
        float g;
        if (cutMode)
        {
            const float thresh = (1.0f - sens) * 9.0f;   // sensitivity 100: any deviation; 0: only 9 dB+
            g = dev > thresh ? -(dev - thresh) * 0.8f : 0.0f;
        }
        else
        {
            const float dead = (1.0f - sens) * 3.0f;
            const float d = dev > dead ? dev - dead : dev < -dead ? dev + dead : 0.0f;
            g = -d * 0.6f;
        }
        raw[b] = clampf (g, -9.0f, 9.0f) * amount;
    }

    // Smoothing across bands: 0 = sharp, 100 = three or four broad moves.
    const float smoothing = value (pSmoothing) * 0.01f;
    const int radius = (int) std::lround (smoothing * 8.0f);
    for (int b = 0; b < kBands; ++b)
    {
        float sum = 0.0f, wsum = 0.0f;
        for (int k = -radius; k <= radius; ++k)
        {
            const int i = b + k;
            if (i < 0 || i >= kBands) continue;
            const float w = 1.0f - std::fabs ((float) k) / (float) (radius + 1);
            sum += raw[i] * w; wsum += w;
        }
        out[b] = sum / wsum;
    }
    // Loudness neutral in SHAPE mode.
    if (! cutMode)
    {
        float mean = 0.0f;
        for (int b = 0; b < kBands; ++b) mean += out[b];
        mean /= (float) kBands;
        for (int b = 0; b < kBands; ++b) out[b] -= mean;
    }
    // Low / mid / high amounts.
    const float lo = value (pLow) * 0.01f, mid = value (pMid) * 0.01f, hi = value (pHigh) * 0.01f;
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = bandHz (b);
        out[b] *= hz < 100.0f ? lo : hz < 5600.0f ? mid : hi;
    }
}

// ===========================================================================
// Spectral Shaper
// ===========================================================================
SpectralShaper::SpectralShaper()
    : SpectralBase ({ "shp", "SPECTRAL SHAPER", "SHAPER", 0xffe08a5cu, true, false })
{
    pOn      = addBool ("on", "On", true);
    pChMode  = addChoice ("chmode", "Channel Mode", { "Stereo", "Mid/Side" }, Stereo);
    pMode    = addChoice ("mode", "Mode", { "Low", "Medium", "High" }, 1);
    pAmount  = addPct ("amount", "Amount", 0.0f, 100.0f, 50.0f);
    pTone    = addFloat ("tone", "Tone", -100.0f, 100.0f, 0.0f, "", 0);
    pAttack  = addMs ("attack", "Attack", 0.5f, 200.0f, 10.0f);
    pRelease = addMs ("release", "Release", 5.0f, 2000.0f, 120.0f);
    pLow     = addHz ("low", "Action Low", 20.0f, 20000.0f, 2000.0f);
    pHigh    = addHz ("high", "Action High", 20.0f, 20000.0f, 12000.0f);
}

void SpectralShaper::computeGains (int c, const float* frameDb, float* out)
{
    const float dt = frameSeconds();
    const float aC = 1.0f - std::exp (-dt / (value (pAttack) * 0.001f));
    const float rC = 1.0f - std::exp (-dt / (value (pRelease) * 0.001f));
    const int mode = (int) value (pMode);
    const float thresh = mode == 0 ? 6.0f : mode == 1 ? 4.0f : 2.0f;
    const float ratioSlope = mode == 0 ? 0.5f : mode == 1 ? 0.7f : 0.85f;
    const float amount = value (pAmount) * 0.01f;
    const float tone = value (pTone) * 0.01f;   // tilt of the detection: + brighter result
    const float lo = value (pLow), hi = std::max (value (pLow) * 1.01f, value (pHigh));

    // Spectral envelope: a wide smoothing across bands.
    float env[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        float sum = 0.0f, wsum = 0.0f;
        for (int k = -9; k <= 9; ++k)
        {
            const int i = b + k;
            if (i < 0 || i >= kBands) continue;
            const float w = 1.0f - std::fabs ((float) k) / 10.0f;
            sum += frameDb[i] * w; wsum += w;
        }
        env[b] = sum / wsum;
        specDb[c][b] = frameDb[b];
    }
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = bandHz (b);
        float target = 0.0f;
        if (hz >= lo && hz <= hi && frameDb[b] > -80.0f)
        {
            // Tone: a negative tone lowers the threshold at high frequencies (darker).
            const float tilt = -tone * 3.0f * std::log2 (hz / 1000.0f);
            const float excess = frameDb[b] - env[b] - (thresh + tilt);
            if (excess > 0.0f)
                target = -excess * ratioSlope * amount;
        }
        gr[c][b] += (target < gr[c][b] ? aC : rC) * (target - gr[c][b]);
        out[b] = clampf (gr[c][b], -24.0f, 0.0f);
    }
}
} // namespace burnish
