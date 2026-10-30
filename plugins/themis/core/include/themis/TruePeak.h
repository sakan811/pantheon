// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "Common.h"

#include <array>

namespace themis
{
// 4x oversampled inter-sample peak estimator (96-tap Kaiser-windowed sinc, 24 taps per phase).
// The estimate for input sample k describes the signal around time k - kDelay.
class TruePeakDetector
{
public:
    static constexpr int kTaps = 96;
    static constexpr int kPhases = 4;
    static constexpr int kPerPhase = kTaps / kPhases;
    static constexpr int kDelay = 12; // samples, rounded up from 11.875

    TruePeakDetector() { buildTable(); reset(); }

    void reset() { hist.fill (0.0f); pos = 0; }

    // Returns the largest absolute interpolated value among the 4 phases for this input sample.
    float process (float x)
    {
        pos = (pos + 1) % kPerPhase;
        hist[static_cast<size_t> (pos)] = x;
        float peak = 0.0f;
        for (int p = 0; p < kPhases; ++p)
        {
            float acc = 0.0f;
            for (int k = 0; k < kPerPhase; ++k)
                acc += table[p][k] * hist[static_cast<size_t> ((pos - k + kPerPhase) % kPerPhase)];
            peak = std::max (peak, std::abs (acc));
        }
        return peak;
    }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    void buildTable()
    {
        const double beta = 4.0, centre = (kTaps - 1) / 2.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double x = (n - centre) / kPhases;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = (n - centre) / centre;
            const double win = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            // sinc(n/L) already has unity gain per polyphase branch (the zero-stuffing gain of L is built in)
            table[n % kPhases][n / kPhases] = static_cast<float> (sinc * win);
        }
    }

    float table[kPhases][kPerPhase] {};
    std::array<float, kPerPhase> hist {};
    int pos = 0;
};
} // namespace themis
