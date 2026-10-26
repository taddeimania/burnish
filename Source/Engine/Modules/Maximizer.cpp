#include "Maximizer.h"

namespace burnish
{
Maximizer::Maximizer()
    : Module ({ "max", "MAXIMIZER", "MAX", 0xffe2574cu, false, false })
{
    pOn        = addBool ("on", "On", true);
    pMode      = addChoice ("mode", "Mode", { "Low Latency", "Mode I", "Mode II", "Mode III", "Mode IV", "Mode V" }, Irc3);
    pStyle3    = addChoice ("style3", "Mode III Style", { "Clipping", "Crisp", "Balanced", "Pumping" }, 2);
    pStyle4    = addChoice ("style4", "Mode IV Style", { "Classic", "Modern", "Transient" }, 1);
    pGain      = addDb ("gain", "Gain", 0.0f, 30.0f, 0.0f);
    pOutput    = addDb ("output", "Output Level", -30.0f, 0.0f, -1.0f);
    pLink      = addBool ("link", "Link Gain and Output", false);
    pTruePeak  = addBool ("truepeak", "True Peak", true);
    pCharacter = addFloat ("character", "Character", 0.0f, 10.0f, 5.0f, "", 1);
    pUpward    = addDb ("upward", "Upward Compress", 0.0f, 12.0f, 0.0f);
    pClipOn    = addBool ("clip.on", "Soft Clip On", false);
    pClipAmount = addPct ("clip.amount", "Soft Clip Amount", 0.0f, 100.0f, 50.0f);
    pClipMode  = addChoice ("clip.mode", "Soft Clip Mode", { "Light", "Moderate", "Heavy" }, Light);
    pTransOn   = addBool ("trans.on", "Transient Emphasis On", false);
    pTransAmount = addPct ("trans.amount", "Transient Emphasis", 0.0f, 100.0f, 30.0f);
    pStereoTrans = addPct ("stereo.transient", "Stereo Independence Transient", 0.0f, 100.0f, 0.0f);
    pStereoSustain = addPct ("stereo.sustain", "Stereo Independence Sustain", 0.0f, 100.0f, 0.0f);
    pStereoLink = addBool ("stereo.link", "Stereo Independence Link", true);
    pTargetLufs = addFloat ("target", "Target LUFS", -30.0f, -6.0f, -14.0f, "LUFS", 1);
}

const char* Maximizer::characterText (float c)
{
    return c < 1.0f ? "CLIPPING" : c < 3.0f ? "FAST" : c < 5.0f ? "MODERATE" : c < 7.0f ? "SMOOTH" : c < 9.0f ? "SLOW" : "VERY SLOW";
}

int Maximizer::windowForMode (int mode) const
{
    const double ms = mode == LowLatency ? 0.5 : mode == Irc1 ? 1.5 : mode == Irc2 ? 2.0 : mode == Irc3 ? 2.5 : mode == Irc4 ? 3.0 : 4.0;
    return std::max (4, (int) std::lround (ms * 0.001 * sr));
}

int Maximizer::latency() const
{
    // Look-ahead + true-peak delay + the soft clip's oversampling path.
    return windowForMode ((int) value (pMode)) + TruePeak4x::kDelay + 23;
}

void Maximizer::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    const int maxWin = std::max (4, (int) std::lround (0.004 * sr));
    limiter.prepare (sr, maxWin);
    for (auto& b : bandLimiter) b.prepare (sr, maxWin);
    for (auto& s : mbSplit) s.prepare (sr);
    for (auto& d : upDet) { d.prepare ((float) sr); d.mode = DetectRms; }
    upAtt = tauCoef (40.0f, (float) sr);
    upRel = tauCoef (400.0f, (float) sr);
    for (auto& d : clipBypass) d.prepare (32);
    for (auto& d : clipBypass) d.setDelay (23);
    for (auto& t : trans) t.prepare ((float) sr);
    linkTrans.prepare ((float) sr);
    gainSm.setTime (40.0f, (float) sr, kStep);
    outSm.setTime (40.0f, (float) sr, kStep);
    charSm.setTime (80.0f, (float) sr, kStep);
    outMeter.prepare (sr);
    outMeter.setTruePeak (true);
    lastMode = -1;
    reset();
}

void Maximizer::reset()
{
    limiter.reset();
    for (auto& b : bandLimiter) b.reset();
    for (auto& s : mbSplit) s.reset();
    for (auto& d : upDet) d.reset();
    upGainDb[0] = upGainDb[1] = 0.0f;
    for (auto& o : clipOs1) o.reset();
    for (auto& o : clipOs2) o.reset();
    for (auto& d : clipBypass) d.reset();
    for (auto& t : trans) t.reset();
    transGainSm[0] = transGainSm[1] = 0.0f;
    linkTrans.reset();
    sustainMeasure = 0.0f;
    outMeter.reset();
    gainSm.snap (value (pGain));
    outSm.snap (value (pOutput));
    charSm.snap (value (pCharacter));
    grDb = grLDb = grRDb = upwardDb = clipDb = 0.0f;
    clipPeakIn = clipPeakOut = 0.0f;
}

