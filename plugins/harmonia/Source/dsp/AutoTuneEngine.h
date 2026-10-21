#pragma once

#include "PitchCorrector.h"
#include "PitchDetector.h"
#include "Scales.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace harmonia
{
struct EngineParams
{
    CorrectorParams corrector;
    float formantSemitones = 0.0f; // 0 keeps the singer's own vocal character
    float mix = 1.0f;              // 0 = dry (delayed), 1 = fully tuned
    float minHz = 75.0f;
    float maxHz = 900.0f;
};

// Real-time pitch corrector for monophonic voice.
//
//   detect (YIN)  ->  decide target note + smooth correction  ->  PSOLA re-synthesis
//
// PSOLA cuts the voice into pitch-synchronous grains and re-spaces them. Each grain keeps
// its original spectral envelope, so the formants (the "voice colour") stay put while the
// pitch moves, which is what keeps the result natural.
//
// The engine runs a fixed look-ahead (latencySamples()) so the pitch track is always
// known slightly before the audio is re-synthesised.
class AutoTuneEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kDesignMinHz = 75.0f;

    struct HopInfo
    {
        float inMidi = 0.0f;
        float outMidi = 0.0f;
        float targetMidi = 0.0f;
        float voiced = 0.0f;
    };
    static constexpr int kHistory = 1024;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = std::clamp (numChannels, 1, kMaxChannels);
        hop = std::max (32, (int) std::lround (fs / 250.0));
        latency = (int) std::ceil (0.046 * fs);
        unvoicedPeriod = std::max (32, (int) std::lround (0.006 * fs));

        detector.prepare (fs, kDesignMinHz);
        corrector.prepare ((double) hop / fs);

        int hs = 1;
        while (hs < (int) (0.5 * fs))
            hs <<= 1;
        histMask = hs - 1;
        for (auto& h : hist)
            h.assign ((size_t) hs, 0.0f);
        monoHist.assign ((size_t) hs, 0.0f);

        int os = 1;
        while (os < latency * 3 + 4 * detector.maxLagSamples())
            os <<= 1;
        outMask = os - 1;
        for (auto& a : acc)
            a.assign ((size_t) os, 0.0f);
        wsum.assign ((size_t) os, 0.0f);

        reset();
    }

    void reset()
    {
        for (auto& h : hist)
            std::fill (h.begin(), h.end(), 0.0f);
        std::fill (monoHist.begin(), monoHist.end(), 0.0f);
        for (auto& a : acc)
            std::fill (a.begin(), a.end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        detector.reset();
        corrector.reset();

        sampleCount = 0;
        emitted = 0;
        hopCounter = 0;
        trackCount = trackHead = 0;
        markCount = markHead = 0;
        synthStarted = false;
        ts = 0.0;
        voicedRun = unvoicedRun = 0;
        inVoiced = false;
        lastPeriod = 0.0f;
        mixSmoothed = params.mix;
        starved = 0;
        histWrite.store (0);

        runHop (0); // pre-roll: finalises the first hop of output
    }

    int latencySamples() const { return latency; }
    int hopSamples() const { return hop; }

    void setParams (const EngineParams& p) { params = p; }

    // Key learning: collect the sung pitch classes, then ask for the best matching key.
    void startLearning()
    {
        chroma.fill (0.0f);
        learning = true;
    }
    KeyGuess stopLearning()
    {
        learning = false;
        return guessKey (chroma.data());
    }
    bool isLearning() const { return learning; }
    float learnedAmount() const
    {
        float s = 0.0f;
        for (float c : chroma)
            s += c;
        return s;
    }

    // Diagnostics (read from the UI thread).
    int starvedEvents() const { return starved; }
    int historyWriteIndex() const { return histWrite.load (std::memory_order_acquire); }
    HopInfo historyAt (int absoluteIndex) const { return history[(size_t) (absoluteIndex & (kHistory - 1))]; }
    double hopRateHz() const { return fs / (double) hop; }

    void process (float* const* io, int numChannels, int numSamples)
    {
        const int nch = std::min (numChannels, channels);
        const float mixTarget = params.mix;
        const float mixCoef = 1.0f - std::exp (-1.0f / (0.01f * (float) fs));

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t t = sampleCount;
            float mono = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                const float v = io[c][i];
                hist[(size_t) c][(size_t) (t & histMask)] = v;
                mono += v;
            }
            mono /= (float) nch;
            monoHist[(size_t) (t & histMask)] = mono;
            detector.push (mono);
            ++sampleCount;

            if (++hopCounter == hop)
            {
                hopCounter = 0;
                runHop (sampleCount);
            }

            mixSmoothed += mixCoef * (mixTarget - mixSmoothed);

            const size_t slot = (size_t) (emitted & outMask);
            const float w = wsum[slot];
            const float norm = 1.0f / std::max (w, 0.3f);
            const float dryAmt = std::max (0.0f, 1.0f - w / 0.3f);
            const int64_t dryIdx = t - latency;

            for (int c = 0; c < nch; ++c)
            {
                const float dry = dryIdx >= 0 ? hist[(size_t) c][(size_t) (dryIdx & histMask)] : 0.0f;
                const float wet = acc[(size_t) c][slot] * norm + dry * dryAmt;
                io[c][i] = dry + (wet - dry) * mixSmoothed;
                acc[(size_t) c][slot] = 0.0f;
            }
            wsum[slot] = 0.0f;
            ++emitted;
        }
    }

