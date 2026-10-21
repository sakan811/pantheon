// Offline accuracy tests for the Harmonia DSP core (no JUCE needed).
//   clang++ -std=c++17 -O2 Tests/dsp_test.cpp -o build/dsp_test && build/dsp_test

#include "TestSignals.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace testsig;
using harmonia::AutoTuneEngine;
using harmonia::EngineParams;

static int failures = 0;
static void check (bool ok, const char* what, double value, const char* unit = "")
{
    std::printf ("  [%s] %-58s %8.3f %s\n", ok ? "PASS" : "FAIL", what, value, unit);
    if (! ok)
        ++failures;
}

static EngineParams makeParams (uint16_t mask, float retuneMs, float natural, float strength = 1.0f)
{
    EngineParams p;
    p.corrector.noteMask = mask;
    p.corrector.retuneMs = retuneMs;
    p.corrector.natural = natural;
    p.corrector.strength = strength;
    return p;
}

// cents error of the measured pitch against `target` in [t0, t1], shifted by engine latency
static std::vector<double> centsVs (const std::vector<PitchSample>& ps, double t0, double t1, double targetMidi)
{
    std::vector<double> v;
    for (auto& p : ps)
        if (p.t >= t0 && p.t <= t1)
            v.push_back ((p.midi - targetMidi) * 100.0);
    return v;
}

