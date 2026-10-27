#pragma once

#include <cmath>

namespace hep
{
// Each curve provides f(u), f'(u) and the closed-form antiderivative F(u) that first-order
// antiderivative anti-aliasing (Parker, Zavalishin, Le Bivic - DAFx 2016) needs.
// All three have f'(0) = 1, so small signals pass at unity gain whichever curve is chosen.
struct TanhCurve
{
    static double f (double u) noexcept  { return std::tanh (u); }
    static double df (double u) noexcept { const double t = std::tanh (u); return 1.0 - t * t; }
    // log(cosh u), arranged so it cannot overflow for large |u|.
    static double F (double u) noexcept
    {
        const double a = std::fabs (u);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453;
    }
};

struct AtanCurve
{
    static double f (double u) noexcept  { return std::atan (u); }
    static double df (double u) noexcept { return 1.0 / (1.0 + u * u); }
    static double F (double u) noexcept  { return u * std::atan (u) - 0.5 * std::log1p (u * u); }
};

struct AlgebraicCurve
{
    static double f (double u) noexcept  { return u / (1.0 + std::fabs (u)); }
    static double df (double u) noexcept { const double d = 1.0 + std::fabs (u); return 1.0 / (d * d); }
    static double F (double u) noexcept  { const double a = std::fabs (u); return a - std::log1p (a); }
};

// Per-channel state: previous shaper input, previous antiderivative of the residual, and the
// DC blocker's memory.
struct AdaaState
{
    double uPrev = 0.0, GPrev = 0.0, dcX = 0.0, dcY = 0.0;
    bool primed = false; // the first sample after a reset seeds the history instead of differencing against zero
    void reset() noexcept { uPrev = GPrev = dcX = dcY = 0.0; primed = false; }
};

// Shapes `data` in place:  out = x + DCblock( ADAA( f(u) - tangent(u) ) / (f'(bias) * drive) ),
// with u = drive * x + bias.
//
// Splitting off the tangent line at the operating point matters. Plain first-order ADAA is an
// average of two successive samples in the linear regime, i.e. a gentle low-pass (-5 dB at 15 kHz
// at 48 kHz, even for a tiny signal). Applying ADAA only to the nonlinear residual keeps the
// small-signal path exactly transparent while the harmonics it generates are still
// anti-aliased. The small-signal gain is 1 for every drive and bias, and the DC the asymmetric
// bias produces is removed from the residual only, so the bass in the clean path is never
// phase-shifted by the DC blocker.
template <class Curve>
inline void shapeBlock (float* data, int n, double driveStart, double driveEnd, double bias,
                        double dcR, AdaaState& st) noexcept
{
    const double fb = Curve::f (bias);
    const double s = Curve::df (bias);
    const double step = n > 0 ? (driveEnd - driveStart) / n : 0.0;
    double drive = driveStart;

    const auto residual = [&] (double u) { return Curve::f (u) - fb - s * (u - bias); };
    const auto antiderivative = [&] (double u)
    {
        const double d = u - bias;
        return Curve::F (u) - fb * u - 0.5 * s * d * d;
    };

    for (int i = 0; i < n; ++i)
    {
        drive += step;
        const double x = (double) data[i];
        const double u = drive * x + bias;
        const double G = antiderivative (u);
        if (! st.primed)
        {
            st.uPrev = u;
            st.GPrev = G;
            st.primed = true;
        }
        const double du = u - st.uPrev;

        // Midpoint fallback when the denominator becomes ill-conditioned.
        const double r = std::fabs (du) > 1.0e-6 ? (G - st.GPrev) / du
                                                  : residual (0.5 * (u + st.uPrev));
        st.uPrev = u;
        st.GPrev = G;

        const double v = r / (s * drive);
        const double y = v - st.dcX + dcR * st.dcY;
        st.dcX = v;
        st.dcY = y;

        data[i] = (float) (x + y);
    }
}
}
