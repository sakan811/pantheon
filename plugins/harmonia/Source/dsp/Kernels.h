// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

namespace harmonia::kernels
{
// Float reductions are not vectorised by the compiler (it may not reorder additions), so each
// kernel keeps kLanes independent partial sums. That maps straight onto SIMD registers and is
// what makes the pitch search cheap.
inline constexpr int kLanes = 8;

template <typename Term>
inline float reduce (int n, Term term)
{
    float acc[kLanes] = {};
    int i = 0;
    for (; i + kLanes <= n; i += kLanes)
        for (int l = 0; l < kLanes; ++l)
            acc[l] += term (i + l);

    float tail = 0.0f;
    for (; i < n; ++i)
        tail += term (i);

    return ((acc[0] + acc[4]) + (acc[1] + acc[5])) + ((acc[2] + acc[6]) + (acc[3] + acc[7])) + tail;
}

inline float dot (const float* a, const float* b, int n)
{
    return reduce (n, [=] (int i) { return a[i] * b[i]; });
}

inline float sumSquares (const float* a, int n)
{
    return dot (a, a, n);
}

inline float sumSquaredDiff (const float* a, const float* b, int n)
{
    return reduce (n, [=] (int i) {
        const float d = a[i] - b[i];
        return d * d;
    });
}
} // namespace harmonia::kernels
