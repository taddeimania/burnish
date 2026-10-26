#include "Analyzer.h"

namespace burnish::ui
{
using namespace juce;

SpectrumSource::SpectrumSource()
{
    fft.prepare (kFrame);
    window.resize (kFrame);
    double wsum = 0;
    for (int i = 0; i < kFrame; ++i)
    {
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * MathConstants<float>::pi * (float) i / (float) kFrame));
        wsum += window[(size_t) i];
    }
    // Normalise so a full-scale sine reads 0 dB.
    const float norm = (float) (2.0 / wsum);
    for (auto& w : window) w *= norm;
    frame.resize (kFrame);
    left.resize (kFrame); right.resize (kFrame); mono.resize (kFrame);
    bins.resize (kFrame / 2 + 1);
    for (auto& v : avg) v = -120.0f;
    resetPeaks();
}

void SpectrumSource::update (const Tap& tap, int channel)
{
    const int pos = tap.position();
    if (pos == lastPos) return;
    lastPos = pos;
    tap.latest (left.data(), right.data(), kFrame);
    for (int i = 0; i < kFrame; ++i)
    {
        float v;
        if (midSide)
        {
            const float m = 0.5f * (left[(size_t) i] + right[(size_t) i]), s = 0.5f * (left[(size_t) i] - right[(size_t) i]);
            v = channel == 1 ? s : m;
        }
        else
            v = channel == 0 ? left[(size_t) i] : channel == 1 ? right[(size_t) i] : 0.5f * (left[(size_t) i] + right[(size_t) i]);
        frame[(size_t) i] = v * window[(size_t) i];
    }
    fft.forwardReal (frame.data(), bins.data());

    // Per column: the maximum bin magnitude in the column's band (so narrow
    // peaks survive), tilted so pink noise reads flat at 3 dB/oct.
    const double binHz = sampleRate / kFrame;
    const float dt = 1.0f / 30.0f;
    const float coef = averageSeconds <= 0.0f ? 1.0f : 1.0f - std::exp (-dt / averageSeconds);
    for (int c = 0; c < kColumns; ++c)
    {
        const float lo = c == 0 ? 20.0f : std::sqrt (columnHz (c - 1) * columnHz (c));
        const float hi = c == kColumns - 1 ? 20000.0f : std::sqrt (columnHz (c) * columnHz (c + 1));
        int k0 = (int) std::floor (lo / binHz), k1 = (int) std::ceil (hi / binHz);
        k0 = jlimit (1, kFrame / 2, k0); k1 = jlimit (k0, kFrame / 2, k1);
        float m2 = 0.0f;
        for (int k = k0; k <= k1; ++k) m2 = std::max (m2, std::norm (bins[(size_t) k]));
        float db = 10.0f * std::log10 (m2 + 1e-12f);
        db += tilt * std::log2 (columnHz (c) / 1000.0f);
        if (! primed) avg[c] = db;
        else avg[c] += (db > avg[c] ? std::max (coef, 0.5f) : coef) * (db - avg[c]);
        if (peakHold && db > peak[c]) peak[c] = db;
    }
    primed = true;
}

float SpectrumSource::dbAtHz (float hz) const
{
    const float t = freqToNorm (hz) * (float) (kColumns - 1);
    const int i = jlimit (0, kColumns - 2, (int) t);
    const float f = t - (float) i;
    return avg[i] + (avg[i + 1] - avg[i]) * f;
}

// ---------------------------------------------------------------------------
SpectrumView::SpectrumView()
{
    setInterceptsMouseClicks (true, true);
}

Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

float SpectrumView::xForHz (float hz) const { const auto r = plotArea(); return r.getX() + freqToNorm (hz) * r.getWidth(); }
float SpectrumView::hzForX (float x) const { const auto r = plotArea(); return normToFreq ((x - r.getX()) / r.getWidth()); }
float SpectrumView::yForDb (float db) const { const auto r = plotArea(); return r.getY() + (dbTop - db) / (dbTop - dbBottom) * r.getHeight(); }
float SpectrumView::yForGain (float db) const { const auto r = plotArea(); return r.getY() + (gainTop - db) / (gainTop - gainBottom) * r.getHeight(); }
float SpectrumView::gainForY (float y) const { const auto r = plotArea(); return gainTop - (y - r.getY()) / r.getHeight() * (gainTop - gainBottom); }

void SpectrumView::drawSpectrum (Graphics& g, const SpectrumSource& src, Colour line, Colour fillColour, bool withPeaks)
{
    const auto r = plotArea();
    Path p;
    const float* c = src.curve();
    p.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i < SpectrumSource::kColumns; ++i)
    {
        const float x = r.getX() + (float) i / (float) (SpectrumSource::kColumns - 1) * r.getWidth();
        const float y = jlimit (r.getY(), r.getBottom(), yForDb (c[i]));
        p.lineTo (x, y);
    }
    p.lineTo (r.getRight(), r.getBottom());
    p.closeSubPath();
    if (fill)
    {
        g.setColour (fillColour);
        g.fillPath (p);
    }
    g.setColour (line);
    Path outline;
    for (int i = 0; i < SpectrumSource::kColumns; ++i)
    {
        const float x = r.getX() + (float) i / (float) (SpectrumSource::kColumns - 1) * r.getWidth();
        const float y = jlimit (r.getY(), r.getBottom(), yForDb (c[i]));
        if (i == 0) outline.startNewSubPath (x, y); else outline.lineTo (x, y);
    }
    g.strokePath (outline, PathStrokeType (1.2f));
    if (withPeaks && showPeaks)
    {
        Path pk;
        const float* pc = src.peaks();
        for (int i = 0; i < SpectrumSource::kColumns; ++i)
        {
            const float x = r.getX() + (float) i / (float) (SpectrumSource::kColumns - 1) * r.getWidth();
            const float y = jlimit (r.getY(), r.getBottom(), yForDb (pc[i]));
            if (i == 0) pk.startNewSubPath (x, y); else pk.lineTo (x, y);
        }
        g.setColour (line.withAlpha (0.45f));
        g.strokePath (pk, PathStrokeType (1.0f));
    }
}

void SpectrumView::drawCurve (Graphics& g, const std::function<float (float)>& gainDb, Colour colour, float thickness, bool fillUnder)
{
    const auto r = plotArea();
    Path p;
    const int steps = (int) r.getWidth() / 2;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + (float) i / (float) steps * r.getWidth();
        const float y = jlimit (r.getY(), r.getBottom(), yForGain (gainDb (hzForX (x))));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    if (fillUnder)
    {
        Path f = p;
        f.lineTo (r.getRight(), yForGain (0.0f));
        f.lineTo (r.getX(), yForGain (0.0f));
        f.closeSubPath();
        g.setColour (colour.withAlpha (0.18f));
        g.fillPath (f);
    }
    g.setColour (colour);
    g.strokePath (p, PathStrokeType (thickness, PathStrokeType::curved));
}

void SpectrumView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    if (showGrid)
        drawFrequencyGrid (g, plotArea(), showLabels, showGainScale ? gainTop : 0.0f, showGainScale ? gainBottom : 0.0f, showGainScale ? (gainTop - gainBottom > 30.0f ? 6.0f : 3.0f) : 0.0f);
    if (secondary != nullptr)
        drawSpectrum (g, *secondary, secondaryColour, secondaryColour.withAlpha (0.12f), false);
    if (source != nullptr)
        drawSpectrum (g, *source, colours::spectrum.brighter (0.3f), colours::spectrumFill, true);
    paintOverlay (g);
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}
} // namespace burnish::ui
