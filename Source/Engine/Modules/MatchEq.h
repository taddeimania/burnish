// MatchEq.h — captures the average spectrum of a reference and of the
// track (APPLY TO), and applies the difference as a linear-phase curve,
// smoothed and scaled, between two cutoffs.
#pragma once

#include "../Channels.h"
#include "../Module.h"
#include "../Stft.h"

#include <atomic>
#include <vector>

namespace burnish
{
class MatchEq final : public Module
{
public:
    static constexpr int kFrame = 2048, kHop = 512;
    static constexpr int kBins = kFrame / 2 + 1;
    enum Capture { Idle = 0, CaptureReference = 1, CaptureApply = 2 };

    MatchEq();

    int pOn, pChMode, pSmoothing, pAmount, pLow, pHigh;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return kFrame; }

    // Capturing (message thread sets, audio thread accumulates).
    std::atomic<int> capture { Idle };
    void clearReference();
    void clearApply();
    bool hasReference() const { return refFrames > 0; }
    bool hasApply() const { return applyFrames > 0; }
    int  referenceFrames() const { return refFrames; }
    int  applyFrames_() const { return applyFrames; }

    // Snapshots in dB per bin (relative), for display and for state.
    std::vector<float> referenceDb, applyDb;   // kBins each; -200 when empty
    std::vector<float> curveDb;                // the matched curve now applied, kBins
    void setReferenceDb (const std::vector<float>& db);   // from saved state
    void setApplyDb (const std::vector<float>& db);
    static float binHz (int k, double sr) { return Stft::binHz (k, kFrame, sr); }

private:
    void rebuildCurve();

    Stft stft;
    std::vector<double> refAcc, applyAcc;
    int refFrames = 0, applyFrames = 0;
    std::vector<float> binGain;
    float lastParams[4] { -1, -1, -1, -1 };
    int   lastRef = -1, lastApply = -1;
    int   sinceRebuild = 0;
};
} // namespace burnish
