#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace themis
{
inline constexpr int kNumEnergyRanges = 4;

// Level that stands for "no signal" in the history (digital silence, or no fundamental found).
inline constexpr float kHistoryFloorDb = -120.0f;
inline bool hasLevel (float db) { return db > kHistoryFloorDb + 1.0f; }

// What one analysed frame contributes to the time graphs. Levels are mean-square levels in dBFS (a full-scale sine
// reads -3 dB).
struct FrameStats
{
    std::array<float, kNumEnergyRanges> rangeDb {}; // stereo energy (mid + side) in each energy range
    float f0Hz = 0.0f;                              // tracked bass fundamental, 0 when none was found
    float fundamentalDb = kHistoryFloorDb;
    float harmonicsDb = kHistoryFloorDb; // summed energy of the fundamental's harmonics

    bool hasFundamental() const { return f0Hz > 0.0f; }
    // The fundamental's levels, or the floor when none was found (what a graph of them should plot).
    float fundamentalLevelDb() const { return hasFundamental() ? fundamentalDb : kHistoryFloorDb; }
    float harmonicsLevelDb() const { return hasFundamental() ? harmonicsDb : kHistoryFloorDb; }

    static constexpr size_t kFields = kNumEnergyRanges + 3;

    // Calls f(value) for every field in storage order; the one place that lists them (push and copy both use it).
    template <typename Self, typename F> // Self: FrameStats or const FrameStats
    static void forEachField (Self& s, F&& f)
    {
        for (auto& v : s.rangeDb)
            f (v);
        f (s.f0Hz);
        f (s.fundamentalDb);
        f (s.harmonicsDb);
    }
};

// Fixed-size ring of recent FrameStats. One writer (the analysis thread), any number of readers (UI). Every field
// is an atomic, and the writer would have to lap the whole ring (~85 s of frames) to touch an entry a reader is
// copying, so no further locking is needed.
class FrameHistory
{
public:
    static constexpr int kCapacity = 1024;

    // Writer thread.
    void setFramePeriod (double seconds) { period.store (seconds, std::memory_order_relaxed); }
    void clear() { count.store (0, std::memory_order_release); }

    void push (const FrameStats& s)
    {
        const uint64_t n = count.load (std::memory_order_relaxed);
        size_t i = static_cast<size_t> (n % kCapacity) * FrameStats::kFields;
        FrameStats::forEachField (s, [&] (float v) { data[i++].store (v, std::memory_order_relaxed); });
        count.store (n + 1, std::memory_order_release);
    }

    // Any thread.
    double framePeriodSeconds() const { return period.load (std::memory_order_relaxed); }

    // Frames pushed since the last clear(); changes whenever there is something new to draw.
    uint64_t sequence() const { return count.load (std::memory_order_acquire); }

    // Copies the newest frames, oldest first, into out[0..n); returns how many were available.
    int copyRecent (FrameStats* out, int n) const
    {
        const uint64_t total = count.load (std::memory_order_acquire);
        const int have = static_cast<int> (std::min<uint64_t> ({ total, static_cast<uint64_t> (kCapacity), static_cast<uint64_t> (std::max (n, 0)) }));
        for (int i = 0; i < have; ++i)
        {
            size_t j = static_cast<size_t> ((total - static_cast<uint64_t> (have - i)) % kCapacity) * FrameStats::kFields;
            FrameStats::forEachField (out[i], [&] (float& v) { v = data[j++].load (std::memory_order_relaxed); });
        }
        return have;
    }

private:
    std::array<std::atomic<float>, kCapacity * FrameStats::kFields> data {};
    std::atomic<uint64_t> count { 0 };
    std::atomic<double> period { 0.1 };
};
} // namespace themis
