#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace harmonia
{
struct CorrectorParams
{
    uint16_t noteMask = 0x0FFF; // allowed pitch classes (bit 0 = C)
    float retuneMs = 40.0f;     // time constant for gliding to the target note (0 = instant)
    float strength = 1.0f;      // 0..1, how much of the correction is applied
    float natural = 0.7f;       // 0..1, how much of the singer's own vibrato/drift is kept
    float refA4 = 440.0f;
};

// Works in cents-from-C-zero ("midi * 100") internally. Called once per analysis hop.
class PitchCorrector
{
public:
    struct Output
    {
        bool active = false;
        float inMidi = 0.0f;     // sung pitch
        float targetMidi = 0.0f; // note being aimed at
        float outMidi = 0.0f;    // corrected pitch
        float correctionCents = 0.0f;
    };

    void prepare (double hopSeconds)
    {
        dt = (float) hopSeconds;
        reset();
    }

    void reset()
    {
        active = false;
        cs = 0.0f;
        unvoicedTime = 0.0f;
        med[0] = med[1] = med[2] = 0.0f;
        medCount = 0;
        pendingCount = 0;
    }

    Output process (bool voiced, float f0, const CorrectorParams& p)
    {
        Output out;

        if (! voiced || f0 <= 0.0f)
        {
            unvoicedTime += dt;
            // Let the correction relax during gaps so a new phrase starts clean.
            cs *= std::exp (-dt / 0.06f);
            medCount = 0;
            if (unvoicedTime > 0.06f)
                active = false;
            out.correctionCents = cs;
            return out;
        }

        unvoicedTime = 0.0f;

        const float midi = 69.0f + 12.0f * std::log2 (f0 / p.refA4);
        float x = midi * 100.0f;

        // Median of three kills single-hop octave blips.
        med[0] = med[1];
        med[1] = med[2];
        med[2] = x;
        if (medCount < 3)
            ++medCount;
        if (medCount == 3)
            x = std::max (std::min (med[0], med[1]), std::min (std::max (med[0], med[1]), med[2]));

        // Ignore a big sudden leap until it has persisted for a few hops: at the end of a
        // note the detector often reports garbage for a moment.
        if (active && std::fabs (x - center) > 700.0f)
        {
            if (pendingCount > 0 && std::fabs (x - pendingX) < 150.0f)
                ++pendingCount;
            else
            {
                pendingCount = 1;
                pendingX = x;
            }

            if (pendingCount < 4)
                x = lastX;
            else
                active = false; // a real leap: treat it as a new note
        }
        else
            pendingCount = 0;
        lastX = x;

        if (! active)
        {
            active = true;
            pendingCount = 0;
            center = x;
            target = nearestAllowed (x / 100.0f, p.noteMask);
        }
        else
        {
            // "Centre" of the sung note: a slow average (vibrato averages out). It speeds
            // up when the voice moves further than a vibrato would, so that real note
            // changes are recognised quickly.
            const float away = std::clamp ((std::fabs (x - center) - 20.0f) / 30.0f, 0.0f, 1.0f);
            const float tau = 0.07f + away * (0.02f - 0.07f);
            center += (1.0f - std::exp (-dt / tau)) * (x - center);
            const float band = 45.0f;
            center = std::clamp (center, x - band, x + band);
        }

        updateTarget (center / 100.0f, p.noteMask);

        const float dev = x - center;
        float ct = (target * 100.0f - center) - (1.0f - p.natural) * dev;
        ct *= p.strength;
        ct = std::clamp (ct, -400.0f, 400.0f);

        const float alpha = p.retuneMs <= 0.5f ? 1.0f : 1.0f - std::exp (-dt * 1000.0f / p.retuneMs);
        cs += alpha * (ct - cs);

        out.active = true;
        out.inMidi = x / 100.0f;
        out.targetMidi = target;
        out.outMidi = (x + cs) / 100.0f;
        out.correctionCents = cs;
        return out;
    }

    float currentCorrectionCents() const { return cs; }

private:
    static bool allowed (int note, uint16_t mask)
    {
        return (mask & (1u << (((note % 12) + 12) % 12))) != 0;
    }

    static float nearestAllowed (float m, uint16_t mask)
    {
        if ((mask & 0x0FFF) == 0)
            return m;
        const int base = (int) std::lround (m);
        float best = (float) base;
        float bestDist = 1e9f;
        for (int n = base - 7; n <= base + 7; ++n)
        {
            if (! allowed (n, mask))
                continue;
            const float dist = std::fabs ((float) n - m);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = (float) n;
            }
        }
        return best;
    }

    // Move to a new note only once the pitch is clearly past the halfway point.
    void updateTarget (float c, uint16_t mask)
    {
        if ((mask & 0x0FFF) == 0)
        {
            target = c;
            return;
        }
        if (! allowed ((int) std::lround (target), mask))
        {
            target = nearestAllowed (c, mask);
            return;
        }

        const float hysteresis = 0.10f;
        for (int iter = 0; iter < 6; ++iter)
        {
            int hi = (int) std::lround (target) + 1;
            while (! allowed (hi, mask))
                ++hi;
            int lo = (int) std::lround (target) - 1;
            while (! allowed (lo, mask))
                --lo;

            if (c > 0.5f * (target + (float) hi) + hysteresis)
                target = (float) hi;
            else if (c < 0.5f * (target + (float) lo) - hysteresis)
                target = (float) lo;
            else
                break;
        }
    }

    float dt = 0.004f;
    bool active = false;
    float center = 0.0f, target = 0.0f, cs = 0.0f, unvoicedTime = 0.0f;
    float med[3] = { 0, 0, 0 };
    int medCount = 0;
    float lastX = 0.0f, pendingX = 0.0f;
    int pendingCount = 0;
};
} // namespace harmonia