private:
    struct TrackEntry
    {
        double t = 0.0;
        float period = 0.0f;
        float cents = 0.0f;
        bool voiced = false;
    };
    struct TrackSample
    {
        float period = 0.0f;
        float cents = 0.0f;
        bool voiced = false;
    };
    struct Mark
    {
        double pos = 0.0; // sub-sample accurate
        float period = 0.0f;
        bool voiced = false;
    };

    static constexpr int kTrackCap = 512;
    static constexpr int kMarkCap = 4096;

    // ---- track ring helpers (index 0 = oldest) ----
    const TrackEntry& trackAt (int i) const { return track[(size_t) ((trackHead - trackCount + i + kTrackCap) % kTrackCap)]; }
    const Mark& markAt (int i) const { return marks[(size_t) ((markHead - markCount + i + kMarkCap) % kMarkCap)]; }

    void pushTrack (const TrackEntry& e)
    {
        track[(size_t) trackHead] = e;
        trackHead = (trackHead + 1) % kTrackCap;
        trackCount = std::min (trackCount + 1, kTrackCap);
    }
    void pushMark (const Mark& m)
    {
        marks[(size_t) markHead] = m;
        markHead = (markHead + 1) % kMarkCap;
        markCount = std::min (markCount + 1, kMarkCap);
    }

    TrackSample queryTrack (double t) const
    {
        TrackSample out;
        if (trackCount == 0)
            return out;

        int i = trackCount - 1;
        while (i > 0 && trackAt (i).t > t)
            --i;

        const TrackEntry& a = trackAt (i);
        if (i == trackCount - 1 || a.t >= t)
            return { a.voiced ? a.period : 0.0f, a.cents, a.voiced };

        const TrackEntry& b = trackAt (i + 1);
        const float f = (float) ((t - a.t) / std::max (1e-9, b.t - a.t));
        if (a.voiced && b.voiced)
            return { a.period + f * (b.period - a.period), a.cents + f * (b.cents - a.cents), true };

        const TrackEntry& n = f < 0.5f ? a : b;
        return { n.voiced ? n.period : 0.0f, n.cents, n.voiced };
    }

    float mono (int64_t pos) const { return monoHist[(size_t) (pos & histMask)]; }

    // ---- per-hop processing ----
    void runHop (int64_t tIn)
    {
        const float prev = inVoiced ? lastPeriod : 0.0f;
        const auto det = detector.detect (params.minHz, params.maxHz, prev, inVoiced);

        if (det.voiced)
        {
            ++voicedRun;
            unvoicedRun = 0;
            lastPeriod = det.period;
        }
        else
        {
            ++unvoicedRun;
            voicedRun = 0;
        }
        if (! inVoiced && voicedRun >= 2)
            inVoiced = true;
        else if (inVoiced && unvoicedRun > 3)
            inVoiced = false;

        const float period = inVoiced ? (det.voiced ? det.period : lastPeriod) : 0.0f;
        const float f0 = period > 0.0f ? (float) fs / period : 0.0f;

        const auto out = corrector.process (inVoiced, f0, params.corrector);

        // The estimate describes the middle of the analysed span: (window + period) / 2 ago.
        const double centerDelay = 0.5 * ((double) detector.windowSamples() + (period > 0.0f ? (double) period : fs / 200.0));

        TrackEntry e;
        e.t = std::max ((double) tIn - centerDelay, trackCount > 0 ? trackAt (trackCount - 1).t + 1.0 : -1e9);
        e.period = period;
        e.cents = out.active ? std::clamp (out.correctionCents, -600.0f, 600.0f) : 0.0f;
        e.voiced = inVoiced && out.active;
        pushTrack (e);

        // UI history + key learning
        {
            const int w = histWrite.load (std::memory_order_relaxed);
            HopInfo& h = history[(size_t) (w & (kHistory - 1))];
            h.voiced = e.voiced ? 1.0f : 0.0f;
            h.inMidi = out.inMidi;
            h.outMidi = out.outMidi;
            h.targetMidi = out.targetMidi;
            histWrite.store (w + 1, std::memory_order_release);

            if (learning && e.voiced)
            {
                int pc = ((int) std::lround (out.inMidi)) % 12;
                if (pc < 0)
                    pc += 12;
                chroma[(size_t) pc] += std::max (0.0f, det.confidence - 0.5f) * 2.0f;
            }
        }

        generateMarks (tIn);
        synthesize (tIn);
    }

    // ---- analysis marks ----
    double alignByPeak (int64_t pred, float T) const
    {
        const int half = std::max (2, (int) (T * 0.5f));
        float best = -1.0f;
        int64_t bestPos = pred;
        for (int64_t p = pred - half; p <= pred + half; ++p)
        {
            const float s = 0.25f * mono (p - 1) + 0.5f * mono (p) + 0.25f * mono (p + 1);
            const float v = s * s;
            if (v > best)
            {
                best = v;
                bestPos = p;
            }
        }
        return (double) bestPos;
    }

    // Finds where the waveform around the previous mark reappears near `pred`, to a
    // fraction of a sample (parabolic peak of the normalised correlation).
    double alignByCorrelation (double lastPos, int64_t pred, float T) const
    {
        const int half = std::max (2, (int) (T * 0.5f));
        const int search = std::max (2, (int) (T * 0.2f));
        const int64_t base = (int64_t) std::llround (lastPos);

        float scores[512];
        const int count = std::min (2 * search + 1, 512);
        int bestIdx = 0;
        for (int i = 0; i < count; ++i)
        {
            const int64_t c = pred - search + i;
            float dot = 0.0f, e = 1e-9f;
            for (int k = -half; k < half; ++k)
            {
                const float a = mono (base + k);
                const float b = mono (c + k);
                dot += a * b;
                e += b * b;
            }
            scores[i] = dot / std::sqrt (e);
            if (scores[i] > scores[bestIdx])
                bestIdx = i;
        }

        double frac = 0.0;
        if (bestIdx > 0 && bestIdx < count - 1)
        {
            const float c0 = scores[bestIdx - 1], c1 = scores[bestIdx], c2 = scores[bestIdx + 1];
            const float den = c0 - 2.0f * c1 + c2;
            if (den < -1e-12f)
                frac = std::clamp (0.5f * (c0 - c2) / den, -1.0f, 1.0f);
        }
        const double cStar = (double) (pred - search + bestIdx) + frac;
        return lastPos + (cStar - (double) base);
    }

    void generateMarks (int64_t tIn)
    {
        if (trackCount == 0)
            return;
        const double limit = trackAt (trackCount - 1).t;

        if (markCount == 0)
        {
            const double p = std::floor (trackAt (trackCount - 1).t);
            if (p < 0.0)
                return;
            pushMark ({ p, (float) unvoicedPeriod, false });
        }

        for (int guard = 0; guard < 64; ++guard)
        {
            const Mark last = markAt (markCount - 1);
            const TrackSample atLast = queryTrack (last.pos);
            const float stepP = atLast.voiced ? atLast.period : (float) unvoicedPeriod;
            const int64_t pred = (int64_t) std::llround (last.pos + (double) stepP);

            if ((double) pred > limit)
                break;

            const TrackSample at = queryTrack ((double) pred);
            if (at.voiced)
            {
                const float T = at.period;
                if (pred + (int64_t) (0.75f * T) + 3 > tIn)
                    break;

                double pos = last.voiced ? alignByCorrelation (last.pos, pred, T) : alignByPeak (pred, T);
                pos = std::max (pos, last.pos + std::max (2.0, 0.5 * (double) T));
                pushMark ({ pos, T, true });
            }
            else
            {
                pushMark ({ (double) pred, (float) unvoicedPeriod, false });
            }
        }
    }

    // ---- synthesis ----
    float readCubic (const std::vector<float>& h, double pos) const
    {
        const int64_t i = (int64_t) std::floor (pos);
        if (i < 1 || i + 2 >= sampleCount || sampleCount - i > (int64_t) histMask - 4)
            return 0.0f;
        const float f = (float) (pos - (double) i);
        const float y0 = h[(size_t) ((i - 1) & histMask)], y1 = h[(size_t) (i & histMask)];
        const float y2 = h[(size_t) ((i + 1) & histMask)], y3 = h[(size_t) ((i + 2) & histMask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void placeGrain (double markPos, double center, double halfLen, double formantRatio)
    {
        const int64_t n0 = (int64_t) std::floor (center);
        const double frac = center - (double) n0;
        const int J = (int) std::ceil (halfLen) + 1;
        const double invH = 3.14159265358979323846 / halfLen;

        for (int j = -J; j <= J; ++j)
        {
            const int64_t n = n0 + j;
            if (n < emitted)
                continue;
            const double u = (double) j - frac;
            if (std::fabs (u) >= halfLen)
                continue;

            const float w = 0.5f * (1.0f + (float) std::cos (u * invH));
            const double srcPos = markPos + u * formantRatio;
            const size_t slot = (size_t) (n & outMask);
            for (int c = 0; c < channels; ++c)
                acc[(size_t) c][slot] += w * readCubic (hist[(size_t) c], srcPos);
            wsum[slot] += w;
        }
    }

    void synthesize (int64_t tIn)
    {
        if (markCount == 0)
            return;

        const int64_t finalizeUpTo = tIn + hop; // outputs before this must be complete
        const double formantRatio = std::pow (2.0, (double) params.formantSemitones / 12.0);

        if (! synthStarted)
        {
            ts = markAt (0).pos + (double) latency;
            synthStarted = true;
        }

        for (int guard = 0; guard < 256; ++guard)
        {
            const double src = ts - (double) latency;
            const Mark& newest = markAt (markCount - 1);
            if (newest.pos <= src)
            {
                if (ts - 0.5 * unvoicedPeriod < (double) finalizeUpTo)
                    ++starved;
                break;
            }

            // nearest analysis mark to the source time
            int j = markCount - 1;
            while (j > 0 && markAt (j).pos > src)
                --j;
            const Mark& m0 = markAt (j);
            const Mark& m1 = markAt (std::min (j + 1, markCount - 1));
            const Mark& mk = (src - m0.pos <= m1.pos - src) ? m0 : m1;

            double Ts, Hw;
            if (mk.voiced)
            {
                const TrackSample q = queryTrack (src);
                const float T = q.voiced ? q.period : mk.period;
                const float cents = q.voiced ? q.cents : 0.0f;
                const double r = std::pow (2.0, (double) cents / 1200.0);
                Ts = (double) T / r;
                Hw = (double) mk.period;
            }
            else
            {
                Ts = Hw = (double) mk.period;
            }

            if (ts - Hw >= (double) finalizeUpTo)
                break;

            placeGrain (mk.pos, ts, Hw, mk.voiced ? formantRatio : 1.0);
            ts += Ts;
        }
    }

    // ---- state ----
    double fs = 48000.0;
    int channels = 2, hop = 192, latency = 2208, unvoicedPeriod = 288;
    EngineParams params;

    PitchDetector detector;
    PitchCorrector corrector;

    std::array<std::vector<float>, kMaxChannels> hist, acc;
    std::vector<float> monoHist, wsum;
    int64_t histMask = 0, outMask = 0;

    int64_t sampleCount = 0, emitted = 0;
    int hopCounter = 0;

    std::array<TrackEntry, kTrackCap> track {};
    int trackCount = 0, trackHead = 0;
    std::array<Mark, kMarkCap> marks {};
    int markCount = 0, markHead = 0;

    bool synthStarted = false;
    double ts = 0.0;

    int voicedRun = 0, unvoicedRun = 0;
    bool inVoiced = false;
    float lastPeriod = 0.0f;
    float mixSmoothed = 1.0f;
    int starved = 0;

    bool learning = false;
    std::array<float, 12> chroma {};

    std::array<HopInfo, kHistory> history {};
    std::atomic<int> histWrite { 0 };
};
} // namespace harmonia