void Maximizer::updateTimes()
{
    const int mode = (int) value (pMode);
    const float c = charSm.value;
    const float base = 2.0f * std::pow (10.0f, c * 0.25f);   // 2 ms .. 630 ms
    float fast = base, slow = base;
    switch (mode)
    {
        case LowLatency: fast = slow = base; break;
        case Irc1: fast = base * 0.35f; slow = base * 3.0f; break;
        case Irc2: fast = base * 0.2f;  slow = base * 2.0f; break;
        case Irc3:
        case Irc5:
        {
            const int style = (int) value (pStyle3);
            if (style == 0)      { fast = base * 0.1f;  slow = base * 0.3f; }
            else if (style == 1) { fast = base * 0.25f; slow = base * 1.0f; }
            else if (style == 2) { fast = base * 0.5f;  slow = base * 4.0f; }
            else                 { fast = base * 6.0f;  slow = base * 6.0f; }
            break;
        }
        case Irc4:
        {
            const int style = (int) value (pStyle4);
            if (style == 0)      { fast = base * 0.35f; slow = base * 3.0f; }
            else if (style == 1) { fast = base * 0.5f;  slow = base * 4.0f; }
            else                 { fast = base * 0.2f;  slow = base * 2.0f; }
            break;
        }
        default: break;
    }
    relFast = tauCoef (fast, (float) sr);
    relSlow = tauCoef (slow, (float) sr);

    if (mode != lastMode)
    {
        lastMode = mode;
        const int win = windowForMode (mode);
        limiter.setWindow (win);
        const int nb = mode == Irc4 ? 3 : 4;
        const float f3[3] { 160.0f, 2500.0f, 20000.0f };
        const float f4[3] { 120.0f, 800.0f, 5000.0f };
        for (auto& s : mbSplit) s.set (nb, mode == Irc4 ? f3 : f4);
        for (int b = 0; b < 4; ++b)
        {
            bandLimiter[b].setWindow (win);
            // Lows release slower, highs faster.
            bandLimiter[b].setRelease (base * (b == 0 ? 4.0f : b == 1 ? 2.0f : b == 2 ? 1.0f : 0.5f));
        }
    }
    else if (mode >= Irc4)
    {
        for (int b = 0; b < 4; ++b)
            bandLimiter[b].setRelease (base * (b == 0 ? 4.0f : b == 1 ? 2.0f : b == 2 ? 1.0f : 0.5f));
    }
    limiter.setTruePeak (value (pTruePeak) > 0.5f);
    for (auto& b : bandLimiter) b.setTruePeak (false);
}

