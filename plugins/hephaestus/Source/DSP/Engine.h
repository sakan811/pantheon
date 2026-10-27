#pragma once

#include "AnalyzerTap.h"
#include "FundamentalEngine.h"
#include "Saturator.h"
#include <atomic>
#include <vector>

namespace hep
{
// Whole-number sample delay (power-of-two ring buffer). Latency compensation needs exact
// integer delays; no interpolation, no smoothing.
class IntDelay
{
public:
    void prepare (int maxDelay)
    {
        int size = 16;
        while (size < maxDelay + 2) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
    void setDelay (int d) noexcept { delay = std::clamp (d, 0, mask - 1); }

    inline float process (float x) noexcept
    {
        buffer[(size_t) pos] = x;
        const float y = buffer[(size_t) ((pos - delay) & mask)];
        pos = (pos + 1) & mask;
        return y;
    }

private:
    std::vector<float> buffer;
    int mask = 15, pos = 0, delay = 0;
};

struct EngineParams
{
    float inGainDb = 0.0f, outGainDb = 0.0f, mix = 1.0f;
    bool midSide = false, delta = false, match = false, satOn = true;
    int osMode = 2;

    Saturator::Params sat;
    FundamentalEngine::Params fund;
};

class Engine
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = maxBlockSize;

        saturator.prepare (sampleRate, maxBlockSize);
        fundamental.prepare (sampleRate);

        int maxLatency = 0;
        for (int m = 0; m < Saturator::numOsModes; ++m)
        {
            latencyTable[(size_t) m] = saturator.latencyFor (m);
            maxLatency = std::max (maxLatency, latencyTable[(size_t) m]);
        }

        forEachDelay ([&] (IntDelay& d) { d.prepare (maxLatency + 8); });
        for (auto* b : { &dryBuf, &wetBuf, &fundBuf })
            b->setSize (maxChannels, maxBlockSize, false, false, true);

        currentOs = -1; // forces a latency refresh on the first block
        msDry = msWet = 1.0e-6;
        matchGain = 1.0;
        inGainLast = outGainLast = 1.0f;
        reset();
    }

    void reset()
    {
        saturator.reset();
        fundamental.reset();
        forEachDelay ([] (IntDelay& d) { d.reset(); });
    }

    int getLatency() const noexcept { return latency; }

    void process (juce::AudioBuffer<float>& buffer, const EngineParams& p)
    {
        const int total = buffer.getNumSamples();
        const int nCh = std::min (buffer.getNumChannels(), maxChannels);

        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, std::min (maxBlock, total - start), nCh, p);
    }

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    StereoTap tapIn, tapOut; // pre (delay-aligned dry) and post signal, for the spectrum display

