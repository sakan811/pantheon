// Headless DSP verification for Hephaestus. Prints measured numbers (not only pass/fail) so the
// behaviour documented in the README can be checked against reality.

#include "Check.h"
#include "DSP/Engine.h"
#include "DSP/SpectrumAnalyzer.h"
#include "TestSignals.h"
#include <chrono>
#include <random>

using namespace hep;
using namespace test;

namespace
{
double db (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

// Hann-windowed single-frequency DFT; returns the amplitude of a stationary sinusoid.
double amplitudeAt (const std::vector<float>& x, int start, int len, double f)
{
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (len - 1));
        const double ph = 2.0 * M_PI * f * (start + i) / fs;
        re += w * x[(size_t) (start + i)] * std::cos (ph);
        im -= w * x[(size_t) (start + i)] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// Amplitude over the second half of a one-second signal, where every filter has settled.
double settledAmplitude (const std::vector<float>& x, double f) { return amplitudeAt (x, 24000, 16000, f); }

EngineParams cleanParams()
{
    EngineParams e;
    e.sat.drive[0] = e.sat.drive[1] = e.sat.drive[2] = 1.0;
    e.sat.bias = 0.0;
    e.osMode = 2;
    return e;
}

struct Stereo { std::vector<float> l, r; };

// An engine plus its parameters, fed in `block`-sized pieces.
struct Rig
{
    Engine eng;
    EngineParams p = cleanParams();

    explicit Rig (int os = 2) { eng.prepare (fs, 512); p.osMode = os; }

    Stereo run (const std::vector<float>& left, const std::vector<float>* rightIn = nullptr, int block = 480)
    {
        const auto& right = rightIn ? *rightIn : left;
        Stereo out { std::vector<float> (left.size()), std::vector<float> (left.size()) };
        juce::AudioBuffer<float> buf (2, block);

        for (size_t pos = 0; pos < left.size(); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, left.size() - pos);
            std::copy_n (left.begin() + (long) pos, n, buf.getWritePointer (0));
            std::copy_n (right.begin() + (long) pos, n, buf.getWritePointer (1));
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            eng.process (view, p);
            std::copy_n (buf.getReadPointer (0), n, out.l.begin() + (long) pos);
            std::copy_n (buf.getReadPointer (1), n, out.r.begin() + (long) pos);
        }
        return out;
    }

    std::vector<float> runMono (const std::vector<float>& in, int block = 480) { return run (in, nullptr, block).l; }
};

std::vector<float> noise (int n, unsigned seed, float amp = 0.3f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-amp, amp);
    std::vector<float> x ((size_t) n);
    for (auto& v : x) v = u (rng);
    return x;
}

// ------------------------------------------------------------------------------------------
void testTransparency()
{
    std::printf ("\n[1] Small-signal transparency (sat on, drive 0 dB, -60 dBFS sines; gain must be flat)\n");
    const char* names[] = { "off", "2x", "4x", "8x" };

    for (int os = 0; os < 4; ++os)
    {
        Rig rig (os);
        double worst = 0;
        std::string line;
        for (double f : { 40.0, 150.0, 1000.0, 3500.0, 8000.0, 15000.0, 20000.0 })
        {
            const auto in = sines ({ { f, 0.001 } }, 48000);
            const double g = db (settledAmplitude (rig.runMono (in), f) / settledAmplitude (in, f));
            worst = std::max (worst, std::fabs (g));
            line += fmt (" %.0fHz:%+.2f", f, g);
        }
        check (worst < 0.2, fmt ("os=%s (latency %d) worst %.3f dB |", names[os], rig.eng.getLatency(), worst) + line);
    }
}

