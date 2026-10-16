#pragma once

#include "Common.h"

#include <cstddef>
#include <vector>

namespace themis
{
// Minimal in-place radix-2 complex FFT. Not realtime-safe to construct; transform() is.
class FFT
{
public:
    explicit FFT (int sizePow2)
        : n (sizePow2), cosT (static_cast<size_t> (sizePow2 / 2)), sinT (static_cast<size_t> (sizePow2 / 2)),
          rev (static_cast<size_t> (sizePow2))
    {
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * kPi * i / n;
            cosT[static_cast<size_t> (i)] = static_cast<float> (std::cos (a));
            sinT[static_cast<size_t> (i)] = static_cast<float> (std::sin (a));
        }
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[static_cast<size_t> (i)] = r;
        }
    }

    int size() const { return n; }

    // re/im each hold n samples.
    void transform (float* re, float* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[static_cast<size_t> (i)];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[static_cast<size_t> (k * step)];
                    const float wi = sinT[static_cast<size_t> (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    int n;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};
} // namespace themis
