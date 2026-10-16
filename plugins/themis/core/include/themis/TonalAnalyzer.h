#pragma once

#include "FFT.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace themis
{
// Fixed analysis grid shared by the analyzer, target curves and EQ fitting:
// 1/6-octave constant-Q bands from 20 Hz upward (61 bands, 20 Hz .. ~20.5 kHz).
struct BandGrid
{
    static constexpr int kPerOctave = 6;
    static constexpr int kNum = 61;
    static constexpr double kF0 = 20.0;

    static double centreHz (int i) { return kF0 * std::pow (2.0, static_cast<double> (i) / kPerOctave); }
    static double lowEdgeHz (int i) { return centreHz (i) * std::pow (2.0, -0.5 / kPerOctave); }
    static double highEdgeHz (int i) { return centreHz (i) * std::pow (2.0, 0.5 / kPerOctave); }

    // Fractional band index for a frequency (clamped to the grid).
    static double indexForHz (double f)
    {
        return std::clamp (std::log2 (std::max (f, 1.0) / kF0) * kPerOctave, 0.0, static_cast<double> (kNum - 1));
    }

    // Normalisation region: curves are shifted so their mean over this range is 0 dB.
    static constexpr double kAnchorLoHz = 200.0;
    static constexpr double kAnchorHiHz = 5000.0;
};

using BandArray = std::array<float, BandGrid::kNum>;

// Shifts a dB curve so its mean across the anchor region is 0.
inline void normaliseAnchor (BandArray& db)
{
    const int lo = static_cast<int> (std::ceil (BandGrid::indexForHz (BandGrid::kAnchorLoHz)));
    const int hi = static_cast<int> (std::floor (BandGrid::indexForHz (BandGrid::kAnchorHiHz)));
    double sum = 0.0;
    for (int i = lo; i <= hi; ++i)
        sum += db[static_cast<size_t> (i)];
    const float mean = static_cast<float> (sum / (hi - lo + 1));
    for (auto& v : db)
        v -= mean;
}

// Long-term constant-Q spectrum of a stereo signal (analysed as mid).
//
// Threading: the audio thread only calls push(), which copies the mid signal into a lock-free ring and
// does no FFT work. A worker thread calls pump() every few milliseconds to run the analysis; UI threads
// read the published values. process() = push() + pump() on one thread, for offline use and tests.
// prepare() must not run concurrently with push()/pump().
class TonalAnalyzer
{
public:
    static constexpr float kGateDb = -70.0f; // frames quieter than this (RMS) are ignored

    TonalAnalyzer() = default;

    // phase (0..1) offsets the frame schedule so two analyzers never run an FFT in the same block.
    void prepare (double sampleRate, double phase = 0.0)
    {
        fs = sampleRate;
        hopPhase = std::clamp (phase, 0.0, 0.99);
        int order = 14; // 16384 -> ~2.9 Hz bins at 48 kHz
        if (fs > 50000.0)
            order = 15;
        if (fs > 100000.0)
            order = 16;
        fftSize = 1 << order;
        hop = fftSize / 4;
        fft = std::make_unique<RealFFT> (fftSize);
        window.resize (static_cast<size_t> (fftSize));
        double wsum2 = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / fftSize);
            window[static_cast<size_t> (i)] = static_cast<float> (w);
            wsum2 += w * w;
        }
        powerNorm = static_cast<float> (1.0 / wsum2);
        frame.assign (static_cast<size_t> (fftSize), 0.0f);
        power.assign (static_cast<size_t> (fftSize / 2 + 1), 0.0f);
        ringSize = static_cast<uint64_t> (fftSize) * kRingFrames; // power of two: indices wrap with a mask
        ring.assign (static_cast<size_t> (ringSize), 0.0f);
        writeCount.store (0, std::memory_order_relaxed);
        buildBandMap();
        setAveragingSeconds (30.0f);
        reset();
    }

    // 0 = infinite (cumulative mean).
    void setAveragingSeconds (float s) { avgSeconds.store (s, std::memory_order_relaxed); }

    // Safe from any thread; applied on the next pump().
    void requestReset() { resetFlag.store (true, std::memory_order_release); }

    // Audio thread: queue n samples for analysis. Wait-free, no allocation, no FFT.
    void push (const float* l, const float* r, int n)
    {
        const uint64_t w = writeCount.load (std::memory_order_relaxed);
        const size_t pos = static_cast<size_t> (w & (ringSize - 1));
        const int first = static_cast<int> (std::min<uint64_t> (static_cast<uint64_t> (n), ringSize - pos));
        float* dst = ring.data() + pos;
        for (int i = 0; i < first; ++i)
            dst[i] = (l[i] + r[i]) * 0.5f;
        for (int i = first; i < n; ++i)
            ring[static_cast<size_t> (i - first)] = (l[i] + r[i]) * 0.5f;
        writeCount.store (w + static_cast<uint64_t> (n), std::memory_order_release);
    }

    // Worker thread: analyse every frame that has become available since the last call. If the worker
    // falls so far behind that the ring would be overwritten, the oldest frames are skipped.
    void pump()
    {
        if (resetFlag.exchange (false, std::memory_order_acq_rel))
            reset();

        const uint64_t written = writeCount.load (std::memory_order_acquire);
        // A block still being written may overwrite up to one FFT length beyond `written`, so keep that clear.
        const uint64_t maxLag = ringSize - static_cast<uint64_t> (fftSize);
        while (nextFrameEnd <= written)
        {
            const uint64_t start = nextFrameEnd - static_cast<uint64_t> (fftSize);
            if (written - start > maxLag)
            {
                const uint64_t minEnd = written - maxLag + static_cast<uint64_t> (fftSize);
                const uint64_t hops = (minEnd - nextFrameEnd + static_cast<uint64_t> (hop) - 1) / static_cast<uint64_t> (hop);
                nextFrameEnd += hops * static_cast<uint64_t> (hop);
                continue;
            }
            analyseFrame (start);
            nextFrameEnd += static_cast<uint64_t> (hop);
        }
    }

    // push() + pump() on the calling thread.
    void process (const float* l, const float* r, int n)
    {
        for (int pos = 0; pos < n; pos += hop)
        {
            const int c = std::min (hop, n - pos);
            push (l + pos, r + pos, c);
            pump();
        }
    }

    // True once enough audio has been averaged to be meaningful (~1 s).
    bool hasData() const { return published.frames.load (std::memory_order_relaxed) >= 8; }

    // Latest long-term spectrum, normalised so the anchor region averages 0 dB.
    void readNormalisedDb (BandArray& out) const
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = published.db[i].load (std::memory_order_relaxed);
    }

    // Analyse an entire buffer offline (reference-track learning). Returns false if too short/quiet.
    static bool analyseOffline (const float* l, const float* r, size_t numSamples, double sampleRate, BandArray& out)
    {
        TonalAnalyzer a;
        a.prepare (sampleRate);
        a.setAveragingSeconds (0.0f);
        constexpr int chunk = 4096;
        for (size_t pos = 0; pos < numSamples; pos += chunk)
            a.process (l + pos, r + pos, static_cast<int> (std::min<size_t> (chunk, numSamples - pos)));
        if (!a.hasData())
            return false;
        a.readNormalisedDb (out);
        return true;
    }

