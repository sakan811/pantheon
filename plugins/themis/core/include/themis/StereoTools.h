// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "Biquad.h"
#include "Common.h"

namespace themis
{
// Mid/side width control with an optional "mono below X Hz" bass-focus filter on the side channel.
class StereoTools
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lastFreq = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& f : hp)
            f.reset();
    }

    // widthPct: 0 = mono, 100 = unchanged, 200 = double side level. monoBelowHz <= 0 disables it.
    void process (float* l, float* r, int n, float widthPct, float monoBelowHz)
    {
        const float w = widthPct * 0.01f;
        const bool useHp = monoBelowHz > 0.5f;
        if (useHp && std::abs (monoBelowHz - lastFreq) > 1.0e-6f)
        {
            const auto c = BiquadCoeffs::highPass (fs, monoBelowHz, 0.7071);
            hp[0].setCoeffs (c);
            hp[1].setCoeffs (c);
            lastFreq = monoBelowHz;
        }
        if (!useHp && std::abs (w - 1.0f) < 1.0e-6f)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float m = midOf (l[i], r[i]);
            float s = sideOf (l[i], r[i]);
            if (useHp)
                s = hp[1].process (hp[0].process (s)); // 4th-order: bass is truly collapsed
            s *= w;
            l[i] = m + s;
            r[i] = m - s;
        }
    }

private:
    double fs = 48000.0;
    float lastFreq = -1.0f;
    Biquad hp[2];
};
} // namespace themis