int main()
{
    const double fs = 48000.0;
    const uint16_t chromatic = 0x0FFF;

    // ------------------------------------------------------------------ 1. steady note
    std::printf ("1. Steady note, 35 cents sharp of A3 (chromatic)\n");
    {
        const double sung = 57.0 + 0.35;
        auto in = makeVoice (fs, 3.0, [&] (double) { return midiToHz (sung); });

        for (int mode = 0; mode < 2; ++mode)
        {
            AutoTuneEngine eng;
            eng.prepare (fs, 1);
            eng.setParams (mode == 0 ? makeParams (chromatic, 0.0f, 0.0f) : makeParams (chromatic, 40.0f, 0.7f));
            auto out = runEngine (eng, in);
            auto ps = measurePitch (out, fs);
            auto s = stats (centsVs (ps, 1.0 + eng.latencySamples() / fs, 2.8, 57.0));
            std::printf ("  mode: %s   (latency %d samples = %.1f ms, starved %d)\n", mode == 0 ? "hard (speed 0, natural 0)" : "soft (speed 40ms, natural 70%)",
                         eng.latencySamples(), 1000.0 * eng.latencySamples() / fs, eng.starvedEvents());
            check (std::fabs (s.mean) < 2.0, "mean error vs A3", s.mean, "cents");
            check (s.stddev < 2.0, "pitch wobble (stddev)", s.stddev, "cents");
            check (eng.starvedEvents() == 0, "no starvation", eng.starvedEvents());
        }
    }

    // ------------------------------------------------------------------ 2. vibrato
    std::printf ("2. Vibrato +-40 cents @5.5 Hz around a note 30 cents flat of C4\n");
    {
        auto f0 = [] (double t) {
            const double m = 60.0 - 0.30 + 0.40 * std::sin (2.0 * kPi * 5.5 * t);
            return midiToHz (m);
        };
        auto in = makeVoice (fs, 4.0, f0);
        auto inPs = measurePitch (in, fs);
        auto inStats = stats (centsVs (inPs, 1.0, 3.5, 60.0));

        struct Case { const char* name; float speed, natural; double minKeep, maxKeep; };
        const Case cases[] = {
            { "natural 100%, speed 40ms", 40.0f, 1.0f, 0.70, 1.3 },
            { "natural 0%, speed 0 (hard)", 0.0f, 0.0f, 0.0, 0.15 },
        };
        for (auto& c : cases)
        {
            AutoTuneEngine eng;
            eng.prepare (fs, 1);
            eng.setParams (makeParams (chromatic, c.speed, c.natural));
            auto out = runEngine (eng, in);
            auto ps = measurePitch (out, fs);
            auto s = stats (centsVs (ps, 1.0 + eng.latencySamples() / fs, 3.5, 60.0));
            const double keep = s.stddev / inStats.stddev;
            std::printf ("  case: %s\n", c.name);
            check (std::fabs (s.mean) < 5.0, "mean error vs C4", s.mean, "cents");
            check (keep >= c.minKeep && keep <= c.maxKeep, "vibrato depth kept (out/in)", keep);
        }
    }

    // ------------------------------------------------------------------ 3. melody with gaps
    std::printf ("3. Melody A3 +30c, C4 -25c, E4 +20c, G3 +40c with breath gaps (C major)\n");
    {
        const double notes[] = { 57.30, 59.75, 63.20, 55.40 }; // sung midi
        const double targets[] = { 57.0, 60.0, 64.0, 55.0 };   // C major: A3, C4, E4, G3 (59.75 -> 60, 63.2 -> 64)
        auto f0 = [&] (double t) {
            const double seg = 0.8;
            const int k = (int) (t / seg);
            const double within = t - k * seg;
            if (k >= 4 || within > seg - 0.06)
                return 0.0;
            return midiToHz (notes[k]);
        };
        auto in = makeVoice (fs, 3.2, f0);
        AutoTuneEngine eng;
        eng.prepare (fs, 1);
        eng.setParams (makeParams (0x0AB5, 30.0f, 0.5f)); // C major bits: C D E F G A B
        auto out = runEngine (eng, in);
        auto ps = measurePitch (out, fs);
        const double lat = eng.latencySamples() / fs;
        for (int k = 0; k < 4; ++k)
        {
            auto s = stats (centsVs (ps, k * 0.8 + 0.35 + lat, k * 0.8 + 0.72, targets[k]));
            char label[64];
            std::snprintf (label, sizeof label, "note %d settled error (n=%d)", k + 1, s.n);
            check (s.n > 5 && std::fabs (s.mean) < 6.0 && s.stddev < 6.0, label, s.mean, "cents");
        }
        check (eng.starvedEvents() == 0, "no starvation", eng.starvedEvents());
    }

    // ------------------------------------------------------------------ 4. range of voices
    std::printf ("4. Different voices, 28 cents flat, hard tune\n");
    {
        const struct { const char* name; double midi; } voices[] = { { "bass E2 (82 Hz)", 40.0 }, { "baritone A2", 45.0 }, { "alto E4", 64.0 }, { "soprano C6", 84.0 } };
        for (auto& v : voices)
        {
            const double sung = v.midi - 0.28;
            auto in = makeVoice (fs, 2.5, [&] (double) { return midiToHz (sung); }, v.midi > 70 ? kEe : kAh);
            AutoTuneEngine eng;
            eng.prepare (fs, 1);
            auto vp = makeParams (chromatic, 0.0f, 0.0f);
            vp.maxHz = 1300.0f;
            eng.setParams (vp);
            auto out = runEngine (eng, in);
            auto ps = measurePitch (out, fs, 60, 1300);
            auto s = stats (centsVs (ps, 1.0 + eng.latencySamples() / fs, 2.3, v.midi));
            char label[96];
            std::snprintf (label, sizeof label, "%s: mean err (stddev %.2f)", v.name, s.stddev);
            check (s.n > 20 && std::fabs (s.mean) < 3.0 && s.stddev < 3.0, label, s.mean, "cents");
        }
    }

    // ------------------------------------------------------------------ 5. transparency
    std::printf ("5. Transparency: already in tune => output is the delayed input (PSOLA may shift timing by < 1 period)\n");
    {
        auto in = makeVoice (fs, 2.0, [] (double) { return midiToHz (57.0); });
        AutoTuneEngine eng;
        eng.prepare (fs, 1);
        eng.setParams (makeParams (chromatic, 40.0f, 0.7f));
        auto out = runEngine (eng, in);
        const int lat = eng.latencySamples();
        double best = -1.0;
        for (int lag = -300; lag <= 300; ++lag)
        {
            double num = 0, da = 0, db = 0;
            for (size_t i = (size_t) (fs * 1.0); i < (size_t) (fs * 1.8); ++i)
            {
                const double a = in[(size_t) ((long) i - lat + lag)], b = out[i];
                num += a * b;
                da += a * a;
                db += b * b;
            }
            best = std::max (best, num / std::sqrt (da * db));
        }
        check (best > 0.97, "waveform correlation with delayed dry", best);
    }

    // ------------------------------------------------------------------ 6. formants kept
    std::printf ("6. Formants stay put while pitch moves up ~ 3 semitones\n");
    {
        auto in = makeVoice (fs, 2.5, [] (double) { return midiToHz (55.0 + 0.0); }); // G3
        // force a 300 cent upward shift by only allowing B-flat... (A#3 = 58)
        AutoTuneEngine eng;
        eng.prepare (fs, 1);
        eng.setParams (makeParams ((uint16_t) (1u << 10), 0.0f, 0.0f)); // only A#
        auto out = runEngine (eng, in);
        auto spectrumCentroid = [&] (const std::vector<float>& x, size_t from, size_t len) {
            // energy-weighted centroid of the Hann-windowed power spectrum, 100 Hz .. 4 kHz
            double num = 0, den = 0;
            for (double f = 100; f < 4000; f += 2.0)
            {
                const double w = 2.0 * kPi * f / fs;
                const double cw = std::cos (w), sw = std::sin (w);
                double cr = 1.0, ci = 0.0, re = 0.0, im = 0.0;
                for (size_t i = 0; i < len; ++i)
                {
                    const double hann = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) len);
                    const double v = x[from + i] * hann;
                    re += v * cr;
                    im += v * ci;
                    const double nr = cr * cw - ci * sw;
                    ci = cr * sw + ci * cw;
                    cr = nr;
                }
                const double p = re * re + im * im;
                num += f * p;
                den += p;
            }
            return num / den;
        };
        const size_t from = (size_t) (fs * 1.5), len = (size_t) (fs * 0.5);
        const double cIn = spectrumCentroid (in, from, len);
        const double cOut = spectrumCentroid (out, from, len);
        auto ps = measurePitch (out, fs);
        auto s = stats (centsVs (ps, 1.2, 2.3, 58.0));
        check (std::fabs (s.mean) < 3.0, "output pitch = A#3", s.mean, "cents");
        // a naive resample by +3 semitones would move the centroid by +19%
        check (std::fabs (cOut / cIn - 1.0) < 0.07, "spectral centroid ratio out/in (resample would be 1.19)", cOut / cIn);
    }

    // ------------------------------------------------------------------ 7. silence / noise safety
    std::printf ("7. Silence, noise and clipping safety\n");
    {
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        std::vector<float> noise ((size_t) (fs * 2));
        for (auto& s : noise)
            s = std::clamp (nd (rng), -1.0f, 1.0f);
        AutoTuneEngine eng;
        eng.prepare (fs, 1);
        eng.setParams (makeParams (chromatic, 20.0f, 0.5f));
        auto out = runEngine (eng, noise);
        bool finite = true;
        double peak = 0;
        for (float s : out)
        {
            finite = finite && std::isfinite (s);
            peak = std::max (peak, (double) std::fabs (s));
        }
        check (finite, "output is finite on white noise", finite ? 1 : 0);
        check (peak < 2.0, "output level sane on white noise", peak);

        std::vector<float> silent ((size_t) fs, 0.0f);
        AutoTuneEngine eng2;
        eng2.prepare (fs, 1);
        eng2.setParams (makeParams (chromatic, 20.0f, 0.5f));
        auto out2 = runEngine (eng2, silent);
        double maxAbs = 0;
        for (float s : out2)
            maxAbs = std::max (maxAbs, (double) std::fabs (s));
        check (maxAbs == 0.0, "silence in -> silence out", maxAbs);
    }

    // ------------------------------------------------------------------ 7b. pitch sweep
    std::printf ("7b. Detection sweep: every semitone E2..C6, two vowels, 0.4%% jitter\n");
    {
        int bad = 0;
        for (int v = 0; v < 2; ++v)
            for (int midi = 40; midi <= 84; ++midi)
            {
                const double sung = midi - 0.28;
                auto in = makeVoice (fs, 1.5, [&] (double) { return midiToHz (sung); }, v == 0 ? kAh : kEe, 7u + (unsigned) midi, 0.004);
                auto ps = measurePitch (in, fs, 60, 1300);
                int ok = 0, n = 0;
                for (auto& p : ps)
                {
                    if (p.t < 0.4)
                        continue;
                    ++n;
                    if (std::fabs ((p.midi - sung) * 100.0) < 15.0)
                        ++ok;
                }
                if (ok < n * 0.97)
                    ++bad;
            }
        check (bad == 0, "pitches/vowels with octave or tracking errors", bad);
    }

    // ------------------------------------------------------------------ 7c. sample rates
    std::printf ("7c. Other sample rates: A3 35 cents sharp, hard tune, + vibrato note soft\n");
    for (double rate : { 44100.0, 88200.0, 96000.0 })
    {
        auto in = makeVoice (rate, 2.5, [] (double) { return midiToHz (57.35); });
        AutoTuneEngine eng;
        eng.prepare (rate, 1);
        eng.setParams (makeParams (chromatic, 0.0f, 0.0f));
        auto out = runEngine (eng, in);
        auto ps = measurePitch (out, rate);
        auto s = stats (centsVs (ps, 1.0 + eng.latencySamples() / rate, 2.3, 57.0));
        char label[96];
        std::snprintf (label, sizeof label, "%.0f Hz: mean err (stddev %.2f, latency %.1f ms, starved %d)", rate, s.stddev,
                       1000.0 * eng.latencySamples() / rate, eng.starvedEvents());
        check (s.n > 20 && std::fabs (s.mean) < 2.0 && s.stddev < 2.0 && eng.starvedEvents() == 0, label, s.mean, "cents");
    }

    // ------------------------------------------------------------------ 8. CPU
    std::printf ("8. CPU cost\n");
    {
        auto in = makeVoice (fs, 10.0, [] (double t) { return midiToHz (50.0 + 7.0 * std::sin (t)); });
        AutoTuneEngine eng;
        eng.prepare (fs, 2);
        eng.setParams (makeParams (chromatic, 30.0f, 0.5f));
        std::vector<float> l = in, r = in;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos + 512 <= l.size(); pos += 512)
        {
            float* ch[2] = { l.data() + pos, r.data() + pos };
            eng.process (ch, 2, 512);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();
        const double load = secs / 10.0 * 100.0;
        check (load < 10.0, "CPU load (stereo, 48 kHz, one core)", load, "%");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
