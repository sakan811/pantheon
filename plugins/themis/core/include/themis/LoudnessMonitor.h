// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "Loudness.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace themis
{
// Programme loudness (ITU-R BS.1770 / EBU R128) of the signal flowing through, for judging a master against streaming
// normalisation and for the peak-to-loudness ratio: LoudnessMeter on the audio thread, the results published through
// atomics. Monitoring only: the samples are read, never written.
//
// Threading: process() runs on the audio thread (two biquads per channel per sample; once per 100 ms it walks a 900-bin
// histogram). requestReset() is safe from any thread and is applied by the next process(). prepare() must not overlap
// with process().
class LoudnessMonitor
{
public:
    // The integrated value needs at least this long to mean anything (the meter's own minimum is 400 ms).
    static constexpr double kMinSeconds = 3.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        meter.prepare (fs);
        clear();
    }

    void requestReset() { resetFlag.store (true, std::memory_order_release); }

    // Audio thread.
    void process (const float* l, const float* r, int n)
    {
        if (resetFlag.exchange (false, std::memory_order_acq_rel))
            clear();
        meter.process (l, r, n);
        samples += static_cast<uint64_t> (n);
        published.integrated.store (meter.integrated(), std::memory_order_relaxed);
        published.shortTerm.store (meter.shortTerm(), std::memory_order_relaxed);
        const double elapsed = static_cast<double> (samples) / fs;
        if (elapsed >= kMinSeconds) // before a full 3 s window the short-term value is a partial average
            published.maxShortTerm.store (loudestShortTerm = std::max (loudestShortTerm, meter.shortTerm()), std::memory_order_relaxed);
        published.seconds.store (elapsed, std::memory_order_relaxed);
    }

    // A pending reset counts as "no data" right away.
    bool hasData() const
    {
        return published.seconds.load (std::memory_order_relaxed) >= kMinSeconds && integrated() > LoudnessMeter::kSilence + 1.0f
               && !resetFlag.load (std::memory_order_acquire);
    }

    float integrated() const { return published.integrated.load (std::memory_order_relaxed); }       // LUFS since the reset, gated
    float shortTerm() const { return published.shortTerm.load (std::memory_order_relaxed); }         // LUFS over the last 3 s
    float maxShortTerm() const { return published.maxShortTerm.load (std::memory_order_relaxed); }   // loudest 3 s since the reset
    double seconds() const { return published.seconds.load (std::memory_order_relaxed); }            // audio measured since the reset

private:
    void clear()
    {
        meter.reset();
        samples = 0;
        loudestShortTerm = LoudnessMeter::kSilence;
        published.integrated.store (LoudnessMeter::kSilence, std::memory_order_relaxed);
        published.shortTerm.store (LoudnessMeter::kSilence, std::memory_order_relaxed);
        published.maxShortTerm.store (LoudnessMeter::kSilence, std::memory_order_relaxed);
        published.seconds.store (0.0, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    LoudnessMeter meter;
    uint64_t samples = 0;
    float loudestShortTerm = LoudnessMeter::kSilence;
    std::atomic<bool> resetFlag { false };

    struct Published
    {
        std::atomic<float> integrated { LoudnessMeter::kSilence }, shortTerm { LoudnessMeter::kSilence }, maxShortTerm { LoudnessMeter::kSilence };
        std::atomic<double> seconds { 0.0 };
    } published;
};
} // namespace themis