void Maximizer::process (float* l, float* r, int n)
{
    // LEARN start/stop requests.
    const int req = learnRequests.load (std::memory_order_relaxed);
    if (req != seenLearn)
    {
        seenLearn = req;
        if (! isLearning)
        {
            isLearning = true;
            learnSeconds = 0.0f;
            learnGainAtStart = value (pGain);
            outMeter.reset();
        }
        else
            isLearning = false;
    }

    if (value (pOn) < 0.5f)
    {
        // Keep the latency: pass through the limiter's delay with no limiting.
        // Simplest: run the limiter with an impossible ceiling.
        for (int start = 0; start < n; start += kStep)
        {
            const int len = std::min (kStep, n - start);
            charSm.target = value (pCharacter); charSm.next();
            updateTimes();
            for (int i = start; i < start + len; ++i)
            {
                float a = clipBypass[0].tick (l[i]), b = clipBypass[1].tick (r[i]);
                limiter.tick (a, b, 1.0e9f, 0.0f);
                l[i] = a; r[i] = b;
            }
        }
        grDb = grLDb = grRDb = upwardDb = clipDb = 0.0f;
        return;
    }

    const int mode = (int) value (pMode);
    const bool clipOn = value (pClipOn) > 0.5f;
    const int clipMode = (int) value (pClipMode);
    const float clipAmt = value (pClipAmount) * 0.01f;
    const float clipKnee = clipMode == Light ? dbToGain (-3.0f) : clipMode == Moderate ? dbToGain (-9.0f) : dbToGain (-30.0f);
    const bool transOn = value (pTransOn) > 0.5f;
    const float transAmt = value (pTransAmount) * 0.01f;
    const float stTrans = value (pStereoTrans) * 0.01f;
    const float stSust = (value (pStereoLink) > 0.5f ? value (pStereoTrans) : value (pStereoSustain)) * 0.01f;
    const float upMax = value (pUpward);

    auto clipShape = [clipKnee, clipAmt] (float x)
    {
        const float ax = std::fabs (x);
        if (ax <= clipKnee) return x;
        const float over = (ax - clipKnee) / (1.0f - clipKnee + 1e-6f);
        const float y = clipKnee + (1.0f - clipKnee) * softTanh (over);
        const float wet = x < 0.0f ? -y : y;
        return x + (wet - x) * clipAmt;
    };

    float grSumL = 0.0f, grSumR = 0.0f, upSum = 0.0f;
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        gainSm.target = value (pGain);
        outSm.target = value (pOutput);
        charSm.target = value (pCharacter);
        charSm.next();
        updateTimes();
        const float gainLin = dbToGain (gainSm.next());
        const float ceiling = dbToGain (outSm.next());

        for (int i = start; i < start + len; ++i)
        {
            float a = l[i] * gainLin, b = r[i] * gainLin;

            // Upward compression: boost quiet passages, nothing near full scale.
            if (upMax > 0.0f)
            {
                const float lev = std::max (upDet[0].next (a), upDet[1].next (b));
                const float target = upMax * clampf ((-6.0f - lev) / 30.0f, 0.0f, 1.0f);
                upGainDb[0] += (target > upGainDb[0] ? upRel : upAtt) * (target - upGainDb[0]);
                const float g = dbToGain (upGainDb[0]);
                a *= g; b *= g;
                upSum += upGainDb[0];
            }
            else upGainDb[0] = 0.0f;

            // Soft clip, 4x oversampled; when off the signal takes the same delay.
            if (clipOn)
            {
                clipPeakIn = std::max (clipPeakIn * 0.9995f, std::max (std::fabs (a), std::fabs (b)));
                a = oversampled (4, clipOs1[0], clipOs2[0], a, clipShape);
                b = oversampled (4, clipOs1[1], clipOs2[1], b, clipShape);
                clipPeakOut = std::max (clipPeakOut * 0.9995f, std::max (std::fabs (a), std::fabs (b)));
                // Keep the bypass delays running so switching is seamless.
                clipBypass[0].tick (a); clipBypass[1].tick (b);
            }
            else
            {
                a = clipBypass[0].tick (a);
                b = clipBypass[1].tick (b);
                clipPeakIn = clipPeakOut = 0.0f;
            }

            // Transient emphasis before limiting.
            if (transOn)
            {
                const float t0 = trans[0].next (std::fabs (a)), t1 = trans[1].next (std::fabs (b));
                const float t = std::max (t0, t1);
                const float target = 1.0f + transAmt * 0.6f * t;
                transGainSm[0] += 0.05f * (target - transGainSm[0]);
                a *= transGainSm[0]; b *= transGainSm[0];
            }

            // Stereo independence: transient-weighted blend of the two settings.
            const float tr = linkTrans.next (0.5f * (std::fabs (a) + std::fabs (b)));
            const float indep = lerp (stSust, stTrans, tr);

            // Multiband pre-limiting (modes IV and V): each band kept under the
            // ceiling on its own, which spares the others from its peaks.
            if (mode >= Irc4)
            {
                const int nb = mode == Irc4 ? 3 : 4;
                float bl[4], br[4];
                mbSplit[0].tick (a, bl);
                mbSplit[1].tick (b, br);
                a = b = 0.0f;
                for (int k = 0; k < nb; ++k)
                {
                    bandLimiter[k].tick (bl[k], br[k], ceiling * 1.05f, indep);
                    a += bl[k]; b += br[k];
                }
            }

            // Release: fast for brief peaks, slow while limiting persists.
            const bool limitingNow = limiter.gainReductionDb() < -0.1f;
            sustainMeasure += 0.0005f * ((limitingNow ? 1.0f : 0.0f) - sustainMeasure);
            const float relCoef = lerp (relFast, relSlow, clampf (sustainMeasure * 1.5f, 0.0f, 1.0f));
            limiter.setReleaseCoefficient (relCoef);
            limiter.tick (a, b, ceiling, indep);
            l[i] = a; r[i] = b;
            grSumL += gainToDb (limiter.gainL());
            grSumR += gainToDb (limiter.gainR());
        }
    }

    // Telemetry: averages over the block.
    grLDb = -grSumL / (float) n;
    grRDb = -grSumR / (float) n;
    grDb = std::max (grLDb, grRDb);
    upwardDb = upMax > 0.0f ? upSum / (float) n : 0.0f;
    clipDb = clipOn && clipPeakIn > 1e-4f ? std::max (0.0f, gainToDb (clipPeakIn) - gainToDb (std::max (clipPeakOut, 1e-6f))) : 0.0f;

    outMeter.process (l, r, n);
    outTruePeakDb = outMeter.truePeakDb();
    if (isLearning)
    {
        learnSeconds += (float) n / (float) sr;
        if (learnSeconds >= kLearnSeconds)
        {
            isLearning = false;
            const float heard = outMeter.hasIntegrated() ? outMeter.integrated() : outMeter.shortTerm();
            if (heard > -60.0f)
            {
                learnedGain.store (clampf (learnGainAtStart + (value (pTargetLufs) - heard), 0.0f, 30.0f));
                learnedReady.store (true);
            }
        }
    }
}
} // namespace burnish
