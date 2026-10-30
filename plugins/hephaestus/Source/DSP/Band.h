// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

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

// The highest harmonic (at most the 5th) of a band's upper edge that still sits clear of Nyquist;
// 1 means not even the 2nd does. A fill band drops every harmonic above it (it would alias).
inline int highestHarmonic (double hi, double sampleRate) noexcept
{
    int k = 5;
    while (k > 1 && (double) k * hi >= 0.48 * sampleRate)
        --k;
    return k;
}

// Where the harmonics a band adds land, in either mode: from the 2nd harmonic of its lower edge to the
// 5th of its upper edge, stopping short of Nyquist. Measured on band-limited noise (test [5f]): less
// than 1% of what a band adds falls above it. Empty (hi <= lo) when nothing fits below Nyquist.
inline Range harmonicSpan (BandMode mode, double lo, double hi, double sampleRate) noexcept
{
    const auto r = effectiveRange (mode, lo, hi);
    // Fill drops whole harmonics (see highestHarmonic); a saturate band keeps generating up to Nyquist.
    const double top = mode == BandMode::fill ? highestHarmonic (r.hi, sampleRate) * r.hi
                                              : std::min (5.0 * r.hi, 0.48 * sampleRate);
    const double bottom = 2.0 * r.lo;
    return top > bottom ? Range { bottom, top } : Range { 0.0, 0.0 };
}
}
