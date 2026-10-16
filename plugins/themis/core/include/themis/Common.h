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

// One-pole smoothing coefficient for a given time constant.
inline double onePoleCoeff (double timeSeconds, double sampleRate)
{
    if (timeSeconds <= 0.0)
        return 0.0;
    return std::exp (-1.0 / (timeSeconds * sampleRate));
}
} // namespace themis