private:
    using Ptrs = std::array<float*, maxChannels>; // a mono buffer repeats channel 0, so no entry is null

    static Ptrs pointersOf (juce::AudioBuffer<float>& b, int start = 0) noexcept
    {
        const int second = b.getNumChannels() > 1 ? 1 : 0;
        return { b.getWritePointer (0, start), b.getWritePointer (second, start) };
    }

    template <class Fn>
    void forEachDelay (Fn&& fn)
    {
        for (auto* set : { &dryDelay, &fundDelay, &bypassDelay })
            for (auto& d : *set)
                fn (d);
    }

    template <class Fn>
    static void forEachSample (int nCh, int n, Fn&& fn)
    {
        for (int c = 0; c < nCh; ++c)
            for (int i = 0; i < n; ++i)
                fn (c, i);
    }

    // Ramps the gain across the chunk and returns the peak of the result.
    static float rampGain (const Ptrs& ch, int nCh, int n, float from, float to) noexcept
    {
        float peak = 0.0f;
        const float step = (to - from) / (float) n;
        for (int c = 0; c < nCh; ++c)
        {
            float g = from;
            for (int i = 0; i < n; ++i)
            {
                g += step;
                ch[(size_t) c][i] *= g;
                peak = std::max (peak, std::fabs (ch[(size_t) c][i]));
            }
        }
        return peak;
    }

    // Mid = (L+R)/2, Side = (L-R)/2: unity through an encode/decode round trip.
    static void midSideEncode (const Ptrs& ch, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = ch[0][i], r = ch[1][i];
            ch[0][i] = 0.5f * (l + r);
            ch[1][i] = 0.5f * (l - r);
        }
    }

    static void midSideDecode (const Ptrs& ch, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = ch[0][i], s = ch[1][i];
            ch[0][i] = m + s;
            ch[1][i] = m - s;
        }
    }

    void applyLatencyFor (int osMode)
    {
        currentOs = osMode;
        latency = latencyTable[(size_t) osMode];
        saturator.onOversamplingChanged();
        forEachDelay ([&] (IntDelay& d) { d.setDelay (latency); d.reset(); });
    }

    // Slow RMS ratio of dry to wet, so loudness differences don't masquerade as tone.
    double matchGainFor (const Ptrs& dry, const Ptrs& wet, int nCh, int n)
    {
        const double tc = 1.0 - std::exp (-(double) n / (0.6 * fs));
        double eDry = 0.0, eWet = 0.0;
        forEachSample (nCh, n, [&] (int c, int i)
        {
            eDry += (double) dry[(size_t) c][i] * dry[(size_t) c][i];
            eWet += (double) wet[(size_t) c][i] * wet[(size_t) c][i];
        });
        msDry += tc * (eDry / (n * nCh) - msDry);
        msWet += tc * (eWet / (n * nCh) - msWet);

        const double target = std::clamp (std::sqrt ((msDry + 1.0e-9) / (msWet + 1.0e-9)), 0.25, 4.0);
        matchGain += 0.02 * (target - matchGain);
        return matchGain;
    }

    void processChunk (juce::AudioBuffer<float>& buffer, int start, int n, int nCh, const EngineParams& p)
    {
        const int os = std::clamp (p.osMode, 0, Saturator::numOsModes - 1);
        if (os != currentOs)
            applyLatencyFor (os);

        const auto io = pointersOf (buffer, start);
        const auto dry = pointersOf (dryBuf), wet = pointersOf (wetBuf), add = pointersOf (fundBuf);
        const bool ms = p.midSide && nCh == 2;

        // 1. Input gain, input meter.
        const float inGain = juce::Decibels::decibelsToGain (p.inGainDb);
        smoothPeak (inPeak, rampGain (io, nCh, n, inGainLast, inGain), n);
        inGainLast = inGain;

        // 2. Dry reference, delayed to line up with the oversampled wet path.
        forEachSample (nCh, n, [&] (int c, int i)
        {
            wet[(size_t) c][i] = io[(size_t) c][i];
            dry[(size_t) c][i] = dryDelay[(size_t) c].process (io[(size_t) c][i]);
        });

        if (ms) midSideEncode (wet, n);

        // 3. Fundamental engine on the clean signal. In mid/side only the mid channel feeds it, so
        //    bass stays centred.
        const bool fundActive = p.fund.extGain > 0.0 || p.fund.resGain > 0.0;
        if (fundActive)
        {
            const float* in[maxChannels] = { wet[0], wet[1] };
            fundamental.process (in, add.data(), ms ? 1 : nCh, n, p.fund);
            if (ms)
                std::fill_n (add[1], n, 0.0f);
        }
        else
        {
            fundamental.reset();
            std::fill_n (add[0], n, 0.0f);
            std::fill_n (add[1], n, 0.0f);
        }
        // Always run the delay so its ring buffer keeps flowing when the engine is switched on later.
        forEachSample (nCh, n, [&] (int c, int i) { add[(size_t) c][i] = fundDelay[(size_t) c].process (add[(size_t) c][i]); });

        // 4. Saturator, or a matching delay when it is switched off; then add the fundamental signal.
        if (p.satOn)
            saturator.process (wet.data(), nCh, n, os, p.sat);
        else
            forEachSample (nCh, n, [&] (int c, int i) { wet[(size_t) c][i] = bypassDelay[(size_t) c].process (wet[(size_t) c][i]); });

        if (fundActive)
            forEachSample (nCh, n, [&] (int c, int i) { wet[(size_t) c][i] += add[(size_t) c][i]; });

        if (ms) midSideDecode (wet, n);

        // 5. Mix / delta, then output gain.
        float gMatch = 1.0f;
        if (p.match) gMatch = (float) matchGainFor (dry, wet, nCh, n);
        else         matchGain = 1.0;

        forEachSample (nCh, n, [&] (int c, int i)
        {
            const float w = wet[(size_t) c][i] * gMatch, d = dry[(size_t) c][i];
            io[(size_t) c][i] = p.delta ? (w - d) : d + p.mix * (w - d);
        });

        const float outGain = juce::Decibels::decibelsToGain (p.outGainDb);
        smoothPeak (outPeak, rampGain (io, nCh, n, outGainLast, outGain), n);
        outGainLast = outGain;

        // 6. Spectrum taps.
        const float* dryOut[maxChannels] = { dry[0], dry[1] };
        const float* ioOut[maxChannels] = { io[0], io[1] };
        tapIn.push (dryOut, nCh, n);
        tapOut.push (ioOut, nCh, n);
    }

    void smoothPeak (std::atomic<float>& meter, float peak, int n) noexcept
    {
        const float decay = std::exp (-(float) n / (0.3f * (float) fs)); // ~300 ms fall-back
        meter.store (std::max (meter.load (std::memory_order_relaxed) * decay, peak), std::memory_order_relaxed);
    }

    double fs = 48000.0;
    int maxBlock = 512, currentOs = -1, latency = 0;
    std::array<int, Saturator::numOsModes> latencyTable {};

    Saturator saturator;
    FundamentalEngine fundamental;
    std::array<IntDelay, maxChannels> dryDelay, fundDelay, bypassDelay;
    juce::AudioBuffer<float> dryBuf, wetBuf, fundBuf;

    double msDry = 1.0e-6, msWet = 1.0e-6, matchGain = 1.0;
    float inGainLast = 1.0f, outGainLast = 1.0f;
};
}
