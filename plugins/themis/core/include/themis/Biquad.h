#pragma once

#include "Common.h"

#include <complex>

namespace themis
{
// Normalised biquad coefficients (a0 == 1): y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // RBJ cookbook designs.
    static BiquadCoeffs peak (double fs, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        return normalise (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                          1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double t = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) - (A - 1) * cw + t),
                          2.0 * A * ((A - 1) - (A + 1) * cw),
                          A * ((A + 1) - (A - 1) * cw - t),
                          (A + 1) + (A - 1) * cw + t,
                          -2.0 * ((A - 1) + (A + 1) * cw),
                          (A + 1) + (A - 1) * cw - t);
    }

    static BiquadCoeffs highShelf (double fs, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double t = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) + (A - 1) * cw + t),
                          -2.0 * A * ((A - 1) + (A + 1) * cw),
                          A * ((A + 1) + (A - 1) * cw - t),
                          (A + 1) - (A - 1) * cw + t,
                          2.0 * ((A - 1) - (A + 1) * cw),
                          (A + 1) - (A - 1) * cw - t);
    }

    static BiquadCoeffs highPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5,
                          1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    static BiquadCoeffs lowPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * kPi * clampFreq (f, fs) / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
                          1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    // |H(e^jw)| in dB at frequency f.
    double magnitudeDb (double fs, double f) const
    {
        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return 20.0 * std::log10 (std::max (std::abs (num / den), 1.0e-12));
    }

private:
    static double clampFreq (double f, double fs) { return std::clamp (f, 1.0, fs * 0.499); }

    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        BiquadCoeffs c;
        c.b0 = b0 / a0;
        c.b1 = b1 / a0;
        c.b2 = b2 / a0;
        c.a1 = a1 / a0;
        c.a2 = a2 / a0;
        return c;
    }
};

// Transposed direct form II with double-precision state (stable at low cutoffs).
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) { co = c; }
    const BiquadCoeffs& coeffs() const { return co; }
    void reset() { z1 = z2 = 0.0; }

    float process (float xf)
    {
        const double x = xf;
        const double y = co.b0 * x + z1;
        z1 = co.b1 * x - co.a1 * y + z2;
        z2 = co.b2 * x - co.a2 * y;
        return static_cast<float> (y);
    }

private:
    BiquadCoeffs co;
    double z1 = 0.0, z2 = 0.0;
};
} // namespace themis
