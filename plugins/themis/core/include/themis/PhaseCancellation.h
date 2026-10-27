#pragma once

#include "Translation.h"

#include <algorithm>
#include <cmath>

namespace themis
{
namespace phasecheck
{
inline constexpr int kMinRunBands = 2;           // a cancelling region must span at least this many adjacent bands
inline constexpr int kMaxGapBands = 1;           // ... and may be bridged over gaps this narrow
inline constexpr float kMinCoherence = 0.5f;     // bands less coherent than this say nothing about a delay or a polarity flip
inline constexpr double kFitMaxHz = 4000.0;      // above this a band's phase wraps too fast for a delay to be read from it
inline constexpr int kMinFitBands = 4;
inline constexpr double kMaxDelayMs = 4.0;       // longer offsets are echoes, not comb filtering
inline constexpr double kDelayStepMs = 0.01;
inline constexpr double kMinDelayMs = 0.05;      // a smaller offset is indistinguishable from none at 30 Hz+ resolution
inline constexpr float kMinFit = 0.8f;           // how well a pure delay / polarity flip must explain the phases (1 = perfectly)
} // namespace phasecheck

enum class PhaseCause
{
    none,     // nothing is cancelling
    polarity, // one channel is inverted (over the coherent bands)
    delay,    // one channel is delayed against the other: comb filtering
    other     // cancelling, but not explained by a delay or a polarity flip (phase-shifting stereo effects, filters, mixed sources)
};

struct PhaseReport
{
    bool have = false;
    Level level = Level::good;
    PhaseCause cause = PhaseCause::none;
    int flaggedBands = 0;     // bands that lose more in a mono sum than uncorrelated channels would
    int worstBand = -1;       // the deepest one
    float worstLossDb = 0.0f; // its mono loss (3 dB is what uncorrelated channels lose)
    double loHz = 0.0, hiHz = 0.0; // the widest cancelling region
    float delayMs = 0.0f;     // |offset| between the channels (delay; also the offset on top of a polarity flip)
    bool leftLate = false;    // the left channel is the delayed one
    float notchHz = 0.0f;     // lowest frequency a delay of this size cancels completely in a mono sum
    float fit = 0.0f;         // how well the delay / polarity model explains the measured phases
};

// Bands too far below the anchor level (translation::kWorstBandFloorDb) are ignored, like in the worst-band readout.
//
// Phase cancellation = bands where the channels are more than uncorrelated, i.e. where a mono sum loses more than the
// 3 dB that independent channels lose (correlation < 0). The band limits are the mono-fold strip's
// (translation::kBandMonoWarnDb / kBandMonoBadDb), so the two can't disagree about which bands cancel.
//
// What causes it is read from the phase between the channels over the coherent bands: if it is ~180 degrees
// throughout, the polarity is flipped; if it rotates linearly with frequency, one channel is late by a fixed time
// (a comb filter, notches at odd multiples of 1 / (2 delay)); otherwise it is something less regular. Both
// explanations are fitted with a pure delay / polarity model, which is a model, not proof: a real mix is many sources.
inline PhaseReport analysePhase (const PhaseBands& ph)
{
    using namespace phasecheck;
    PhaseReport r;
    r.have = true;

    const auto inRange = [&] (int b) {
        const double f = BandGrid::centreHz (b);
        return f >= translation::kWorstBandMinHz && f <= translation::kWorstBandMaxHz && ph.levelDb[static_cast<size_t> (b)] >= translation::kWorstBandFloorDb;
    };
    std::array<Level, BandGrid::kNum> levels {};
    std::array<float, BandGrid::kNum> loss {};
    for (int b = 0; b < BandGrid::kNum; ++b)
    {
        const auto s = static_cast<size_t> (b);
        if (!inRange (b))
            continue;
        loss[s] = static_cast<float> (-powerToDb ((1.0 + ph.correlation[s]) * 0.5, 1.0e-6));
        levels[s] = translation::classify (loss[s], translation::kBandMonoWarnDb, translation::kBandMonoBadDb);
    }

    // Cancelling regions: runs of flagged bands, bridged over short gaps; the widest one is reported.
    int runStart = -1, runEnd = -1, bestStart = -1, bestEnd = -1, gap = 0;
    float bestDepth = 0.0f, runDepth = 0.0f;
    const auto closeRun = [&] {
        if (runStart >= 0 && runEnd - runStart + 1 >= kMinRunBands)
        {
            const bool wider = runEnd - runStart > bestEnd - bestStart;
            if (bestStart < 0 || wider || (runEnd - runStart == bestEnd - bestStart && runDepth > bestDepth))
            {
                bestStart = runStart;
                bestEnd = runEnd;
                bestDepth = runDepth;
            }
            for (int b = runStart; b <= runEnd; ++b)
            {
                const auto s = static_cast<size_t> (b);
                if (levels[s] == Level::good)
                    continue;
                ++r.flaggedBands;
                r.level = std::max (r.level, levels[s]);
                if (loss[s] > r.worstLossDb)
                {
                    r.worstLossDb = loss[s];
                    r.worstBand = b;
                }
            }
        }
        runStart = runEnd = -1;
        runDepth = 0.0f;
    };
    for (int b = 0; b < BandGrid::kNum; ++b)
    {
        if (inRange (b) && levels[static_cast<size_t> (b)] != Level::good)
        {
            if (runStart < 0)
                runStart = b;
            runEnd = b;
            runDepth = std::max (runDepth, loss[static_cast<size_t> (b)]);
            gap = 0;
        }
        else if (runStart >= 0 && ++gap > kMaxGapBands)
        {
            gap = 0;
            closeRun();
        }
    }
    closeRun();

    if (r.flaggedBands == 0)
        return r;
    r.loHz = BandGrid::lowEdgeHz (bestStart);
    r.hiHz = BandGrid::highEdgeHz (bestEnd);
    r.cause = PhaseCause::other;

    // Fit "phase = -2 pi f tau" (tau > 0: the left channel is late) and "phase = pi - 2 pi f tau" to the coherent bands.
    struct Point { double hz, phase, weight; };
    std::array<Point, BandGrid::kNum> pts {};
    int n = 0;
    double weightSum = 0.0;
    for (int b = 0; b < BandGrid::kNum; ++b)
    {
        const auto s = static_cast<size_t> (b);
        const double f = BandGrid::centreHz (b);
        if (!inRange (b) || f > kFitMaxHz || ph.coherence[s] < kMinCoherence)
            continue;
        pts[static_cast<size_t> (n++)] = { f, ph.phaseRad[s], ph.coherence[s] };
        weightSum += ph.coherence[s];
    }
    if (n < kMinFitBands)
        return r;

    double bestIn = -2.0, bestAnti = -2.0, tauIn = 0.0, tauAnti = 0.0;
    for (double tau = -kMaxDelayMs; tau <= kMaxDelayMs + 1.0e-9; tau += kDelayStepMs)
    {
        double score = 0.0;
        for (int i = 0; i < n; ++i)
            score += pts[static_cast<size_t> (i)].weight * std::cos (pts[static_cast<size_t> (i)].phase + 2.0 * kPi * pts[static_cast<size_t> (i)].hz * tau * 1.0e-3);
        score /= weightSum;
        if (score > bestIn)
        {
            bestIn = score;
            tauIn = tau;
        }
        if (-score > bestAnti)
        {
            bestAnti = -score;
            tauAnti = tau;
        }
    }

    if (bestAnti >= kMinFit && bestAnti > bestIn)
    {
        r.cause = PhaseCause::polarity;
        r.fit = static_cast<float> (bestAnti);
        r.delayMs = static_cast<float> (std::abs (tauAnti) >= kMinDelayMs ? std::abs (tauAnti) : 0.0);
        r.leftLate = tauAnti > 0.0;
    }
    else if (bestIn >= kMinFit && std::abs (tauIn) >= kMinDelayMs)
    {
        r.cause = PhaseCause::delay;
        r.fit = static_cast<float> (bestIn);
        r.delayMs = static_cast<float> (std::abs (tauIn));
        r.leftLate = tauIn > 0.0;
        r.notchHz = static_cast<float> (500.0 / std::abs (tauIn)); // 1 / (2 tau), tau in ms
    }
    return r;
}
} // namespace themis