void testLatencyAlignment()
{
    std::printf ("\n[2] Latency alignment: dry impulse must land exactly on the reported latency\n");
    const auto peakAt = [] (const std::vector<float>& v)
    {
        return (int) (std::max_element (v.begin(), v.end(), [] (float a, float b) { return std::fabs (a) < std::fabs (b); }) - v.begin());
    };

    for (int os = 0; os < 4; ++os)
    {
        std::vector<float> imp (4096, 0.0f);
        imp[100] = 0.001f;

        Rig dryRig (os), wetRig (os);
        dryRig.p.mix = 0.0f;
        wetRig.p.mix = 1.0f;
        const int dryAt = peakAt (dryRig.runMono (imp)) - 100, wetAt = peakAt (wetRig.runMono (imp)) - 100;
        const int latency = dryRig.eng.getLatency();
        check (dryAt == latency && std::abs (wetAt - latency) <= 4,
               fmt ("os=%d latency %d, dry peak at +%d, wet peak at +%d", os, latency, dryAt, wetAt));
    }
}

void testAliasing()
{
    std::printf ("\n[3] Aliasing: 5.3 kHz sine, tanh, +18 dB drive. Alias energy relative to the fundamental\n");
    const int N = 1 << 16;
    const double f = 7237.0 * fs / N; // exactly on a bin
    const auto in = sines ({ { f, 0.5 } }, N + 16384);

    const auto aliasLevel = [&] (const std::vector<float>& y)
    {
        std::vector<float> data ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i)
            data[(size_t) i] = y[(size_t) (16384 + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * i / (N - 1)));
        juce::dsp::FFT fft (16);
        fft.performFrequencyOnlyForwardTransform (data.data());

        const double binHz = fs / N;
        double pFund = 0, pAlias = 0;
        for (int k = 20; k < N / 2; ++k)
        {
            const double hz = k * binHz;
            bool harmonic = false;
            for (int h = 1; h * f < fs * 0.5 + 100; h += 2) // odd harmonics expected from a symmetric curve
                if (std::fabs (hz - h * f) < 12 * binHz) harmonic = true;
            const double pw = (double) data[(size_t) k] * data[(size_t) k];
            if (std::fabs (hz - f) < 12 * binHz) pFund += pw;
            else if (! harmonic) pAlias += pw;
        }
        return 10.0 * std::log10 (pAlias / pFund + 1.0e-30);
    };

    // Naive reference: same drive, plain tanh at the base rate, no ADAA, no oversampling.
    std::vector<float> naive (in.size());
    for (size_t i = 0; i < in.size(); ++i)
        naive[i] = (float) (std::tanh (8.0 * in[i]) / 8.0);
    const double naiveDb = aliasLevel (naive);
    std::printf ("  naive tanh, no ADAA, no oversampling : %7.1f dB\n", naiveDb);

    double levels[4];
    for (int os = 0; os < 4; ++os)
    {
        Rig rig (os);
        rig.p.sat.drive[0] = rig.p.sat.drive[1] = rig.p.sat.drive[2] = 8.0; // +18 dB
        levels[os] = aliasLevel (rig.runMono (in));
        std::printf ("  Hephaestus os=%d                       : %7.1f dB\n", os, levels[os]);
    }

    check (levels[0] < naiveDb - 6.0, "ADAA alone beats the naive shaper by > 6 dB");
    check (levels[2] < levels[0] - 10.0, "4x oversampling is > 10 dB cleaner than ADAA alone");
    check (levels[3] <= levels[2] + 1.0, "8x is no worse than 4x");
}

void testEvenOdd()
{
    std::printf ("\n[4] Even/odd control: 1 kHz sine, tanh, +12 dB drive on the mid band\n");
    const auto in = sines ({ { 1000.0, 0.3 } }, 48000);

    for (double bias : { 0.0, 0.5 })
    {
        Rig rig;
        rig.p.sat.drive[1] = 4.0;
        rig.p.sat.bias = bias;
        const auto out = rig.runMono (in);
        const double a1 = settledAmplitude (out, 1000);
        const double h2 = db (settledAmplitude (out, 2000) / a1), h3 = db (settledAmplitude (out, 3000) / a1);
        const auto detail = fmt ("bias %.1f: H2 %+.1f dB, H3 %+.1f dB (re fundamental)", bias, h2, h3);
        check (bias == 0.0 ? (h2 < -90.0 && h3 > -40.0) : h2 > -40.0, detail);
    }
}

