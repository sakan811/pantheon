#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>
#include <vector>

// Signal sources shared by the test executables.
namespace test
{
constexpr double fs = 48000.0;

// Sum of sines: partials are { frequency Hz, amplitude }.
inline std::vector<float> sines (std::initializer_list<std::pair<double, double>> partials, int n)
{
    std::vector<float> x ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
        for (auto [f, a] : partials)
            x[(size_t) i] += (float) (a * std::sin (2.0 * M_PI * f * i / fs));
    return x;
}

// Pink-ish noise (Paul Kellet's economy filter).
class PinkNoise
{
public:
    explicit PinkNoise (unsigned seed) : rng (seed) {}

    float next()
    {
        const double w = dist (rng);
        b1 = 0.99765 * b1 + w * 0.0990460;
        b2 = 0.96300 * b2 + w * 0.2965164;
        b3 = 0.57000 * b3 + w * 1.0526913;
        return (float) ((b1 + b2 + b3 + w * 0.1848) * 0.035);
    }

private:
    std::mt19937 rng;
    std::uniform_real_distribution<double> dist { -1.0, 1.0 };
    double b1 = 0, b2 = 0, b3 = 0;
};

// Programme material whose fundamental is missing: 110/165/220 Hz (a 55 Hz series without its
// first harmonic) over pink noise. What the plugin is designed to fill.
class MissingFundamentalProgramme
{
public:
    explicit MissingFundamentalProgramme (unsigned seed = 3) : pink (seed) {}

    void fill (juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double s = t / fs;
            const float tone = (float) (0.16 * std::sin (2 * M_PI * 110 * s) + 0.14 * std::sin (2 * M_PI * 165 * s)
                                        + 0.08 * std::sin (2 * M_PI * 220 * s));
            const float v = pink.next() + tone;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, v);
            t += 1.0;
        }
    }

private:
    PinkNoise pink;
    double t = 0;
};

// A sub-bass note that comes and goes (one second on, one second off) over faint pink noise: the band
// has energy, and nothing above it. What an adaptive harmonic fill is for.
class SubBassProgramme
{
public:
    explicit SubBassProgramme (unsigned seed = 5) : pink (seed) {}

    void fill (juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double s = t / fs;
            const bool on = std::fmod (s, 2.0) < 1.0;
            const float v = 0.15f * pink.next() + (on ? (float) (0.35 * std::sin (2 * M_PI * 52 * s)) : 0.0f);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, v);
            t += 1.0;
        }
    }

private:
    PinkNoise pink;
    double t = 0;
};
}
