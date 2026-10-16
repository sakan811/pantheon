#pragma once

#include "FFT.h"

#include <array>
#include <atomic>
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

// Long-term constant-Q spectrum of a stereo signal (analysed as mid). Audio-thread writes,
// UI-thread reads published values.
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
        fft = std::make_unique<FFT> (fftSize);
        window.resize (static_cast<size_t> (fftSize));
        double wsum2 = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / fftSize);
            window[static_cast<size_t> (i)] = static_cast<float> (w);
            wsum2 += w * w;
        }
        powerNorm = static_cast<float> (1.0 / wsum2);
        ring.assign (static_cast<size_t> (fftSize), 0.0f);
        re.assign (static_cast<size_t> (fftSize), 0.0f);
        im.assign (static_cast<size_t> (fftSize), 0.0f);
        buildBandMap();
        setAveragingSeconds (30.0f);
        reset();
    }

    // 0 = infinite (cumulative mean).
    void setAveragingSeconds (float s) { avgSeconds.store (s, std::memory_order_relaxed); }

    // Safe from any thread; applied on the next process() call.
    void requestReset() { resetFlag.store (true, std::memory_order_release); }

    void process (const float* l, const float* r, int n)
    {
        if (resetFlag.exchange (false, std::memory_order_acq_rel))
            reset();

        for (int i = 0; i < n; ++i)
        {
            ring[static_cast<size_t> (writePos)] = (l[i] + r[i]) * 0.5f;
            writePos = (writePos + 1) % fftSize;
            ++samplesSeen;
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                if (samplesSeen >= static_cast<size_t> (fftSize)) // ring holds a full frame of real data
                    analyseFrame();
            }
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

    void analyseFrame()
    {
        // Gate on RMS of the analysed segment.
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float v = ring[static_cast<size_t> ((writePos + i) % fftSize)];
            re[static_cast<size_t> (i)] = v * window[static_cast<size_t> (i)];
            im[static_cast<size_t> (i)] = 0.0f;
            energy += static_cast<double> (v) * v;
        }
        const float rmsDb = 10.0f * std::log10 (static_cast<float> (energy / fftSize) + 1.0e-20f);
        if (rmsDb < kGateDb)
            return;

        fft->transform (re.data(), im.data());

        // power spectrum, stored in place of the real part
        const int half = fftSize / 2;
        for (int k = 0; k <= half; ++k)
        {
            const size_t s = static_cast<size_t> (k);
            re[s] = (re[s] * re[s] + im[s] * im[s]) * powerNorm;
        }

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
                    e += re[static_cast<size_t> (k)];
            }
            else
            {
                const float p0 = re[static_cast<size_t> (m.interpLo)];
                const float p1 = re[static_cast<size_t> (std::min (m.interpLo + 1, half))];
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
        std::fill (ring.begin(), ring.end(), 0.0f);
        avg.fill (0.0);
        writePos = 0;
        sinceHop = static_cast<int> (hopPhase * hop);
        samplesSeen = 0;
        frameCount = 0;
        for (auto& v : published.db)
            v.store (-100.0f, std::memory_order_relaxed);
        published.frames.store (0, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    int fftSize = 16384, hop = 4096;
    std::unique_ptr<FFT> fft;
    std::vector<float> window, ring, re, im;
    float powerNorm = 1.0f;
    std::array<BandMap, BandGrid::kNum> bandMap {};
    std::array<double, BandGrid::kNum> avg {};
    int writePos = 0, sinceHop = 0;
    size_t samplesSeen = 0;
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
