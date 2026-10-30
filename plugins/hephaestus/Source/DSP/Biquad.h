// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <array>
#include <cmath>

namespace hep
{
// Double-precision RBJ biquad. Low-frequency filters (tens of Hz) put poles very close
// to z = 1, which single precision cannot resolve cleanly, so everything here is double.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowpass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * M_PI * f / fs, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighpass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * M_PI * f / fs, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    // Constant 0 dB peak-gain band-pass.
    void setBandpass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * M_PI * f / fs, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

private:
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
};

// Butterworth low/high-pass of order 2 or 4 (one or two cascaded biquads).
class Butterworth
{
public:
    enum class Type { lowpass, highpass };

    void setup (Type t, int order, double fs, double freq) noexcept
    {
        static constexpr double q4[2] = { 0.5411961001461970, 1.3065629648763766 };
        sections = order >= 4 ? 2 : 1;
        freq = std::fmin (std::fmax (freq, 1.0), fs * 0.45);

        for (int i = 0; i < sections; ++i)
        {
            const double q = sections == 2 ? q4[i] : 0.7071067811865476;
            if (t == Type::lowpass) bq[(size_t) i].setLowpass (fs, freq, q);
            else                    bq[(size_t) i].setHighpass (fs, freq, q);
        }
    }

    void reset() noexcept { for (auto& b : bq) b.reset(); }

    inline double process (double x) noexcept
    {
        x = bq[0].process (x);
        return sections == 2 ? bq[1].process (x) : x;
    }

private:
    std::array<Biquad, 2> bq;
    int sections = 1;
};

// High-pass + low-pass in series; the building block for every "range" in the fundamental engine.
struct Bandpass
{
    Butterworth hp, lp;

    void setup (double fs, int hpOrder, double hpHz, int lpOrder, double lpHz) noexcept
    {
        hp.setup (Butterworth::Type::highpass, hpOrder, fs, hpHz);
        lp.setup (Butterworth::Type::lowpass, lpOrder, fs, lpHz);
    }

    void reset() noexcept { hp.reset(); lp.reset(); }
    inline double process (double x) noexcept { return lp.process (hp.process (x)); }
};
}
