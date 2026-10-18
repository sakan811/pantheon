#pragma once

#include <algorithm>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
 #define THEMIS_RESTRICT __restrict
#else
 #define THEMIS_RESTRICT
#endif

namespace themis
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float lin) { return 20.0f * std::log10 (std::max (lin, 1.0e-9f)); }

// Default averaging window of the long-term meters (the plugin's "30 s" choice must match it).
inline constexpr float kDefaultAveragingSeconds = 30.0f;

// Added to a power before taking its log, so silence reads as a very low level instead of -inf.
inline constexpr double kPowerFloor = 1.0e-20;

inline double dbToPower (double db) { return std::pow (10.0, db / 10.0); }
inline double powerToDb (double power, double floor = kPowerFloor) { return 10.0 * std::log10 (power + floor); }

// Mid/side split of a stereo sample; L = M + S, R = M - S.
inline float midOf (float l, float r) { return (l + r) * 0.5f; }
inline float sideOf (float l, float r) { return (l - r) * 0.5f; }

// Weight of the newest value in an exponential average with time constant `tauSeconds`, one value arriving
// every `stepSeconds`. tau <= 0 means "cumulative mean" and returns 0 (see runningMeanWeight).
inline double emaWeight (double stepSeconds, double tauSeconds)
{
    return tauSeconds <= 0.0 ? 0.0 : 1.0 - std::exp (-stepSeconds / tauSeconds);
}

// Weight for the `count`-th value (1-based) of a running average: a plain cumulative mean until the exponential
// average's weight `emaW` is larger, so a fresh average is not dominated by its zero start value.
inline double runningMeanWeight (double emaW, int count) { return std::max (emaW, 1.0 / static_cast<double> (count)); }

// One-pole smoothing coefficient for a given time constant.
inline double onePoleCoeff (double timeSeconds, double sampleRate)
{
    if (timeSeconds <= 0.0)
        return 0.0;
    return std::exp (-1.0 / (timeSeconds * sampleRate));
}
} // namespace themis