FundamentalEngine::Params extendParams()
{
    FundamentalEngine::Params p;
    p.extGain = 1.0;
    const double weightsDb[4] = { -6.0, -9.0, -15.0, -22.0 };
    for (int i = 0; i < 4; ++i)
        p.harmonic[i] = std::pow (10.0, weightsDb[i] / 20.0);
    return p;
}

std::vector<float> runFund (const std::vector<float>& in, const FundamentalEngine::Params& p)
{
    FundamentalEngine fe;
    fe.prepare (fs);
    std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 480)
    {
        const int n = (int) std::min<size_t> (480, in.size() - pos);
        const float* inp[1] = { in.data() + pos };
        float* outp[1] = { out.data() + pos };
        fe.process (inp, outp, 1, n, p);
    }
    return out;
}

// Magnitude of the engine's 4th-order Butterworth bass-range filter at f.
double rangeGain (double f, const FundamentalEngine::Params& p)
{
    const double hi = effectiveRangeHigh (p.lo, p.hi);
    return 1.0 / std::sqrt (1.0 + std::pow (p.lo / f, 8.0)) / std::sqrt (1.0 + std::pow (f / hi, 8.0));
}

void testExtend()
{
    std::printf ("\n[5] Harmonic Bass (EXTEND): weights relative to the band-passed fundamental, across the range\n");
    const double expected[4] = { -6.0, -9.0, -15.0, -22.0 };
    const auto params = extendParams();

    for (double f0 : { 45.0, 60.0, 90.0 })
        for (double amp : { 0.5, 0.05 })
        {
            const auto in = sines ({ { f0, amp } }, 96000);
            const auto add = runFund (in, params);
            // Harmonics are defined relative to the fundamental *as the range filter passes it*; a
            // fundamental at the very edge of the range is attenuated by that filter by design.
            const double ref = amplitudeAt (in, 48000, 32000, f0) * rangeGain (f0, params);

            double worst = 0;
            std::string line;
            for (int h = 0; h < 4; ++h)
            {
                const double g = db (amplitudeAt (add, 48000, 32000, f0 * (h + 2)) / ref);
                worst = std::max (worst, std::fabs (g - expected[h]));
                line += fmt (" H%d:%+.1f", h + 2, g);
            }
            const double sub = db (amplitudeAt (add, 48000, 32000, f0 / 2) / ref);
            const double fund = db (amplitudeAt (add, 48000, 32000, f0) / ref);
            check (worst < 1.5 && sub < -45.0 && fund < -25.0,
                   fmt ("%3.0f Hz @ %.2f: max deviation %.2f dB |", f0, amp, worst) + line
                       + fmt (" | f/2 %.0f dB, f %.0f dB", sub, fund));
        }
}

void testExtendNoteOff()
{
    std::printf ("\n[5b] Harmonic Bass must not hang after a note ends (envelope release vs. tails)\n");
    auto burst = sines ({ { 60.0, 0.5 } }, 72000);
    std::fill (burst.begin() + 24000, burst.end(), 0.0f); // note ends at 0.5 s
    const auto add = runFund (burst, extendParams());

    const auto rms = [&] (int from, int to)
    {
        double s = 0;
        for (int i = from; i < to; ++i) s += (double) add[(size_t) i] * add[(size_t) i];
        return std::sqrt (s / (to - from));
    };
    const double sustain = rms (12000, 22000);
    for (double afterMs : { 50.0, 100.0 })
    {
        const int from = 24000 + (int) (afterMs * 48.0);
        const double tail = db (rms (from, from + 2400) / sustain);
        check (tail < -30.0, fmt ("harmonic output %3.0f ms after note-off: %.0f dB re sustain", afterMs, tail));
    }
}

