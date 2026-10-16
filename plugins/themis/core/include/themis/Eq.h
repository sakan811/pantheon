#pragma once

#include "Biquad.h"

#include <array>

namespace themis
{
enum class EqTarget : int { Stereo = 0, Mid = 1, Side = 2 };

enum class EqBandType : int { HighPass, LowShelf, Bell, HighShelf };

struct EqBandParams
{
    bool enabled = true;
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.8f;
    EqTarget target = EqTarget::Stereo;
};

// 6-band mastering EQ: HPF, low shelf, 3 bells, high shelf. Each band can act on L/R, Mid or Side.
class MasteringEq
{
public:
    static constexpr int kNumBands = 6;
    static constexpr int kChunk = 32;

    static EqBandType typeOf (int band)
    {
        static constexpr EqBandType types[kNumBands] = { EqBandType::HighPass, EqBandType::LowShelf, EqBandType::Bell,
                                                         EqBandType::Bell,     EqBandType::Bell,     EqBandType::HighShelf };
        return types[band];
    }

    static BiquadCoeffs coeffsFor (int band, const EqBandParams& p, double fs)
    {
        switch (typeOf (band))
        {
            case EqBandType::HighPass:  return BiquadCoeffs::highPass (fs, p.freq, 0.7071);
            case EqBandType::LowShelf:  return BiquadCoeffs::lowShelf (fs, p.freq, p.gainDb, 0.7071);
            case EqBandType::HighShelf: return BiquadCoeffs::highShelf (fs, p.freq, p.gainDb, 0.7071);
            case EqBandType::Bell:      return BiquadCoeffs::peak (fs, p.freq, p.gainDb, p.q);
        }
        return {};
    }

    // Combined magnitude response (dB) of all enabled bands, ignoring Mid/Side targeting.
    static double responseDb (const std::array<EqBandParams, kNumBands>& bands, double fs, double f)
    {
        double db = 0.0;
        for (int i = 0; i < kNumBands; ++i)
            if (bands[static_cast<size_t> (i)].enabled)
                db += coeffsFor (i, bands[static_cast<size_t> (i)], fs).magnitudeDb (fs, f);
        return db;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        smoothAlpha = 1.0 - onePoleCoeff (0.02, fs / kChunk);
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            cur[static_cast<size_t> (i)] = target[static_cast<size_t> (i)];
            updateCoeffs (i);
            fA[i].reset();
            fB[i].reset();
        }
    }

    // Call from the audio thread before process(); smoothing is applied internally.
    void setBands (const std::array<EqBandParams, kNumBands>& b)
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            if (b[static_cast<size_t> (i)].enabled && !target[static_cast<size_t> (i)].enabled)
            {
                cur[static_cast<size_t> (i)] = b[static_cast<size_t> (i)];
                updateCoeffs (i);
                fA[i].reset();
                fB[i].reset();
            }
            target[static_cast<size_t> (i)] = b[static_cast<size_t> (i)];
        }
    }

    void process (float* l, float* r, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int len = std::min (kChunk, n - done);
            for (int b = 0; b < kNumBands; ++b)
                smoothBand (b);
            for (int i = done; i < done + len; ++i)
            {
                float xl = l[i], xr = r[i];
                for (int b = 0; b < kNumBands; ++b)
                {
                    if (!cur[static_cast<size_t> (b)].enabled)
                        continue;
                    switch (cur[static_cast<size_t> (b)].target)
                    {
                        case EqTarget::Stereo:
                            xl = fA[b].process (xl);
                            xr = fB[b].process (xr);
                            break;
                        case EqTarget::Mid:
                        {
                            const float m = fA[b].process ((xl + xr) * 0.5f), s = (xl - xr) * 0.5f;
                            xl = m + s;
                            xr = m - s;
                            break;
                        }
                        case EqTarget::Side:
                        {
                            const float m = (xl + xr) * 0.5f, s = fB[b].process ((xl - xr) * 0.5f);
                            xl = m + s;
                            xr = m - s;
                            break;
                        }
                    }
                }
                l[i] = xl;
                r[i] = xr;
            }
            done += len;
        }
    }

private:
    void smoothBand (int b)
    {
        auto& c = cur[static_cast<size_t> (b)];
        const auto& t = target[static_cast<size_t> (b)];
        c.enabled = t.enabled;
        c.target = t.target;
        const double a = smoothAlpha;
        // smooth frequency in the log domain so sweeps feel even
        const double lf = std::log (c.freq) + (std::log (t.freq) - std::log (c.freq)) * a;
        const float nf = static_cast<float> (std::exp (lf));
        const float ng = c.gainDb + static_cast<float> ((t.gainDb - c.gainDb) * a);
        const float nq = c.q + static_cast<float> ((t.q - c.q) * a);
        const bool changed = std::abs (nf - c.freq) > 1.0e-4f * c.freq || std::abs (ng - c.gainDb) > 1.0e-4f
                             || std::abs (nq - c.q) > 1.0e-4f;
        if (changed)
        {
            c.freq = nf;
            c.gainDb = ng;
            c.q = nq;
            updateCoeffs (b);
        }
    }

    void updateCoeffs (int b)
    {
        const auto c = coeffsFor (b, cur[static_cast<size_t> (b)], fs);
        fA[b].setCoeffs (c);
        fB[b].setCoeffs (c);
    }

    double fs = 48000.0;
    double smoothAlpha = 0.1;
    std::array<EqBandParams, kNumBands> cur {}, target {};
    Biquad fA[kNumBands], fB[kNumBands];
};
} // namespace themis
