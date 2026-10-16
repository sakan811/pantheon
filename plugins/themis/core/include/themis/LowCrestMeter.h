#pragma once

#include "Biquad.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace themis
{
// Low-end crest factor (peak / RMS in dB) of the 20-250 Hz band of a stereo signal (analysed as mid),
// the same band and quantity as the "Low End Crest Factor" view in iZotope Tonal Balance Control.
// Low values = a compressed / sustained low end, high values = a punchy / dynamic one.
//
// The signal is cut into ~0.4 s windows; each window's crest factor is averaged over time (exponential
// average, or cumulative for averaging time 0), skipping windows that are effectively silent.
//
// Threading: process() is cheap (a few biquads per sample, no allocation) and runs on the audio thread;
// the published values are atomics. requestReset() is safe from any thread and is applied by process().
// prepare() must not overlap with process().
class LowCrestMeter
{
public:
    static constexpr double kLowHz = 20.0, kHighHz = 250.0;
    static constexpr double kWindowSeconds = 0.4;
    static constexpr float kGateDb = -60.0f; // windows whose band RMS is below this (dBFS) are ignored
    static constexpr int kMinWindows = 5;    // ~2 s before a value is shown

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        windowSamples = std::max (1, static_cast<int> (kWindowSeconds * fs));
        const auto hp = BiquadCoeffs::highPass (fs, kLowHz, 0.7071);
        const auto lp = BiquadCoeffs::lowPass (fs, kHighHz, 0.7071); // two in series: 4th-order Linkwitz-Riley
        highPass.setCoeffs (hp);
        lowPass1.setCoeffs (lp);
        lowPass2.setCoeffs (lp);
        setAveragingSeconds (30.0f);
        reset();
    }

    // 0 = infinite (cumulative mean).
    void setAveragingSeconds (float s) { avgSeconds.store (s, std::memory_order_relaxed); }

    void requestReset() { resetFlag.store (true, std::memory_order_release); }

    // Audio thread.
    void process (const float* l, const float* r, int n)
    {
        if (resetFlag.exchange (false, std::memory_order_acq_rel))
            reset();

        for (int i = 0; i < n; ++i)
        {
            const float x = lowPass2.process (lowPass1.process (highPass.process ((l[i] + r[i]) * 0.5f)));
            peak = std::max (peak, std::abs (x));
            sumSquares += static_cast<double> (x) * x;
            if (++windowCount >= windowSamples)
                finishWindow();
        }
    }

    // A pending reset counts as "no data" right away, even if no audio is flowing to apply it.
    bool hasData() const
    {
        return published.windows.load (std::memory_order_relaxed) >= kMinWindows && !resetFlag.load (std::memory_order_acquire);
    }

    // Averaged crest factor in dB (valid when hasData()).
    float crestDb() const { return published.crestDb.load (std::memory_order_relaxed); }

private:
    void finishWindow()
    {
        if (!std::isfinite (sumSquares)) // a NaN/inf input would otherwise poison the filter state for good
        {
            resetFilters();
            clearWindow();
            return;
        }
        const double rms = std::sqrt (sumSquares / windowCount);
        const float rmsDb = 20.0f * std::log10 (static_cast<float> (rms) + 1.0e-20f);
        if (warmingUp)
            warmingUp = false; // the first window contains the filters' start-up transient
        else if (rmsDb >= kGateDb)
        {
            const float crest = 20.0f * std::log10 (peak / static_cast<float> (rms));
            ++windows;
            const float tau = avgSeconds.load (std::memory_order_relaxed);
            double alpha = tau <= 0.0f ? 0.0 : 1.0 - std::exp (-kWindowSeconds / tau);
            alpha = std::max (alpha, 1.0 / windows); // cumulative mean until the EMA catches up
            avg += alpha * (static_cast<double> (crest) - avg);
            published.crestDb.store (static_cast<float> (avg), std::memory_order_relaxed);
            published.windows.store (windows, std::memory_order_relaxed);
        }
        clearWindow();
    }

    void clearWindow()
    {
        peak = 0.0f;
        sumSquares = 0.0;
        windowCount = 0;
    }

    void resetFilters()
    {
        highPass.reset();
        lowPass1.reset();
        lowPass2.reset();
        warmingUp = true;
    }

    void reset()
    {
        resetFilters();
        clearWindow();
        windows = 0;
        avg = 0.0;
        published.windows.store (0, std::memory_order_relaxed);
        published.crestDb.store (0.0f, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    int windowSamples = 19200;
    Biquad highPass, lowPass1, lowPass2;
    float peak = 0.0f;
    double sumSquares = 0.0;
    int windowCount = 0;
    int windows = 0;
    bool warmingUp = true;
    double avg = 0.0;

    std::atomic<float> avgSeconds { 30.0f };
    std::atomic<bool> resetFlag { false };

    struct Published
    {
        std::atomic<float> crestDb { 0.0f };
        std::atomic<int> windows { 0 };
    } published;
};
} // namespace themis
