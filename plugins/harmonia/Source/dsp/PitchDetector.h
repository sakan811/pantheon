#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace harmonia
{
// Ring buffer that always exposes its most recent N samples as one contiguous block
// (every sample is written twice).
class MirrorRing
{
public:
    void resize (int capacity)
    {
        n = capacity;
        buf.assign ((size_t) n * 2, 0.0f);
        head = 0;
    }

    void clear()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        head = 0;
    }

    void push (float v)
    {
        buf[(size_t) head] = v;
        buf[(size_t) (head + n)] = v;
        if (++head == n)
            head = 0;
    }

    // Pointer to the oldest of the last `len` samples. The newest sample is tail(len)[len - 1].
    const float* tail (int len) const { return buf.data() + head + n - len; }

    int capacity() const { return n; }

private:
    std::vector<float> buf;
    int n = 0, head = 0;
};

// YIN pitch detector tuned for sung vocals. It searches at a decimated rate (~24 kHz) and
// then refines the period at the full rate, which keeps the CPU cost low and the
// resolution well below a cent.
class PitchDetector
{
public:
    struct Result
    {
        bool voiced = false;
        float period = 0.0f;     // in full-rate samples, fractional
        float confidence = 0.0f; // 0..1
        float rms = 0.0f;
    };

    // minHz is the lowest pitch that will ever be asked for.
    void prepare (double sampleRate, float minHz)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 24000.0));
        fsDec = fs / decim;
        tauMaxD = (int) std::ceil (fsDec / minHz) + 2;
        winD = tauMaxD;

        const int decNeeded = winD + tauMaxD + 4;
        int cap = 1;
        while (cap < decNeeded)
            cap <<= 1;
        dec.resize (cap);

        const int fullNeeded = (winD + tauMaxD) * decim + 8;
        cap = 1;
        while (cap < fullNeeded)
            cap <<= 1;
        full.resize (cap);

        d.assign ((size_t) tauMaxD + 2, 0.0f);
        cm.assign ((size_t) tauMaxD + 2, 1.0f);
        reset();
    }

    void reset()
    {
        dec.clear();
        full.clear();
        accum = 0.0f;
        accumCount = 0;
    }

    // Number of full-rate samples of history the detector looks at.
    int windowSamples() const { return winD * decim; }
    int maxLagSamples() const { return tauMaxD * decim; }

    void push (float x)
    {
        full.push (x);
        accum += x;
        if (++accumCount == decim)
        {
            dec.push (accum / (float) decim);
            accum = 0.0f;
            accumCount = 0;
        }
    }

    // prevPeriod: last confirmed period in full-rate samples (0 if none).
    Result detect (float minHz, float maxHz, float prevPeriod, bool wasVoiced)
    {
        Result res;

        const int tmin = std::max (2, (int) std::floor (fsDec / maxHz));
        const int tmax = std::min (tauMaxD, (int) std::ceil (fsDec / minHz));
        if (tmax <= tmin + 2)
            return res;

        const int total = winD + tauMaxD;
        const float* x = dec.tail (total);

        // Level gate on the newest window.
        {
            const float* nw = x + tauMaxD;
            float e = 0.0f;
            for (int j = 0; j < winD; ++j)
                e += nw[j] * nw[j];
            res.rms = std::sqrt (e / (float) winD);
            if (res.rms < 0.0015f) // about -56 dBFS
                return res;
        }

        // Difference function, always comparing against the newest `winD` samples.
        for (int tau = 1; tau <= tmax; ++tau)
        {
            const float* a = x + (tauMaxD - tau);
            const float* b = x + tauMaxD;
            float s = 0.0f;
            for (int j = 0; j < winD; ++j)
            {
                const float diff = a[j] - b[j];
                s += diff * diff;
            }
            d[(size_t) tau] = s;
        }

        cm[0] = 1.0f;
        float run = 0.0f;
        for (int tau = 1; tau <= tmax; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 1e-12f ? d[(size_t) tau] * (float) tau / run : 1.0f;
        }

        // Collect dips below threshold.
        const float thr = wasVoiced ? 0.28f : 0.20f;
        struct Cand { int tau; float v; };
        Cand cands[6];
        int nc = 0;

        int tau = tmin;
        while (tau < tmax && nc < 6)
        {
            if (cm[(size_t) tau] < thr)
            {
                while (tau + 1 <= tmax && cm[(size_t) tau + 1] < cm[(size_t) tau])
                    ++tau;
                cands[nc++] = { tau, cm[(size_t) tau] };
                while (tau <= tmax && cm[(size_t) tau] < thr)
                    ++tau;
            }
            else
                ++tau;
        }

        if (nc == 0)
            return res;

        Cand chosen = cands[0];

        // Octave guard: a strong 2nd/3rd harmonic (e.g. sitting on a formant) can make the
        // half or third period look periodic. If a whole multiple of the first dip is
        // clearly deeper, the true period is that multiple.
        if (chosen.v > 0.035f)
        {
            for (int i = 1; i < nc; ++i)
            {
                const float ratio = (float) cands[i].tau / (float) cands[0].tau;
                const float k = std::round (ratio);
                if (k >= 2.0f && k <= 4.0f && std::fabs (ratio - k) < 0.06f * k && cands[i].v < 0.5f * chosen.v)
                    chosen = cands[i];
            }
        }

        if (prevPeriod > 0.0f)
        {
            float bestDist = 1e9f;
            Cand closest = cands[0];
            for (int i = 0; i < nc; ++i)
            {
                const float dist = std::fabs (std::log2 ((float) (cands[i].tau * decim) / prevPeriod));
                if (dist < bestDist)
                {
                    bestDist = dist;
                    closest = cands[i];
                }
            }
            if (bestDist < 0.06f && closest.v <= chosen.v + 0.06f)
                chosen = closest;
        }

        // Fractional position at the decimated rate.
        float tauD = (float) chosen.tau;
        if (chosen.tau > 1 && chosen.tau < tmax)
        {
            const float c0 = cm[(size_t) chosen.tau - 1], c1 = cm[(size_t) chosen.tau], c2 = cm[(size_t) chosen.tau + 1];
            const float den = c0 - 2.0f * c1 + c2;
            if (std::fabs (den) > 1e-9f)
                tauD += std::clamp (0.5f * (c0 - c2) / den, -1.0f, 1.0f);
        }

        // Refine at the full rate around the coarse estimate.
        const int totalFull = total * decim;
        const float* fx = full.tail (totalFull);
        const int Wf = winD * decim;
        const int maxLagF = tauMaxD * decim;
        const float coarse = tauD * (float) decim;
        const int lo = std::max (2, (int) std::floor (coarse) - decim - 1);
        const int hi = std::min (maxLagF, (int) std::ceil (coarse) + decim + 1);

        float bestD = 1e30f;
        int bestLag = lo;
        float dv[64];
        const int span = std::min (hi - lo + 1, 64);
        for (int i = 0; i < span; ++i)
        {
            const int lag = lo + i;
            const float* a = fx + (maxLagF - lag);
            const float* b = fx + maxLagF;
            float s = 0.0f;
            for (int j = 0; j < Wf; ++j)
            {
                const float diff = a[j] - b[j];
                s += diff * diff;
            }
            dv[i] = s;
            if (s < bestD)
            {
                bestD = s;
                bestLag = lag;
            }
        }

        float period = (float) bestLag;
        const int bi = bestLag - lo;
        if (bi > 0 && bi < span - 1)
        {
            const float c0 = dv[bi - 1], c1 = dv[bi], c2 = dv[bi + 1];
            const float den = c0 - 2.0f * c1 + c2;
            if (std::fabs (den) > 1e-12f)
                period += std::clamp (0.5f * (c0 - c2) / den, -1.0f, 1.0f);
        }

        res.voiced = true;
        res.period = period;
        res.confidence = std::clamp (1.0f - chosen.v, 0.0f, 1.0f);
        return res;
    }

private:
    double fs = 48000.0, fsDec = 24000.0;
    int decim = 2, tauMaxD = 0, winD = 0;
    MirrorRing dec, full;
    float accum = 0.0f;
    int accumCount = 0;
    std::vector<float> d, cm;
};
} // namespace harmonia