private:
    struct BandMap
    {
        int firstBin = 0, lastBin = -1; // inclusive range of FFT bins inside the band
        int interpLo = 0;               // for bands narrower than one bin: interpolate between bins
        float interpFrac = 0.0f, binsWide = 0.0f;
        bool valid = true;
    };

    void buildBandMap()
    {
        const double binHz = fs / fftSize;
        const double nyquist = fs * 0.5;
        for (int b = 0; b < BandGrid::kNum; ++b)
        {
            BandMap m;
            const double lo = BandGrid::lowEdgeHz (b), hi = BandGrid::highEdgeHz (b), c = BandGrid::centreHz (b);
            m.valid = lo < nyquist * 0.98;
            m.firstBin = static_cast<int> (std::ceil (lo / binHz));
            m.lastBin = std::min (static_cast<int> (std::ceil (hi / binHz)) - 1, fftSize / 2);
            if (m.lastBin < m.firstBin)
            {
                const double pos = c / binHz;
                m.interpLo = static_cast<int> (std::floor (pos));
                m.interpFrac = static_cast<float> (pos - m.interpLo);
                m.binsWide = static_cast<float> ((hi - lo) / binHz);
            }
            bandMap[static_cast<size_t> (b)] = m;
        }
    }

    void analyseFrame (uint64_t start)
    {
        // Gate on RMS of the analysed segment.
        const size_t mask = static_cast<size_t> (ringSize - 1);
        const size_t s0 = static_cast<size_t> (start & mask);
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float v = ring[(s0 + static_cast<size_t> (i)) & mask];
            frame[static_cast<size_t> (i)] = v * window[static_cast<size_t> (i)];
            energy += static_cast<double> (v) * v;
        }
        const float rmsDb = 10.0f * std::log10 (static_cast<float> (energy / fftSize) + 1.0e-20f);
        if (rmsDb < kGateDb)
            return;

        fft->powerSpectrum (frame.data(), power.data(), powerNorm);
        const int half = fftSize / 2;

        ++frameCount;
        const double hopSec = static_cast<double> (hop) / fs;
        const float tau = avgSeconds.load (std::memory_order_relaxed);
        double alpha = tau <= 0.0f ? 0.0 : 1.0 - std::exp (-hopSec / tau);
        alpha = std::max (alpha, 1.0 / static_cast<double> (frameCount)); // cumulative mean until EMA catches up

        for (int b = 0; b < BandGrid::kNum; ++b)
        {
            const auto& m = bandMap[static_cast<size_t> (b)];
            double e = 0.0;
            if (!m.valid)
                e = 1.0e-20;
            else if (m.lastBin >= m.firstBin)
            {
                for (int k = m.firstBin; k <= m.lastBin; ++k)
                    e += power[static_cast<size_t> (k)];
            }
            else
            {
                const float p0 = power[static_cast<size_t> (m.interpLo)];
                const float p1 = power[static_cast<size_t> (std::min (m.interpLo + 1, half))];
                e = (p0 + (p1 - p0) * m.interpFrac) * m.binsWide;
            }
            avg[static_cast<size_t> (b)] += alpha * (e - avg[static_cast<size_t> (b)]);
        }
        publish();
    }

    void publish()
    {
        BandArray db;
        for (size_t i = 0; i < db.size(); ++i)
            db[i] = 10.0f * std::log10 (static_cast<float> (avg[i]) + 1.0e-20f);
        normaliseAnchor (db);
        for (size_t i = 0; i < db.size(); ++i)
            published.db[i].store (db[i], std::memory_order_relaxed);
        published.frames.store (frameCount, std::memory_order_relaxed);
    }

    void reset()
    {
        avg.fill (0.0);
        frameCount = 0;
        // Only audio pushed from now on counts. Frames end every `hop` samples once a full frame of new data exists.
        const uint64_t base = writeCount.load (std::memory_order_acquire);
        const uint64_t offset = static_cast<uint64_t> (hop - static_cast<int> (hopPhase * hop));
        nextFrameEnd = base + offset;
        while (nextFrameEnd - base < static_cast<uint64_t> (fftSize))
            nextFrameEnd += static_cast<uint64_t> (hop);
        for (auto& v : published.db)
            v.store (-100.0f, std::memory_order_relaxed);
        published.frames.store (0, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    int fftSize = 16384, hop = 4096;
    static constexpr uint64_t kRingFrames = 16; // ring length in FFT frames (~5 s at 48 kHz)
    std::unique_ptr<RealFFT> fft;
    std::vector<float> window, ring, frame, power;
    float powerNorm = 1.0f;
    std::array<BandMap, BandGrid::kNum> bandMap {};
    std::array<double, BandGrid::kNum> avg {};
    uint64_t ringSize = 0;
    std::atomic<uint64_t> writeCount { 0 }; // total samples pushed since prepare(); the ring index is count & (ringSize-1)
    uint64_t nextFrameEnd = 0;              // worker-side: absolute sample index where the next frame ends
    int frameCount = 0;
    double hopPhase = 0.0;

    std::atomic<float> avgSeconds { 30.0f };
    std::atomic<bool> resetFlag { false };

    struct Published
    {
        std::array<std::atomic<float>, BandGrid::kNum> db;
        std::atomic<int> frames { 0 };
        Published() { for (auto& v : db) v.store (-100.0f); }
    } published;
};
} // namespace themis