void testRestore()
{
    std::printf ("\n[6] Fundamental Restore: 120 + 180 Hz present, 60 Hz missing\n");
    FundamentalEngine::Params p;
    p.resGain = 1.0;
    p.fill = 0.0;

    const auto harmonics = sines ({ { 120.0, 0.3 }, { 180.0, 0.3 } }, 96000);
    const double restored = amplitudeAt (runFund (harmonics, p), 48000, 32000, 60.0);
    check (restored > 0.15 && restored < 0.6,
           fmt ("restored 60 Hz amplitude %.3f (harmonics are 0.300 each; target ~0.3)", restored));

    // Half the input level -> must scale linearly (not quadratically).
    const auto quiet = sines ({ { 120.0, 0.15 }, { 180.0, 0.15 } }, 96000);
    const double restoredQuiet = amplitudeAt (runFund (quiet, p), 48000, 32000, 60.0);
    check (std::fabs (db (restoredQuiet / restored) + 6.0) < 1.5,
           fmt ("-6 dB input -> %+.1f dB restored (linear tracking = -6.0)", db (restoredQuiet / restored)));

    // With a real fundamental present and fill = 100%, the restored component must back off.
    p.fill = 1.0;
    const auto full = sines ({ { 60.0, 0.4 }, { 120.0, 0.3 }, { 180.0, 0.3 } }, 96000);
    const double withFund = amplitudeAt (runFund (full, p), 48000, 32000, 60.0);
    const double without = amplitudeAt (runFund (harmonics, p), 48000, 32000, 60.0);
    check (withFund < 0.6 * without,
           fmt ("fill=100%%: restored %.3f with fundamental vs %.3f without (ratio %.2f)", withFund, without, withFund / without));
}

void testBassStaysCentred()
{
    std::printf ("\n[7] Mid/Side keeps bass centred: a pure-side 70 Hz tone (L = -R)\n");
    const auto left = sines ({ { 70.0, 0.4 } }, 48000);
    std::vector<float> right (left.size());
    for (size_t i = 0; i < left.size(); ++i) right[i] = -left[i];

    double h2[2];
    for (int ms = 0; ms < 2; ++ms)
    {
        Rig rig;
        rig.p.midSide = ms == 1;
        rig.p.fund = extendParams();
        const auto out = rig.run (left, &right).l;
        h2[ms] = db (settledAmplitude (out, 140) / settledAmplitude (out, 70));
    }
    check (h2[0] > -12.0 && h2[1] < -80.0,
           fmt ("2nd harmonic of a side-only tone: L/R mode %+.1f dB, M/S mode %+.1f dB (re fundamental)", h2[0], h2[1]));
}

void testMixDeltaMatch()
{
    std::printf ("\n[8] Mix, Delta and Gain Match behave as documented\n");
    const auto in = noise (96000, 11);
    const auto configure = [] (Rig& rig)
    {
        rig.p.sat.drive[0] = rig.p.sat.drive[1] = rig.p.sat.drive[2] = 4.0;
        rig.p.sat.bias = 0.3;
        rig.p.fund = extendParams();
    };

    Rig wetRig, dryRig, deltaRig, halfRig;
    for (auto* r : { &wetRig, &dryRig, &deltaRig, &halfRig }) configure (*r);
    wetRig.p.mix = 1.0f;
    dryRig.p.mix = 0.0f;
    deltaRig.p.delta = true;
    halfRig.p.mix = 0.37f;

    const auto wet = wetRig.runMono (in), dry = dryRig.runMono (in), delta = deltaRig.runMono (in), half = halfRig.runMono (in);
    const int latency = dryRig.eng.getLatency();

    float dryErr = 0, deltaErr = 0, mixErr = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if ((int) i >= latency) dryErr = std::max (dryErr, std::fabs (dry[i] - in[i - (size_t) latency]));
        deltaErr = std::max (deltaErr, std::fabs (delta[i] - (wet[i] - dry[i])));
        mixErr = std::max (mixErr, std::fabs (half[i] - (dry[i] + 0.37f * (wet[i] - dry[i]))));
    }
    check (dryErr < 1.0e-6f, fmt ("mix = 0 is the input delayed by exactly the reported latency (max error %.1e)", dryErr));
    check (deltaErr < 1.0e-5f, fmt ("delta = wet - dry sample for sample (max error %.1e)", deltaErr));
    check (mixErr < 1.0e-5f, fmt ("mix = 0.37 is the linear blend (max error %.1e)", mixErr));

    // Gain match: heavy saturation lowers the level; matching must restore it.
    const auto tone = sines ({ { 1000.0, 0.5 } }, 48000 * 8);
    const auto rms = [] (const std::vector<float>& x)
    {
        double s = 0; size_t n = 0;
        for (size_t i = x.size() - 48000; i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; }
        return std::sqrt (s / (double) n);
    };
    double offDb = 0, onDb = 0;
    for (int match = 0; match < 2; ++match)
    {
        Rig rig;
        rig.p.sat.drive[0] = rig.p.sat.drive[1] = rig.p.sat.drive[2] = 4.0;
        rig.p.match = match == 1;
        (match ? onDb : offDb) = db (rms (rig.runMono (tone)) / rms (tone));
    }
    check (offDb < -3.0 && std::fabs (onDb) < 1.0,
           fmt ("gain match: level change %+.1f dB without, %+.1f dB with", offDb, onDb));
}

