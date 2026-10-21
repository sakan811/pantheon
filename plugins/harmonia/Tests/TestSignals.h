#pragma once

// Synthetic singing voice + measurement helpers used by the offline tests.

#include "../Source/dsp/AutoTuneEngine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace testsig
{
constexpr double kPi = 3.14159265358979323846;

// Pass/fail reporting shared by the test programs.
inline int failures = 0;
inline void check (bool ok, const char* what, double value, const char* unit = "")
{
    std::printf ("  [%s] %-58s %8.3f %s\n", ok ? "PASS" : "FAIL", what, value, unit);
    if (! ok)
        ++failures;
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    // 2-pole resonator, unity gain at DC
    void setResonator (double fc, double bw, double fs)
    {
        const double r = std::exp (-kPi * bw / fs);
        const double th = 2.0 * kPi * fc / fs;
        a1 = -2.0 * r * std::cos (th);
        a2 = r * r;
        b0 = 1.0 + a1 + a2;
    }
    double process (double x)
    {
        const double y = b0 * x - a1 * z1 - a2 * z2;
        z2 = z1;
        z1 = y;
        return y;
    }
};

struct Vowel
{
    double f[3];
    double bw[3];
};
inline const Vowel kAh { { 700, 1220, 2600 }, { 90, 110, 170 } };
inline const Vowel kEe { { 300, 2300, 3000 }, { 70, 150, 200 } };

// f0Hz(t) returns the fundamental at time t seconds, or <= 0 for a silent/unvoiced gap
// (breathy noise only when `noiseInGaps`).
inline std::vector<float> makeVoice (double fs, double seconds, const std::function<double (double)>& f0Hz,
                                     const Vowel& v = kAh, unsigned seed = 1, double jitter = 0.002)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> nd (0.0, 1.0);
    const int n = (int) (seconds * fs);
    std::vector<float> out ((size_t) n);

    Biquad res[3];
    for (int i = 0; i < 3; ++i)
        res[i].setResonator (v.f[i], v.bw[i], fs);

    double phase = 0.0, prevG = 0.0;
    double jit = 1.0, amp = 1.0;
    double envState = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double f0 = f0Hz (t);
        const bool voiced = f0 > 0.0;

        double src = 0.0;
        if (voiced)
        {
            phase += f0 * jit / fs;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                jit = 1.0 + jitter * nd (rng);
                amp = 1.0 + 0.03 * nd (rng);
            }
            // Rosenberg glottal flow, then differentiate (lip radiation).
            const double Tp = 0.40, Tn = 0.16;
            double g;
            if (phase < Tp)
                g = 0.5 * (1.0 - std::cos (kPi * phase / Tp));
            else if (phase < Tp + Tn)
                g = std::cos (kPi * (phase - Tp) / (2.0 * Tn));
            else
                g = 0.0;
            src = (g - prevG) * 40.0 * amp;
            prevG = g;
        }
        else
            prevG = 0.0;

        const double target = voiced ? 1.0 : 0.0;
        envState += (target - envState) * (1.0 - std::exp (-1.0 / (0.004 * fs)));

        double x = src * envState + 0.02 * nd (rng) * (voiced ? 1.0 : 0.0);
        for (auto& r : res)
            x = r.process (x);
        out[(size_t) i] = (float) x;
    }

    float peak = 0.0f;
    for (float s : out)
        peak = std::max (peak, std::fabs (s));
    if (peak > 0.0f)
        for (float& s : out)
            s *= 0.5f / peak;
    return out;
}

inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

struct PitchSample
{
    double t; // seconds (centre of analysis window)
    double midi;
    float conf;
};

// Independent pitch measurement of a rendered signal.
inline std::vector<PitchSample> measurePitch (const std::vector<float>& x, double fs, float minHz = 70, float maxHz = 1000)
{
    harmonia::PitchDetector det;
    det.prepare (fs, 60.0f);
    const int hop = (int) (fs * 0.005);
    std::vector<PitchSample> out;
    float prev = 0.0f;
    bool was = false;
    for (size_t i = 0; i < x.size(); ++i)
    {
        det.push (x[i]);
        if ((int) ((i + 1) % (size_t) hop) == 0)
        {
            const auto r = det.detect (minHz, maxHz, prev, was);
            was = r.voiced;
            if (r.voiced)
            {
                prev = r.period;
                const double centre = (double) (i + 1) - 0.5 * (det.windowSamples() + r.period);
                out.push_back ({ centre / fs, hzToMidi (fs / r.period), r.confidence });
            }
            else
                prev = 0.0f;
        }
    }
    return out;
}

struct Stats
{
    double mean = 0, stddev = 0, maxAbs = 0;
    int n = 0;
};

inline Stats stats (const std::vector<double>& v)
{
    Stats s;
    s.n = (int) v.size();
    if (v.empty())
        return s;
    for (double x : v)
        s.mean += x;
    s.mean /= (double) v.size();
    for (double x : v)
    {
        s.stddev += (x - s.mean) * (x - s.mean);
        s.maxAbs = std::max (s.maxAbs, std::fabs (x));
    }
    s.stddev = std::sqrt (s.stddev / (double) v.size());
    return s;
}

// Pitch error in cents of the measured pitch against `targetMidi`, for samples with t0 <= t <= t1.
inline std::vector<double> centsVs (const std::vector<PitchSample>& ps, double t0, double t1, double targetMidi)
{
    std::vector<double> v;
    for (auto& p : ps)
        if (p.t >= t0 && p.t <= t1)
            v.push_back ((p.midi - targetMidi) * 100.0);
    return v;
}

// Run a mono signal through the engine in odd-sized blocks (exercises block handling).
inline std::vector<float> runEngine (harmonia::AutoTuneEngine& eng, const std::vector<float>& in, int block = 480)
{
    std::vector<float> out (in.size());
    std::vector<float> tmp ((size_t) block);
    const int shorter = block > 64 ? block - 37 : std::max (1, block - 1);
    size_t pos = 0;
    int b = block;
    while (pos < in.size())
    {
        const size_t n = std::min ((size_t) b, in.size() - pos);
        std::copy (in.begin() + (long) pos, in.begin() + (long) (pos + n), tmp.begin());
        float* chans[1] = { tmp.data() };
        eng.process (chans, 1, (int) n);
        std::copy (tmp.begin(), tmp.begin() + (long) n, out.begin() + (long) pos);
        pos += n;
        b = (b == block) ? shorter : block; // alternate block sizes
    }
    return out;
}
} // namespace testsig
