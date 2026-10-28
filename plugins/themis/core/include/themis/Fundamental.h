#pragma once

#include "Common.h"
#include "FrameHistory.h"

#include <algorithm>
#include <cmath>

namespace themis
{
namespace fundamental
{
inline constexpr double kMinHz = 30.0, kMaxHz = 250.0; // where a bass fundamental is looked for
inline constexpr int kMaxHarmonic = 8;                 // harmonics 2..8 are summed into the "harmonics" level
inline constexpr float kProminenceDb = 20.0f;          // a peak counts if it is within this of the strongest tonal peak in range
inline constexpr float kMinLevelDb = -75.0f;           // the strongest peak must be at least this loud (mean-square dBFS)
inline constexpr float kMinTonalityDb = 12.0f;         // main-lobe energy over the energy of the bins just outside it (a sine: 25 dB+, noise: < 8 dB)
inline constexpr int kLobeBins = 2;                    // a Hann-windowed sine puts all its energy within +/-2 bins
inline constexpr int kWideBins = 7;                    // neighbourhood used to judge whether a peak stands out
inline constexpr int kPeakRadius = 3;                  // a fundamental must be the largest bin within this many bins
inline constexpr double kHarmonicTolerance = 0.03;     // harmonics are searched for within +/-3 % (half a semitone)

// Hand-authored rule of thumb for the label of the fundamental-versus-harmonics balance (fundamental level minus
// harmonics level). Not measured data.
inline constexpr float kSubHeavyDb = 15.0f, kHarmonicRichDb = -6.0f;
enum class Balance { subHeavy, balanced, harmonicRich };

inline const char* name (Balance b)
{
    return b == Balance::subHeavy ? "sub-heavy" : b == Balance::harmonicRich ? "harmonic-rich" : "balanced";
}

inline Balance balanceOf (float fundamentalMinusHarmonicsDb)
{
    return fundamentalMinusHarmonicsDb >= kSubHeavyDb ? Balance::subHeavy
           : fundamentalMinusHarmonicsDb <= kHarmonicRichDb ? Balance::harmonicRich
                                                            : Balance::balanced;
}

inline constexpr float kMaxTiltDb = 24.0f; // the balance graph's range; a fundamental with no harmonics at all sits at the top

// How far a frame's fundamental sits above its harmonics, in +/-kMaxTiltDb; kHistoryFloorDb when no fundamental was found.
inline float tiltDb (const FrameStats& f)
{
    if (!f.hasFundamental())
        return kHistoryFloorDb;
    return hasLevel (f.harmonicsDb) ? std::clamp (f.fundamentalDb - f.harmonicsDb, -kMaxTiltDb, kMaxTiltDb) : kMaxTiltDb;
}
} // namespace fundamental

struct FundamentalEstimate
{
    bool found = false;
    double hz = 0.0;
    float fundamentalDb = kHistoryFloorDb; // mean-square level of the fundamental
    float harmonicsDb = kHistoryFloorDb;   // summed level of harmonics 2..kMaxHarmonic that stand out of the spectrum
};

// Finds the bass fundamental in a power spectrum (bins 0..numBins-1, `binHz` apart; `msScale` converts the bin
// powers' sum to a mean square) and measures its level against its harmonics.
//
// The fundamental is the lowest tonal spectral peak in 30..250 Hz that is within kProminenceDb of the strongest one.
// Taking the lowest (not the strongest) peak is what lets a bass whose fundamental is much weaker than its first
// harmonic still be measured as such. A note whose fundamental is missing altogether (more than ~20 dB under the
// first harmonic) cannot be told apart from a note that starts at that harmonic, and is reported from the harmonic.
inline FundamentalEstimate estimateFundamental (const float* power, int numBins, double binHz, double msScale)
{
    using namespace fundamental;
    FundamentalEstimate out;
    const int lo = std::max (kWideBins + 1, static_cast<int> (std::ceil (kMinHz / binHz)));
    const int hi = std::min (static_cast<int> (std::floor (kMaxHz / binHz)), numBins - 1 - kWideBins);
    if (hi <= lo)
        return out;

    const auto sum = [&] (int from, int to) {
        double s = 0.0;
        for (int k = std::max (from, 0); k <= std::min (to, numBins - 1); ++k)
            s += power[k];
        return s;
    };
    const auto lobe = [&] (int k) { return sum (k - kLobeBins, k + kLobeBins); };
    // Largest bin in a neighbourhood (ties go to the lower bin, so a flat top counts once).
    const auto isPeak = [&] (int k, int radius) {
        for (int j = k - radius; j <= k + radius; ++j)
            if (j != k && (j < k ? power[j] > power[k] : power[j] >= power[k]))
                return false;
        return true;
    };
    // A tonal peak stands well clear of the bins around its main lobe; a noise peak does not.
    const double minTonality = dbToPower (kMinTonalityDb);
    const auto isTonal = [&] (int k) {
        const double outside = sum (k - kWideBins, k + kWideBins) - lobe (k);
        return lobe (k) >= minTonality * std::max (outside, 0.0);
    };
    const auto tonalPeak = [&] (int k) { return isPeak (k, kPeakRadius) && isTonal (k); };

    double strongest = 0.0;
    for (int k = lo; k <= hi; ++k)
        if (tonalPeak (k))
            strongest = std::max (strongest, lobe (k));
    if (strongest * msScale < dbToPower (kMinLevelDb))
        return out;

    const double threshold = strongest * dbToPower (-kProminenceDb);
    for (int k = lo; k <= hi; ++k)
    {
        if (!tonalPeak (k) || lobe (k) < threshold)
            continue;

        // Parabolic interpolation on the log spectrum for a sub-bin frequency.
        const double a = std::log (power[k - 1] + 1.0e-30), b = std::log (power[k] + 1.0e-30), c = std::log (power[k + 1] + 1.0e-30);
        const double curvature = a - 2.0 * b + c;
        const double delta = curvature < 0.0 ? std::clamp (0.5 * (a - c) / curvature, -0.5, 0.5) : 0.0;
        out.hz = (k + delta) * binHz;
        out.found = true;
        out.fundamentalDb = static_cast<float> (powerToDb (lobe (k) * msScale, 1.0e-12));

        double harmonics = 0.0;
        for (int h = 2; h <= kMaxHarmonic; ++h)
        {
            const double centre = out.hz * h / binHz;
            const int reach = std::max (kLobeBins, static_cast<int> (std::ceil (centre * kHarmonicTolerance)));
            if (centre + reach + kWideBins + 1 >= numBins)
                break;
            int best = static_cast<int> (std::lround (centre));
            for (int j = best - reach; j <= best + reach; ++j)
                if (power[j] > power[best])
                    best = j;
            if (isPeak (best, 1) && isTonal (best))
                harmonics += lobe (best);
        }
        if (harmonics > 0.0)
            out.harmonicsDb = static_cast<float> (powerToDb (harmonics * msScale, 1.0e-12));
        return out;
    }
    return out;
}
} // namespace themis