void testRobustness()
{
    std::printf ("\n[9] Robustness: random parameters, block sizes, mode switches, mono, mid/side\n");
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    Engine eng;
    eng.prepare (fs, 512);
    bool finite = true;
    float maxAbs = 0;

    for (int iter = 0; iter < 600; ++iter)
    {
        EngineParams p;
        p.inGainDb = -12.0f + 24.0f * u (rng);
        p.outGainDb = -12.0f + 24.0f * u (rng);
        p.mix = u (rng);
        p.midSide = u (rng) > 0.5f;
        p.delta = u (rng) > 0.8f;
        p.match = u (rng) > 0.5f;
        p.satOn = u (rng) > 0.2f;
        p.osMode = (int) (u (rng) * 3.99f);
        p.sat.curve = (int) (u (rng) * 2.99f);
        p.sat.bias = 0.8 * u (rng);
        for (auto& d : p.sat.drive) d = std::pow (10.0, 24.0 * u (rng) / 20.0);
        p.sat.xoverLow = 60.0 + 440.0 * u (rng);
        p.sat.xoverHigh = 1500.0 + 10500.0 * u (rng);
        p.fund = extendParams();
        p.fund.lo = 25.0 + 95.0 * u (rng);
        p.fund.hi = p.fund.lo * (1.5 + 3.0 * u (rng));
        p.fund.resGain = u (rng) > 0.5f ? 1.0 : 0.0;
        p.fund.extGain = u (rng) > 0.3f ? 2.0 : 0.0;

        const int block = 1 + (int) (u (rng) * 1500.0f); // includes > maxBlock and tiny sizes
        juce::AudioBuffer<float> buf (u (rng) > 0.15f ? 2 : 1, block);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < block; ++i)
                buf.setSample (c, i, 0.9f * (2.0f * u (rng) - 1.0f));

        eng.process (buf, p);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < block; ++i)
            {
                const float s = buf.getSample (c, i);
                if (! std::isfinite (s)) finite = false;
                maxAbs = std::max (maxAbs, std::fabs (s));
            }
    }
    check (finite && maxAbs < 200.0f, fmt ("600 randomised blocks: all finite, peak %.1f", maxAbs));

    // Mid/side on a mono signal: both channels must remain identical.
    Rig ms;
    ms.p.midSide = true;
    ms.p.sat.drive[0] = ms.p.sat.drive[1] = ms.p.sat.drive[2] = 4.0;
    ms.p.sat.bias = 0.4;
    ms.p.fund = extendParams();
    const auto out = ms.run (sines ({ { 70.0, 0.4 }, { 900.0, 0.2 } }, 24000));
    float maxDiff = 0;
    for (size_t i = 0; i < out.l.size(); ++i) maxDiff = std::max (maxDiff, std::fabs (out.l[i] - out.r[i]));
    check (maxDiff < 1.0e-6f, fmt ("mid/side keeps a mono input mono (max L-R %.1e)", maxDiff));
}

