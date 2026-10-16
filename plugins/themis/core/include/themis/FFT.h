#pragma once

#include "Common.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace themis
{
// Minimal in-place radix-2 complex FFT (split re/im arrays). Not realtime-safe to construct; transform() is.
// Twiddles are stored per stage so every butterfly pass reads them contiguously (vectoriser-friendly).
class FFT
{
public:
    explicit FFT (int sizePow2)
        : n (sizePow2), twRe (static_cast<size_t> (sizePow2 > 1 ? sizePow2 - 1 : 0)), twIm (twRe.size()),
          rev (static_cast<size_t> (sizePow2))
    {
        // Stage with half-length h (1, 2, 4 ... n/2) owns entries [h-1, 2h-1): w_k = exp(-i*pi*k/h).
        for (int h = 1; h < n; h <<= 1)
            for (int k = 0; k < h; ++k)
            {
                const double a = -kPi * k / h;
                twRe[static_cast<size_t> (h - 1 + k)] = static_cast<float> (std::cos (a));
                twIm[static_cast<size_t> (h - 1 + k)] = static_cast<float> (std::sin (a));
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
        // First stage: w = 1, no multiplies.
        for (int i = 0; i + 1 < n; i += 2)
        {
            const float tr = re[i + 1], ti = im[i + 1];
            re[i + 1] = re[i] - tr;
            im[i + 1] = im[i] - ti;
            re[i] += tr;
            im[i] += ti;
        }
        for (int h = 2; h < n; h <<= 1)
        {
            const float* wr = twRe.data() + (h - 1);
            const float* wi = twIm.data() + (h - 1);
            for (int i = 0; i < n; i += 2 * h)
            {
                float* THEMIS_RESTRICT ar = re + i;
                float* THEMIS_RESTRICT br = re + i + h;
                float* THEMIS_RESTRICT ai = im + i;
                float* THEMIS_RESTRICT bi = im + i + h;
                for (int k = 0; k < h; ++k)
                {
                    const float tr = br[k] * wr[k] - bi[k] * wi[k];
                    const float ti = br[k] * wi[k] + bi[k] * wr[k];
                    br[k] = ar[k] - tr;
                    bi[k] = ai[k] - ti;
                    ar[k] += tr;
                    ai[k] += ti;
                }
            }
        }
    }

private:
    int n;
    std::vector<float> twRe, twIm;
    std::vector<int> rev;
};

// FFT of a real signal of length n (power of two, >= 4) using one complex FFT of length n/2.
// Produces bins 0..n/2. Holds scratch buffers, so one instance must not be used from two threads at once.
class RealFFT
{
public:
    explicit RealFFT (int sizePow2)
        : n (sizePow2), half (sizePow2 / 2), fft (sizePow2 / 2), zRe (static_cast<size_t> (sizePow2 / 2)), zIm (zRe.size()),
          wRe (zRe.size() + 1), wIm (zRe.size() + 1)
    {
        for (int k = 0; k <= half; ++k)
        {
            const double a = -2.0 * kPi * k / n;
            wRe[static_cast<size_t> (k)] = static_cast<float> (std::cos (a));
            wIm[static_cast<size_t> (k)] = static_cast<float> (std::sin (a));
        }
    }

    int size() const { return n; }

    // x: n real samples. outRe/outIm: n/2 + 1 bins each.
    void forward (const float* x, float* outRe, float* outIm)
    {
        run (x, [&] (int k, float re, float im) {
            outRe[k] = re;
            outIm[k] = im;
        });
    }

    // power[k] = scale * |X[k]|^2 for k = 0..n/2.
    void powerSpectrum (const float* x, float* power, float scale)
    {
        run (x, [&] (int k, float re, float im) { power[k] = (re * re + im * im) * scale; });
    }

private:
    template <typename Sink>
    void run (const float* x, Sink&& sink)
    {
        for (int j = 0; j < half; ++j)
        {
            zRe[static_cast<size_t> (j)] = x[2 * j];
            zIm[static_cast<size_t> (j)] = x[2 * j + 1];
        }
        fft.transform (zRe.data(), zIm.data());

        // Split the packed transform Z into the even (Fe) and odd (Fo) sample spectra and recombine.
        for (int k = 0; k <= half; ++k)
        {
            const size_t a = static_cast<size_t> (k == half ? 0 : k);
            const size_t b = static_cast<size_t> (k == 0 ? 0 : half - k);
            const float feRe = 0.5f * (zRe[a] + zRe[b]), feIm = 0.5f * (zIm[a] - zIm[b]);
            const float foRe = 0.5f * (zIm[a] + zIm[b]), foIm = -0.5f * (zRe[a] - zRe[b]);
            const float wr = wRe[static_cast<size_t> (k)], wi = wIm[static_cast<size_t> (k)];
            sink (k, feRe + wr * foRe - wi * foIm, feIm + wr * foIm + wi * foRe);
        }
    }

    int n, half;
    FFT fft;
    std::vector<float> zRe, zIm, wRe, wIm;
};
} // namespace themis
