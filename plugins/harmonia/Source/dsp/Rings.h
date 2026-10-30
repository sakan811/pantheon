// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <algorithm>
#include <array>
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

    // Pointer to the oldest of the last `len` samples (1 <= len <= capacity). The newest sample
    // is tail(len)[len - 1]. Samples from before the start of the stream read as zero.
    const float* tail (int len) const { return buf.data() + head + n - len; }

    int capacity() const { return n; }

private:
    std::vector<float> buf;
    int n = 0, head = 0;
};

// Fixed-size history of records; the oldest are dropped. Index 0 is the oldest record.
template <typename T, int N>
class HistoryRing
{
    static_assert ((N & (N - 1)) == 0, "capacity must be a power of two");

public:
    void clear() { head = count = 0; }

    void push (const T& v)
    {
        items[(size_t) head] = v;
        head = (head + 1) & (N - 1);
        count = std::min (count + 1, N);
    }

    int size() const { return count; }
    bool empty() const { return count == 0; }
    const T& operator[] (int i) const { return items[(size_t) ((head - count + i) & (N - 1))]; }
    const T& back() const { return (*this)[count - 1]; }

private:
    std::array<T, N> items {};
    int head = 0, count = 0;
};
} // namespace harmonia