// ------------------------------------------------------------------------------------------
void testSpectrumAnalyzer()
{
    std::printf ("\n[10] Spectrum analyzer calibration (8192-point Hann, dBFS, tilt around 1 kHz)\n");
    const int N = SpectrumAnalyzer::fftSize;
    const double binHz = fs / N;

    // Pushes a sine on the given channels, lets the analyzer consume it, and returns the analyzer.
    const auto measure = [&] (double f, float tilt, bool bothChannels, SpectrumAnalyzer& an)
    {
        static StereoTap tap;
        const auto x = sines ({ { f, 0.5 } }, 3 * N); // 0.5 amplitude = -6.02 dBFS
        const std::vector<float> silence (x.size(), 0.0f);
        const float* ch[2] = { x.data(), bothChannels ? x.data() : silence.data() };
        tap.push (ch, 2, (int) x.size());
        an.setSampleRate (fs);
        an.setTilt (tilt);
        an.update (tap);
    };

    const double f1k = 171 * binHz, f4k = 683 * binHz; // bin-centred
    SpectrumAnalyzer flat, tilted, offBin, oneSided;
    measure (f1k, 0.0f, true, flat);
    measure (f4k, 4.5f, true, tilted);
    measure (f1k + 0.5 * binHz, 0.0f, true, offBin);
    measure (f1k, 0.0f, false, oneSided);

    const double sixDb = -6.02;
    check (std::fabs (flat.peakInRange (900, 1100) - sixDb) < 0.15,
           fmt ("bin-centred 1 kHz sine at -6.02 dBFS reads %.2f dB", flat.peakInRange (900, 1100)));
    check (std::fabs (tilted.peakInRange (3900, 4100) - (sixDb + 4.5 * std::log2 (f4k / 1000.0))) < 0.15,
           fmt ("4 kHz with 4.5 dB/oct tilt reads %.2f dB (expected %.2f)", tilted.peakInRange (3900, 4100),
                sixDb + 4.5 * std::log2 (f4k / 1000.0)));
    check (std::fabs (offBin.peakInRange (900, 1100) - sixDb) < 1.5,
           fmt ("worst-case off-bin sine (Hann scalloping) reads %.2f dB, within 1.5 dB", offBin.peakInRange (900, 1100)));
    check (std::fabs (oneSided.peakInRange (900, 1100) - (sixDb - 3.01)) < 0.2,
           fmt ("left-only signal reads %.2f dB: L/R power is averaged, not summed", oneSided.peakInRange (900, 1100)));
    check (std::fabs (tilted.getTilt() - SpectrumAnalyzer::defaultTilt) < 1.0e-6f && SpectrumAnalyzer::defaultTilt == 4.5f,
           "default tilt is 4.5 dB/oct, as in FabFilter Pro-Q");
}

void benchmark()
{
    std::printf ("\n[11] CPU: 10 s of stereo @ 48 kHz, everything on\n");
    const auto in = sines ({ { 55.0, 0.3 }, { 440.0, 0.2 }, { 5000.0, 0.1 } }, 480000);

    for (int os = 0; os < 4; ++os)
    {
        Rig rig (os);
        rig.p.sat.drive[0] = rig.p.sat.drive[1] = rig.p.sat.drive[2] = 3.0;
        rig.p.sat.bias = 0.3;
        rig.p.fund = extendParams();
        rig.p.fund.resGain = 1.0;

        const auto t0 = std::chrono::steady_clock::now();
        rig.run (in, nullptr, 512);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  os=%d: %.2f s for 10 s of audio  ->  %.1f%% of one core\n", os, sec, sec * 10.0);
    }
}
}

int main()
{
    testTransparency();
    testLatencyAlignment();
    testAliasing();
    testEvenOdd();
    testExtend();
    testExtendNoteOff();
    testRestore();
    testBassStaysCentred();
    testMixDeltaMatch();
    testRobustness();
    testSpectrumAnalyzer();
    benchmark();
    return finish ("ALL TESTS");
}
