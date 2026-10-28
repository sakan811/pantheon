#pragma once

#include <algorithm>

namespace hep
{
// The user draws up to maxBands bands over the spectrum; each one is either a saturator or a
// harmonic filler. Everything that decides where a band really sits lives here, so the engine and
// the spectrum view cannot disagree.
constexpr int maxBands = 6;

enum class BandMode { saturate = 0, fill = 1 };

// The upper edge of a band is never allowed closer than this ratio to the lower edge.
constexpr double minRangeRatio = 1.5;

// A fill band works on fundamentals: its harmonics (up to the 5th) must stay clear of Nyquist at
// ordinary sample rates, so its range stops here.
constexpr double fillMaxHz = 2000.0;

struct Range { double lo, hi; };

inline Range effectiveRange (BandMode mode, double lo, double hi) noexcept
{
    if (mode == BandMode::fill)
    {
        lo = std::min (lo, fillMaxHz / minRangeRatio);
        hi = std::min (hi, fillMaxHz);
    }
    return { lo, std::max (hi, lo * minRangeRatio) };
}

// Where the harmonics of a fill band land, and where its adaptive fill looks for energy that is
// already there: just above the band, up to the 5th harmonic of its top edge.
inline Range harmonicReach (double hi) noexcept { return { hi * 1.25, hi * 5.2 }; }
}
