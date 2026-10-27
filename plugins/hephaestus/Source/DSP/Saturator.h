#pragma once

#include "Waveshaper.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace hep
{
// Three-band harmonic saturator.
//
//   input -> LR4 split (low | mid | high) -> [oversample -> ADAA waveshaper -> decimate] per band
//         -> sum
//
// The crossovers are 4th-order Linkwitz-Riley (flat magnitude sum, minimum phase, no latency);
// the low band is run through an all-pass at the upper crossover so the three bands sum flat.
// The only latency comes from the linear-phase oversampling filters.
class Saturator
{
public:
    static constexpr int numBands = 3;
    static constexpr int maxChannels = 2;
    static constexpr int numOsModes = 4; // off, 2x, 4x, 8x

    struct Params
    {
        int curve = 0;                      // 0 tanh, 1 atan, 2 algebraic
        double bias = 0.0;                  // pre-shaper offset; > 0 adds even harmonics
        double drive[numBands] = { 1, 1, 1 }; // linear
        double xoverLow = 150.0, xoverHigh = 3500.0;
    };

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) maxChannels };

        for (auto* f : { &lowSplit, &highSplit, &lowAllpass })
            f->prepare (spec);

        lowSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        highSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

        for (auto& b : bandBuf)
            b.setSize (maxChannels, maxBlock, false, false, true);

        for (int mode = 1; mode < numOsModes; ++mode)
            for (int b = 0; b < numBands; ++b)
            {
                auto os = std::make_unique<juce::dsp::Oversampling<float>> (
                    (size_t) maxChannels, (size_t) mode,
                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
                os->initProcessing ((size_t) maxBlock);
                oversampler[(size_t) mode][(size_t) b] = std::move (os);
            }

        cutLow = cutHigh = -1.0;
        reset();
    }

    // Latency (whole samples) introduced by an oversampling mode.
    int latencyFor (int osMode) const
    {
        if (osMode <= 0) return 0;
        return (int) std::lround (oversampler[(size_t) osMode][0]->getLatencyInSamples());
    }

    void reset()
    {
        for (auto* f : { &lowSplit, &highSplit, &lowAllpass })
            f->reset();

        resetShapers();
        driveLast = { 1.0, 1.0, 1.0 };
    }

    // In-place over `ch` channel pointers. Result is delayed by latencyFor(osMode).
    void process (float* const* ch, int nCh, int n, int osMode, const Params& p)
    {
        updateCrossovers (p);

        // Split.
        for (int c = 0; c < nCh; ++c)
        {
            auto* low = bandBuf[0].getWritePointer (c);
            auto* mid = bandBuf[1].getWritePointer (c);
            auto* high = bandBuf[2].getWritePointer (c);

            for (int i = 0; i < n; ++i)
            {
                float lo, rest, m, h;
                lowSplit.processSample (c, ch[c][i], lo, rest);
                highSplit.processSample (c, rest, m, h);
                low[i] = lowAllpass.processSample (c, lo);
                mid[i] = m;
                high[i] = h;
            }
        }

        // Shape every band.
        for (int b = 0; b < numBands; ++b)
        {
            float* bandPtrs[maxChannels] = { bandBuf[(size_t) b].getWritePointer (0),
                                             bandBuf[(size_t) b].getWritePointer (nCh > 1 ? 1 : 0) };
            const double dcR = dcCoefficient (osMode);
            const double dStart = driveLast[(size_t) b], dEnd = p.drive[b];
            driveLast[(size_t) b] = dEnd;

            if (osMode <= 0)
            {
                for (int c = 0; c < nCh; ++c)
                    shape (p.curve, bandPtrs[c], n, dStart, dEnd, p.bias, dcR, adaa[(size_t) b][(size_t) c]);
            }
            else
            {
                auto& os = *oversampler[(size_t) osMode][(size_t) b];
                juce::dsp::AudioBlock<float> block (bandPtrs, (size_t) nCh, (size_t) n);
                auto up = os.processSamplesUp (block);

                for (int c = 0; c < nCh; ++c)
                    shape (p.curve, up.getChannelPointer ((size_t) c), (int) up.getNumSamples(),
                           dStart, dEnd, p.bias, dcR, adaa[(size_t) b][(size_t) c]);

                os.processSamplesDown (block);
            }
        }

        // Recombine.
        for (int c = 0; c < nCh; ++c)
        {
            const float* low = bandBuf[0].getReadPointer (c);
            const float* mid = bandBuf[1].getReadPointer (c);
            const float* high = bandBuf[2].getReadPointer (c);
            for (int i = 0; i < n; ++i)
                ch[c][i] = low[i] + mid[i] + high[i];
        }
    }

    // Switching oversampling invalidates the shaper history (it was recorded at another rate).
    void onOversamplingChanged() { resetShapers(); }

private:
    void resetShapers()
    {
        for (auto& modes : oversampler)
            for (auto& os : modes)
                if (os != nullptr)
                    os->reset();

        for (auto& band : adaa)
            for (auto& s : band)
                s.reset();
    }

    // 5 Hz DC blocker coefficient at the rate the shaper runs at.
    double dcCoefficient (int osMode) const noexcept
    {
        return 1.0 - 2.0 * juce::MathConstants<double>::pi * 5.0 / (fs * (double) (1 << std::max (osMode, 0)));
    }

    static void shape (int curve, float* data, int n, double d0, double d1, double bias, double dcR,
                       AdaaState& st) noexcept
    {
        switch (curve)
        {
            case 1:  shapeBlock<AtanCurve>      (data, n, d0, d1, bias, dcR, st); break;
            case 2:  shapeBlock<AlgebraicCurve> (data, n, d0, d1, bias, dcR, st); break;
            default: shapeBlock<TanhCurve>      (data, n, d0, d1, bias, dcR, st); break;
        }
    }

    void updateCrossovers (const Params& p)
    {
        // Glide towards the target so moving a crossover knob doesn't zipper.
        const double lowTarget = std::clamp (p.xoverLow, 20.0, fs * 0.2);
        const double highTarget = std::clamp (p.xoverHigh, lowTarget * 2.0, fs * 0.45);
        cutLow  = cutLow  < 0.0 ? lowTarget  : cutLow  + 0.2 * (lowTarget  - cutLow);
        cutHigh = cutHigh < 0.0 ? highTarget : cutHigh + 0.2 * (highTarget - cutHigh);

        lowSplit.setCutoffFrequency ((float) cutLow);
        highSplit.setCutoffFrequency ((float) cutHigh);
        lowAllpass.setCutoffFrequency ((float) cutHigh);
    }

    double fs = 48000.0, cutLow = -1.0, cutHigh = -1.0;
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowAllpass;
    std::array<juce::AudioBuffer<float>, numBands> bandBuf;
    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, numBands>, numOsModes> oversampler;
    std::array<std::array<AdaaState, maxChannels>, numBands> adaa;
    std::array<double, numBands> driveLast { 1.0, 1.0, 1.0 };
};
}
