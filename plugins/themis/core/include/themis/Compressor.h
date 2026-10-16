#pragma once

#include "Biquad.h"

namespace themis
{
struct CompParams
{
    float thresholdDb = -18.0f;
    float ratio = 2.0f;
    float attackMs = 30.0f;
    float releaseMs = 200.0f;
    float makeupDb = 0.0f;
    float mix = 1.0f;            // 1 = fully compressed, <1 = parallel
    float sidechainHpHz = 90.0f; // keeps bass from pumping the whole mix
};

// Stereo-linked feed-forward glue compressor, soft knee, gain smoothed in the dB domain.
class Compressor
{
public:
    static constexpr float kKneeDb = 6.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        grState = 0.0;
        for (auto& f : hp)
            f.reset();
        lastHp = -1.0f;
        grDb = 0.0f;
    }

    void process (float* l, float* r, int n, const CompParams& p)
    {
        if (std::abs (p.sidechainHpHz - lastHp) > 1.0e-6f)
        {
            const auto c = BiquadCoeffs::highPass (fs, p.sidechainHpHz, 0.7071);
            hp[0].setCoeffs (c);
            hp[1].setCoeffs (c);
            lastHp = p.sidechainHpHz;
        }
        const double atk = onePoleCoeff (p.attackMs * 0.001, fs);
        const double rel = onePoleCoeff (p.releaseMs * 0.001, fs);
        const float makeup = dbToLin (p.makeupDb);
        const float slope = 1.0f / std::max (p.ratio, 1.0f) - 1.0f;
        float worst = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float sl = hp[0].process (l[i]), sr = hp[1].process (r[i]);
            const float levelDb = linToDb (std::max (std::abs (sl), std::abs (sr)));
            const float over = levelDb - p.thresholdDb;

            float targetGr; // <= 0
            if (2.0f * over < -kKneeDb)
                targetGr = 0.0f;
            else if (2.0f * std::abs (over) <= kKneeDb)
            {
                const float t = over + kKneeDb * 0.5f;
                targetGr = slope * t * t / (2.0f * kKneeDb);
            }
            else
                targetGr = slope * over;

            const double coef = targetGr < grState ? atk : rel;
            grState = coef * grState + (1.0 - coef) * targetGr;
            worst = std::min (worst, static_cast<float> (grState));

            const float g = dbToLin (static_cast<float> (grState)) * makeup;
            l[i] = l[i] * (1.0f - p.mix) + l[i] * g * p.mix;
            r[i] = r[i] * (1.0f - p.mix) + r[i] * g * p.mix;
        }
        grDb = worst;
    }

    float gainReductionDb() const { return grDb; }

private:
    double fs = 48000.0;
    double grState = 0.0;
    Biquad hp[2];
    float lastHp = -1.0f;
    float grDb = 0.0f;
};
} // namespace themis
