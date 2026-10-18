// Self-contained tests for the Themis DSP core (no framework dependency).
#include <themis/Compressor.h>
#include <themis/Engine.h>
#include <themis/EqFit.h>
#include <themis/Limiter.h>
#include <themis/LowCrestMeter.h>
#include <themis/Loudness.h>
#include <themis/StereoScope.h>
#include <themis/StereoTools.h>
#include <themis/TargetCurve.h>
#include <themis/TonalAnalyzer.h>
#include <themis/Translation.h>

#include <chrono>
#include <cstdio>
#include <random>

using namespace themis;

static int g_failures = 0, g_checks = 0;

#define CHECK(cond)                                                                       \
    do                                                                                    \
    {                                                                                     \
        ++g_checks;                                                                       \
        if (!(cond))                                                                      \
        {                                                                                 \
            ++g_failures;                                                                 \
            std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                              \
    do                                                                                                     \
    {                                                                                                      \
        ++g_checks;                                                                                        \
        const double va = (a), vb = (b), vt = (tol);                                                       \
        if (!(std::abs (va - vb) <= vt))                                                                   \
        {                                                                                                  \
            ++g_failures;                                                                                  \
            std::printf ("  FAIL %s:%d  %s = %.5f, expected %.5f +/- %.5f\n", __FILE__, __LINE__, #a, va, vb, vt); \
        }                                                                                                  \
    } while (0)

#define TEST(name)                          \
    static void name();                     \
    struct Reg_##name                       \
    {                                       \
        Reg_##name() { tests().push_back ({ #name, name }); } \
    } reg_##name;                           \
    static void name()

struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }

// ---- helpers ---------------------------------------------------------------------------------
static std::vector<float> sine (double fs, double hz, double ampDb, double seconds, double phase = 0.0)
{
    std::vector<float> v (static_cast<size_t> (fs * seconds));
    const double a = std::pow (10.0, ampDb / 20.0);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float> (a * std::sin (2.0 * kPi * hz * static_cast<double> (i) / fs + phase));
    return v;
}

static double rmsDb (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double> (v[i]) * v[i];
    return 10.0 * std::log10 (s / static_cast<double> (to - from) + 1e-20);
}

static std::vector<float> whiteNoise (size_t n, unsigned seed, float amp = 0.25f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v (n);
    for (auto& x : v)
        x = d (rng) * amp;
    return v;
}

// Paul Kellet's refined pink filter (+/-0.05 dB from 10 Hz to Nyquist).
static std::vector<float> pinkNoise (size_t n, unsigned seed, float amp = 0.25f)
{
    auto w = whiteNoise (n, seed, 1.0f);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& x : w)
    {
        const double white = x;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        x = static_cast<float> (pink * 0.11 * amp);
    }
    return w;
}

// Brute-force 8x oversampled true peak using a long windowed sinc (independent of TruePeakDetector).
static float referenceTruePeak (const std::vector<float>& x)
{
    constexpr int up = 8, halfLen = 32;
    float peak = 0;
    for (size_t i = static_cast<size_t> (halfLen); i + static_cast<size_t> (halfLen) < x.size(); ++i)
        for (int p = 0; p < up; ++p)
        {
            double acc = 0;
            const double frac = static_cast<double> (p) / up;
            for (int k = -halfLen + 1; k <= halfLen; ++k)
            {
                // signal value at time i + frac, interpolated from neighbours x[i + k]
                const double t = frac - k;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double win = 0.5 + 0.5 * std::cos (kPi * t / (halfLen + 1));
                acc += x[static_cast<size_t> (static_cast<long> (i) + k)] * sinc * win;
            }
            peak = std::max (peak, static_cast<float> (std::abs (acc)));
        }
    return peak;
}

// ---- tests -----------------------------------------------------------------------------------
TEST (biquad_responses)
{
    const double fs = 48000;
    CHECK_NEAR (BiquadCoeffs::peak (fs, 1000, 6.0, 1.0).magnitudeDb (fs, 1000), 6.0, 0.01);
    CHECK_NEAR (BiquadCoeffs::peak (fs, 1000, -4.0, 2.0).magnitudeDb (fs, 20), 0.0, 0.05);
    CHECK_NEAR (BiquadCoeffs::lowShelf (fs, 200, 5.0, 0.707).magnitudeDb (fs, 20), 5.0, 0.15);
    CHECK_NEAR (BiquadCoeffs::highShelf (fs, 8000, -3.0, 0.707).magnitudeDb (fs, 20000), -3.0, 0.3);
    CHECK_NEAR (BiquadCoeffs::highPass (fs, 100, 0.7071).magnitudeDb (fs, 100), -3.01, 0.05);

    // Measured steady-state gain through the actual filter matches the analytic response.
    Biquad f;
    f.setCoeffs (BiquadCoeffs::peak (fs, 1000, 6.0, 1.0));
    auto s = sine (fs, 1000, -20, 1.0);
    for (auto& x : s)
        x = f.process (x);
    CHECK_NEAR (rmsDb (s, 24000, 48000) - rmsDb (sine (fs, 1000, -20, 1.0), 24000, 48000), 6.0, 0.05);
}

TEST (k_weighting_matches_itu_table_at_48k)
{
    const auto kw = makeKWeighting (48000.0);
    CHECK_NEAR (kw.first.b0, 1.53512485958697, 1e-6);
    CHECK_NEAR (kw.first.b1, -2.69169618940638, 1e-6);
    CHECK_NEAR (kw.first.b2, 1.19839281085285, 1e-6);
    CHECK_NEAR (kw.first.a1, -1.69065929318241, 1e-6);
    CHECK_NEAR (kw.first.a2, 0.73248077421585, 1e-6);
    CHECK_NEAR (kw.second.a1, -1.99004745483398, 1e-6);
    CHECK_NEAR (kw.second.a2, 0.99007225036621, 1e-6);
}

TEST (loudness_of_reference_sine)
{
    // BS.1770: a stereo 997 Hz sine at -20 dBFS peak in both channels reads -20.0 LUFS.
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter lm;
        lm.prepare (fs);
        auto s = sine (fs, 997, -20, 10.0);
        lm.process (s.data(), s.data(), static_cast<int> (s.size()));
        CHECK_NEAR (lm.momentary(), -20.0, 0.1);
        CHECK_NEAR (lm.shortTerm(), -20.0, 0.1);
        CHECK_NEAR (lm.integrated(), -20.0, 0.1);
    }
}

TEST (loudness_gating_ignores_quiet_passages)
{
    const double fs = 48000;
    LoudnessMeter lm;
    lm.prepare (fs);
    auto loud = sine (fs, 997, -20, 6.0);
    auto quiet = sine (fs, 997, -60, 6.0); // > -70 abs gate, but 40 LU under: removed by relative gate
    lm.process (loud.data(), loud.data(), static_cast<int> (loud.size()));
    lm.process (quiet.data(), quiet.data(), static_cast<int> (quiet.size()));
    CHECK_NEAR (lm.integrated(), -20.0, 0.15);
    CHECK_NEAR (lm.momentary(), -60.0, 0.2);
}

TEST (true_peak_finds_intersample_peaks)
{
    // fs/4 sine at 45 degrees: every sample is +/-0.7071 but the waveform peaks at 1.0.
    auto s = sine (48000, 12000, 0.0, 0.1, kPi / 4.0);
    float samplePeak = 0, tp = 0;
    TruePeakDetector d;
    for (float x : s)
    {
        samplePeak = std::max (samplePeak, std::abs (x));
        tp = std::max (tp, d.process (x));
    }
    CHECK_NEAR (samplePeak, 0.7071, 0.001);
    CHECK_NEAR (tp, 1.0, 0.03);
}

TEST (limiter_respects_ceiling_and_is_transparent_below_it)
{
    const double fs = 48000;
    const size_t n = 96000;

    // Hot programme: noise band-limited to 16 kHz (like real music) plus a tone, ~10 dB over the ceiling.
    auto bandLimit = [] (std::vector<float> x, double fc) {
        const int L = 255;
        std::vector<double> h (L);
        for (int k = 0; k < L; ++k)
        {
            const double t = k - (L - 1) / 2.0, f = fc / 24000.0;
            const double s = std::abs (t) < 1e-9 ? f : std::sin (kPi * f * t) / (kPi * t);
            h[static_cast<size_t> (k)] = s * (0.42 - 0.5 * std::cos (2 * kPi * k / (L - 1)) + 0.08 * std::cos (4 * kPi * k / (L - 1)));
        }
        std::vector<float> y (x.size(), 0.0f);
        for (size_t i = L; i < x.size(); ++i)
        {
            double a = 0;
            for (int k = 0; k < L; ++k)
                a += h[static_cast<size_t> (k)] * x[i - static_cast<size_t> (k)];
            y[i] = static_cast<float> (a);
        }
        return y;
    };
    auto tone = sine (fs, 3100, -2.0, 2.0);
    auto nl = bandLimit (whiteNoise (n, 7, 1.0f), 16000), nr = bandLimit (whiteNoise (n, 8, 1.0f), 16000);
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
    {
        l[i] = nl[i] * 4.0f + tone[i];
        r[i] = nr[i] * 4.0f + tone[i];
    }
    Limiter lim;
    lim.prepare (fs);
    LimiterParams p;
    p.ceilingDb = -1.0f;
    p.releaseMs = 80.0f;
    lim.process (l.data(), r.data(), static_cast<int> (n), p, true);

    float sp = 0;
    for (size_t i = 0; i < n; ++i)
        sp = std::max ({ sp, std::abs (l[i]), std::abs (r[i]) });
    CHECK (sp <= dbToLin (-1.0f) * 1.001f);
    const float tpDb = linToDb (std::max (referenceTruePeak (l), referenceTruePeak (r)));
    std::printf ("  band-limited program: sample peak %.2f dB, true peak %.2f dB (ceiling -1.0)\n", linToDb (sp), tpDb);
    CHECK (tpDb <= -1.0f + 0.1f);
    CHECK (lim.gainReductionDb() < -3.0f);

    // Worst case: full-band white noise. Any 4x interpolator under-reads near Nyquist; stay within 1.5 dB.
    {
        auto wl = whiteNoise (n, 17, 1.0f), wr = whiteNoise (n, 18, 1.0f);
        for (size_t i = 0; i < n; ++i)
        {
            wl[i] *= 3.0f;
            wr[i] *= 3.0f;
        }
        Limiter lw;
        lw.prepare (fs);
        lw.process (wl.data(), wr.data(), static_cast<int> (n), p, true);
        const float wtp = linToDb (std::max (referenceTruePeak (wl), referenceTruePeak (wr)));
        std::printf ("  full-band noise: true peak %.2f dB (ceiling -1.0)\n", wtp);
        CHECK (wtp <= -1.0f + 1.5f);
    }

    // Below the ceiling the limiter is a pure delay.
    auto quiet = sine (fs, 440, -12, 1.0);
    std::vector<float> ql = quiet, qr = quiet;
    Limiter lim2;
    lim2.prepare (fs);
    lim2.process (ql.data(), qr.data(), static_cast<int> (quiet.size()), p, true);
    const int D = lim2.latencySamples();
    double maxErr = 0;
    for (size_t i = static_cast<size_t> (D); i < quiet.size(); ++i)
        maxErr = std::max (maxErr, static_cast<double> (std::abs (ql[i] - quiet[i - static_cast<size_t> (D)])));
    CHECK (maxErr < 1e-6);
}

TEST (compressor_static_curve)
{
    const double fs = 48000;
    Compressor c;
    c.prepare (fs);
    CompParams p;
    p.thresholdDb = -20;
    p.ratio = 4;
    p.attackMs = 1;
    p.releaseMs = 50;
    p.sidechainHpHz = 20;
    auto s = sine (fs, 1000, -10, 2.0);
    auto copy = s;
    c.process (s.data(), copy.data(), static_cast<int> (s.size()), p);
    // 10 dB over threshold at 4:1 => 7.5 dB reduction
    CHECK_NEAR (rmsDb (s, 72000, 96000) - rmsDb (sine (fs, 1000, -10, 2.0), 72000, 96000), -7.5, 0.4);
    CHECK_NEAR (c.gainReductionDb(), -7.5, 0.6);

    // Dry/wet mix = 0 is transparent
    p.mix = 0.0f;
    auto t = sine (fs, 1000, -10, 0.5), t2 = t;
    c.process (t.data(), t2.data(), static_cast<int> (t.size()), p);
    CHECK_NEAR (rmsDb (t, 0, t.size()), rmsDb (sine (fs, 1000, -10, 0.5), 0, t.size()), 0.001);
}

TEST (eq_mid_side_targeting)
{
    const double fs = 48000;
    auto run = [&] (EqTarget target, bool monoInput) {
        MasteringEq eq;
        eq.prepare (fs);
        std::array<EqBandParams, MasteringEq::kNumBands> b {};
        for (auto& x : b)
            x.enabled = false;
        b[3].enabled = true;
        b[3].freq = 1000;
        b[3].gainDb = 6;
        b[3].q = 1;
        b[3].target = target;
        eq.setBands (b);
        eq.reset();
        auto tone = sine (fs, 1000, -20, 1.0);
        std::vector<float> l = tone, r;
        if (monoInput)
            r = tone;
        else
        {
            r = tone;
            for (auto& x : r)
                x = -x; // pure side
        }
        eq.process (l.data(), r.data(), static_cast<int> (l.size()));
        return rmsDb (l, 24000, 48000) - rmsDb (tone, 24000, 48000);
    };
    CHECK_NEAR (run (EqTarget::Stereo, true), 6.0, 0.1);
    CHECK_NEAR (run (EqTarget::Mid, true), 6.0, 0.1);   // mono content is all mid
    CHECK_NEAR (run (EqTarget::Side, true), 0.0, 0.01); // ...and untouched by a side band
    CHECK_NEAR (run (EqTarget::Side, false), 6.0, 0.1);
    CHECK_NEAR (run (EqTarget::Mid, false), 0.0, 0.01);
}

TEST (stereo_tools)
{
    const double fs = 48000;
    StereoTools st;
    st.prepare (fs);
    auto tone = sine (fs, 5000, -12, 0.5);
    std::vector<float> l = tone, r = tone;
    for (auto& x : r)
        x *= -0.5f;
    auto l0 = l, r0 = r;

    st.process (l.data(), r.data(), static_cast<int> (l.size()), 100.0f, 0.0f);
    CHECK (l == l0 && r == r0); // unity width, no mono-below: untouched

    st.process (l.data(), r.data(), static_cast<int> (l.size()), 0.0f, 0.0f);
    double diff = 0;
    for (size_t i = 0; i < l.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (l[i] - r[i])));
    CHECK (diff < 1e-6); // width 0 => mono

    // Mono-below: a 50 Hz side signal is crushed, a 5 kHz one passes.
    for (double hz : { 50.0, 5000.0 })
    {
        StereoTools s2;
        s2.prepare (fs);
        auto t = sine (fs, hz, -12, 1.0);
        std::vector<float> a = t, b = t;
        for (auto& x : b)
            x = -x;
        s2.process (a.data(), b.data(), static_cast<int> (a.size()), 100.0f, 150.0f);
        const double outDb = rmsDb (a, 24000, 48000), inDb = rmsDb (t, 24000, 48000);
        if (hz < 100)
            CHECK (outDb - inDb < -20.0);
        else
            CHECK_NEAR (outDb - inDb, 0.0, 0.2);
    }
}

TEST (tonal_analyzer_pink_is_flat_white_tilts_3db_per_octave)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 20);
    BandArray out {};
    auto at = [] (const BandArray& a, double hz) { return a[static_cast<size_t> (std::lround (BandGrid::indexForHz (hz)))]; };

    {
        auto pink = pinkNoise (n, 3);
        CHECK (TonalAnalyzer::analyseOffline (pink.data(), pink.data(), n, fs, out));
        for (double hz : { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0 })
            CHECK_NEAR (at (out, hz), 0.0, 1.2);
    }
    {
        auto white = whiteNoise (n, 5);
        CHECK (TonalAnalyzer::analyseOffline (white.data(), white.data(), n, fs, out));
        CHECK_NEAR (at (out, 8000) - at (out, 1000), 9.0, 1.0);   // +3 dB/oct in constant-Q energy
        CHECK_NEAR (at (out, 1000) - at (out, 125), 9.0, 1.0);
    }
    {
        auto tone = sine (fs, 1000, -20, 10.0);
        auto bg = whiteNoise (tone.size(), 1, 0.001f);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] += bg[i];
        CHECK (TonalAnalyzer::analyseOffline (tone.data(), tone.data(), n / 2, fs, out));
        int peak = 0;
        for (int i = 1; i < BandGrid::kNum; ++i)
            if (out[static_cast<size_t> (i)] > out[static_cast<size_t> (peak)])
                peak = i;
        CHECK (std::abs (BandGrid::centreHz (peak) - 1000.0) < 1000.0 * 0.12);
    }
}

static BandArray analyse (const std::vector<float>& x, double fs)
{
    BandArray out {};
    CHECK (TonalAnalyzer::analyseOffline (x.data(), x.data(), x.size(), fs, out));
    return out;
}

static std::vector<float> scaledToRmsDb (std::vector<float> x, double targetDb)
{
    const float g = static_cast<float> (std::pow (10.0, (targetDb - rmsDb (x, 0, x.size())) / 20.0));
    for (auto& v : x)
        v *= g;
    return x;
}

TEST (tonal_analyzer_is_independent_of_level)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 20);
    // Coloured material (white noise: +3 dB/oct on this grid) so the check is not satisfied by a flat curve.
    const auto white = whiteNoise (n, 17);
    const auto ref = analyse (white, fs);
    for (double db : { -6.0, -30.0, -60.0, -80.0 })
    {
        const auto got = analyse (scaledToRmsDb (white, db), fs);
        for (int i = 0; i <= BandGrid::indexForHz (16000.0); ++i)
            CHECK_NEAR (got[static_cast<size_t> (i)], ref[static_cast<size_t> (i)], 0.01);
    }
}

TEST (tonal_analyzer_weights_loud_and_quiet_passages_equally)
{
    const double fs = 48000;
    const size_t half = static_cast<size_t> (fs * 12);
    const auto loud = scaledToRmsDb (pinkNoise (half, 3), -18.0);
    const auto white = whiteNoise (half, 5);

    // pink then white, the white part 20 dB or 10 dB below the pink: the result is the same either way
    auto programme = [&] (double whiteDb) {
        auto x = loud;
        const auto w = scaledToRmsDb (white, whiteDb);
        x.insert (x.end(), w.begin(), w.end());
        return analyse (x, fs);
    };
    const auto a = programme (-38.0), b = programme (-28.0);
    for (int i = 4; i <= BandGrid::indexForHz (16000.0); ++i)
        CHECK_NEAR (a[static_cast<size_t> (i)], b[static_cast<size_t> (i)], 0.3);

    // and the quiet half really does count: halfway between the pink (flat) and white (+9 dB over 1k..8k) shapes
    auto at = [] (const BandArray& x, double hz) { return x[static_cast<size_t> (std::lround (BandGrid::indexForHz (hz)))]; };
    CHECK_NEAR (at (a, 8000) - at (a, 1000), 4.5, 1.5);
}

TEST (tonal_analyzer_gates_far_quieter_frames_relative_to_the_programme)
{
    const double fs = 48000;
    const size_t half = static_cast<size_t> (fs * 12);
    auto x = scaledToRmsDb (pinkNoise (half, 3), -20.0);
    const auto hiss = scaledToRmsDb (whiteNoise (half, 5), -75.0); // 55 dB below the programme, still above digital silence
    x.insert (x.end(), hiss.begin(), hiss.end());
    const auto got = analyse (x, fs);
    auto at = [&] (double hz) { return got[static_cast<size_t> (std::lround (BandGrid::indexForHz (hz)))]; };
    CHECK_NEAR (at (8000) - at (1000), 0.0, 1.0); // would be ~4.5 dB if the hiss counted

    // the same programme 40 dB lower is gated identically (a fixed dBFS gate would let the hiss in)
    auto y = scaledToRmsDb (pinkNoise (half, 3), -60.0);
    const auto hiss2 = scaledToRmsDb (whiteNoise (half, 5), -115.0);
    y.insert (y.end(), hiss2.begin(), hiss2.end());
    const auto got2 = analyse (y, fs);
    for (int i = 0; i <= BandGrid::indexForHz (16000.0); ++i)
        CHECK_NEAR (got2[static_cast<size_t> (i)], got[static_cast<size_t> (i)], 0.05);
}

TEST (tonal_analyzer_low_bands_are_accurate)
{
    // Pink noise is flat on this grid; the lowest bands span only a few FFT bins, so this catches bin-count jitter.
    const double fs = 48000;
    const auto out = analyse (pinkNoise (static_cast<size_t> (fs * 60), 31), fs);
    for (int i = 0; i <= BandGrid::indexForHz (250.0); ++i)
        CHECK_NEAR (out[static_cast<size_t> (i)], 0.0, 1.0);
}

TEST (stereo_scope_correlation_and_samples)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 3);
    const auto a = whiteNoise (n, 1), b = whiteNoise (n, 2);
    auto feed = [&] (StereoScope& s, const std::vector<float>& l, const std::vector<float>& r) {
        for (size_t pos = 0; pos < n; pos += 480)
            s.push (l.data() + pos, r.data() + pos, static_cast<int>(std::min<size_t> (480, n - pos)));
    };
    std::vector<float> neg (n);
    for (size_t i = 0; i < n; ++i)
        neg[i] = -a[i];

    StereoScope s;
    s.prepare (fs);
    CHECK (!s.hasData());
    feed (s, a, a);
    CHECK (s.hasData());
    CHECK_NEAR (s.correlation(), 1.0, 5e-3);
    feed (s, a, neg);
    CHECK_NEAR (s.correlation(), -1.0, 5e-3);
    feed (s, a, b);
    CHECK_NEAR (s.correlation(), 0.0, 0.15);

    // silence keeps the last value instead of dividing by zero
    const std::vector<float> zero (n, 0.0f);
    const float before = s.correlation();
    feed (s, zero, zero);
    CHECK_NEAR (s.correlation(), before, 1e-3);

    s.requestReset();
    float l1[4] = { 1, 2, 3, 4 }, r1[4] = { -1, -2, -3, -4 };
    s.push (l1, r1, 4);
    CHECK (!s.hasData() || s.correlation() < -0.9f); // reset applied, only the new block counts

    // newest samples, oldest first, and only as many as exist since prepare
    StereoScope t;
    t.prepare (fs);
    float lo[8] = {}, ro[8] = {};
    CHECK (t.copyRecent (lo, ro, 8) == 0);
    t.push (l1, r1, 4);
    CHECK (t.copyRecent (lo, ro, 8) == 4);
    CHECK (lo[0] == 1.0f && lo[3] == 4.0f && ro[3] == -4.0f);
    CHECK (t.copyRecent (lo, ro, 2) == 2);
    CHECK (lo[0] == 3.0f && lo[1] == 4.0f);

    float x = 0, y = 0;
    StereoScope::toXY (1.0f, 1.0f, x, y); // mono: vertical
    CHECK_NEAR (x, 0.0, 1e-6);
    CHECK (y > 1.4f);
    StereoScope::toXY (1.0f, 0.0f, x, y); // left only: up-left
    CHECK (x < 0.0f && y > 0.0f);
    StereoScope::toXY (0.0f, 1.0f, x, y);
    CHECK (x > 0.0f && y > 0.0f);
}

TEST (tonal_analyzer_cost_per_frame)
{
    TonalAnalyzer a;
    a.prepare (48000);
    auto noise = whiteNoise (48000 * 3, 11);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < noise.size(); pos += 128)
        a.process (noise.data() + pos, noise.data() + pos, 128);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  (3 s of audio analysed in %.1f ms; ~%.2f ms per 4096-sample frame)\n", ms, ms / (3.0 * 48000 / 4096));
    CHECK (ms < 3000.0 * 0.05); // < 5% realtime
}

TEST (fft_real_and_complex_match_naive_dft)
{
    std::mt19937 rng (21);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (int n : { 8, 64, 1024 })
    {
        std::vector<float> x (static_cast<size_t> (n)), im (static_cast<size_t> (n));
        for (auto& v : x)
            v = d (rng);
        for (auto& v : im)
            v = d (rng);

        // complex transform
        std::vector<float> cr = x, ci = im;
        FFT (n).transform (cr.data(), ci.data());
        // real transform
        std::vector<float> rr (static_cast<size_t> (n / 2 + 1)), ri (rr.size()), pw (rr.size());
        RealFFT rf (n);
        rf.forward (x.data(), rr.data(), ri.data());
        rf.powerSpectrum (x.data(), pw.data(), 2.0f);

        double worstC = 0.0, worstR = 0.0, worstP = 0.0;
        for (int k = 0; k <= n / 2; ++k)
        {
            double xr = 0, xi = 0, yr = 0, yi = 0;
            for (int t = 0; t < n; ++t)
            {
                const double a = -2.0 * kPi * k * t / n, c = std::cos (a), sn = std::sin (a);
                xr += x[static_cast<size_t> (t)] * c;
                xi += x[static_cast<size_t> (t)] * sn;
                yr += x[static_cast<size_t> (t)] * c - im[static_cast<size_t> (t)] * sn;
                yi += x[static_cast<size_t> (t)] * sn + im[static_cast<size_t> (t)] * c;
            }
            worstC = std::max (worstC, std::abs (yr - cr[static_cast<size_t> (k)]) + std::abs (yi - ci[static_cast<size_t> (k)]));
            worstR = std::max (worstR, std::abs (xr - rr[static_cast<size_t> (k)]) + std::abs (xi - ri[static_cast<size_t> (k)]));
            worstP = std::max (worstP, std::abs (2.0 * (xr * xr + xi * xi) - pw[static_cast<size_t> (k)]));
        }
        const double tol = 1.0e-5 * n;
        CHECK (worstC < tol);
        CHECK (worstR < tol);
        CHECK (worstP < tol * n);
    }
}

TEST (tonal_analyzer_threaded_api_matches_process_and_survives_backlog)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 12);
    const auto pink = pinkNoise (n, 9);
    BandArray ref {}, got {};

    TonalAnalyzer a;
    a.prepare (fs);
    a.setAveragingSeconds (0.0f);
    for (size_t pos = 0; pos < n; pos += 128)
        a.process (pink.data() + pos, pink.data() + pos, static_cast<int> (std::min<size_t> (128, n - pos)));
    CHECK (a.hasData());
    a.readNormalisedDb (ref);

    // push() in odd-sized blocks, pump() only now and then (wraps the ring several times, never overflows it)
    TonalAnalyzer b;
    b.prepare (fs);
    b.setAveragingSeconds (0.0f);
    int blocks = 0;
    for (size_t pos = 0; pos < n; pos += 1531, ++blocks)
    {
        const int c = static_cast<int> (std::min<size_t> (1531, n - pos));
        b.push (pink.data() + pos, pink.data() + pos, c);
        if (blocks % 20 == 0)
            b.pump();
    }
    b.pump();
    CHECK (b.hasData());
    b.readNormalisedDb (got);
    for (size_t i = 0; i < ref.size(); ++i)
        CHECK_NEAR (got[i], ref[i], 1.0e-3);

    // a worker that never keeps up skips frames but must still produce a sensible curve
    TonalAnalyzer c;
    c.prepare (fs);
    c.setAveragingSeconds (0.0f);
    for (size_t pos = 0; pos < n; pos += 512)
        c.push (pink.data() + pos, pink.data() + pos, static_cast<int> (std::min<size_t> (512, n - pos)));
    c.pump();
    CHECK (c.hasData());
    c.readNormalisedDb (got);
    for (double hz : { 125.0, 1000.0, 8000.0 })
        CHECK_NEAR (got[static_cast<size_t> (std::lround (BandGrid::indexForHz (hz)))], 0.0, 1.5);

    // reset discards earlier audio and is applied by pump()
    c.requestReset();
    c.pump();
    CHECK (!c.hasData());
    c.push (pink.data(), pink.data(), 8192);
    c.pump();
    CHECK (!c.hasData()); // less than one frame since the reset
}

TEST (low_crest_meter)
{
    const double fs = 48000;
    const int n = static_cast<int> (fs * 6);
    auto run = [&] (LowCrestMeter& m, const std::vector<float>& x) {
        for (int pos = 0; pos < n; pos += 512)
        {
            const int c = std::min (512, n - pos);
            m.process (x.data() + pos, x.data() + pos, c);
        }
    };

    // A steady low sine has a crest factor of sqrt(2) = 3.01 dB.
    std::vector<float> sine (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        sine[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * 60.0 * i / fs));
    LowCrestMeter a;
    a.prepare (fs);
    CHECK (!a.hasData());
    run (a, sine);
    CHECK (a.hasData());
    CHECK_NEAR (a.crestDb(), 3.01, 0.15);

    // Content above the band does not count: a 5 kHz sine is filtered out (gated as silence).
    std::vector<float> hi (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        hi[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * 5000.0 * i / fs));
    LowCrestMeter b;
    b.prepare (fs);
    run (b, hi);
    CHECK (!b.hasData());

    // Decaying 50 Hz "kicks" once per second are far more peaky than a sustained tone.
    std::vector<float> kick (static_cast<size_t> (n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / fs, 1.0);
        kick[static_cast<size_t> (i)] = 0.8f * static_cast<float> (std::exp (-t * 18.0) * std::sin (2.0 * kPi * 50.0 * t));
    }
    LowCrestMeter c;
    c.prepare (fs);
    run (c, kick);
    CHECK (c.hasData());
    CHECK (c.crestDb() > a.crestDb() + 3.0f);

    // reset discards the measurement, immediately for readers even before more audio arrives
    c.requestReset();
    CHECK (!c.hasData());
    c.process (kick.data(), kick.data(), 64);
    CHECK (!c.hasData());

    // a burst of NaN must not poison the meter: it recovers once clean audio returns
    LowCrestMeter d;
    d.prepare (fs);
    std::vector<float> bad (4800, std::nanf (""));
    d.process (bad.data(), bad.data(), static_cast<int> (bad.size()));
    run (d, sine);
    CHECK (d.hasData());
    CHECK_NEAR (d.crestDb(), 3.01, 0.15);
}

TEST (target_curve_json_roundtrip_and_tracks)
{
    const auto& built = TargetCurve::builtIns();
    CHECK (built.size() >= 6);
    for (const auto& c : built)
    {
        // normalised to 0 dB mean over the anchor region
        double s = 0;
        int cnt = 0;
        for (int i = 0; i < BandGrid::kNum; ++i)
            if (BandGrid::centreHz (i) >= 200 && BandGrid::centreHz (i) <= 5000)
            {
                s += c.medianDb[static_cast<size_t> (i)];
                ++cnt;
            }
        CHECK_NEAR (s / cnt, 0.0, 0.3);
        TargetCurve back;
        CHECK (TargetCurve::fromJson (c.toJson(), back));
        CHECK (back.name == c.name);
        for (size_t i = 0; i < c.medianDb.size(); ++i)
        {
            CHECK_NEAR (back.medianDb[i], c.medianDb[i], 0.001);
            CHECK_NEAR (back.halfWidthDb[i], c.halfWidthDb[i], 0.001);
        }
    }
    TargetCurve junk;
    CHECK (!TargetCurve::fromJson ("{\"hello\":1}", junk));
    CHECK (!TargetCurve::fromJson ("{\"themisCurve\":1,\"median\":[1,2,3],\"width\":[1]}", junk));

    BandArray a {}, b {};
    for (size_t i = 0; i < a.size(); ++i)
    {
        a[i] = 1.0f;
        b[i] = -1.0f;
    }
    const auto avg = TargetCurve::fromTracks ("two", { a, b });
    CHECK_NEAR (avg.medianDb[30], 0.0, 0.01);
    CHECK (avg.halfWidthDb[30] >= 1.5f);

    // A low-sample-rate reference has no data above its Nyquist: that must not leak into the curve.
    BandArray lowRate {};
    for (size_t i = 0; i < lowRate.size(); ++i)
        lowRate[i] = i < 56 ? 3.0f : -195.0f;
    const auto lr = TargetCurve::fromTracks ("lowrate", { lowRate });
    for (size_t i = 0; i < lr.medianDb.size(); ++i)
        CHECK_NEAR (lr.medianDb[i], 0.0, 0.01); // flat input, extended flat, normalised to 0
    // Mixed with a full-bandwidth track, the empty bands take their statistics from that track alone.
    BandArray full {};
    full.fill (3.0f);
    full[58] = 9.0f;
    const auto mixed = TargetCurve::fromTracks ("mixed", { lowRate, full }, 1.5f, 2.0f);
    for (size_t i = 0; i < mixed.medianDb.size(); ++i)
        CHECK (mixed.medianDb[i] > -10.0f);
    CHECK (mixed.medianDb[59] > mixed.medianDb[40]);
}

TEST (eq_fit_reduces_error)
{
    const double fs = 48000;
    const auto target = TargetCurve::builtIns()[1];

    // Pretend the mix is the target "broken" by a known EQ: +4 dB bump at 2.5 kHz, -3 dB low shelf, -2 dB air.
    std::array<EqBandParams, MasteringEq::kNumBands> broken {};
    for (auto& b : broken)
        b.enabled = false;
    broken[1] = { true, 90.0f, -3.0f, 0.7071f, EqTarget::Stereo };
    broken[3] = { true, 2500.0f, 4.0f, 1.0f, EqTarget::Stereo };
    broken[5] = { true, 9000.0f, -2.0f, 0.7071f, EqTarget::Stereo };
    BandArray measured {};
    for (int i = 0; i < BandGrid::kNum; ++i)
        measured[static_cast<size_t> (i)] = target.medianDb[static_cast<size_t> (i)]
                                            + static_cast<float> (MasteringEq::responseDb (broken, fs, BandGrid::centreHz (i)));
    normaliseAnchor (measured);

    const auto fit = fitEqToTarget (measured, target, fs, 1.0f, 8.0f);
    std::printf ("  residual rms %.2f dB -> %.2f dB\n", fit.residualRmsBefore, fit.residualRmsAfter);
    CHECK (fit.residualRmsAfter < fit.residualRmsBefore * 0.45);

    // Applying the fit to the measured curve lands closer to the target than before.
    BandArray corrected = measured;
    for (int i = 0; i < BandGrid::kNum; ++i)
        corrected[static_cast<size_t> (i)] += static_cast<float> (MasteringEq::responseDb (fit.bands, fs, BandGrid::centreHz (i)));
    normaliseAnchor (corrected);
    double before = 0, after = 0;
    for (size_t i = 0; i < measured.size(); ++i)
    {
        before += std::pow (measured[i] - target.medianDb[i], 2);
        after += std::pow (corrected[i] - target.medianDb[i], 2);
    }
    std::printf ("  squared error vs target: %.1f -> %.1f\n", before, after);
    CHECK (after < before * 0.35);
}

TEST (engine_latency_bypass_ceiling_and_ab)
{
    const double fs = 48000;
    const int block = 256;

    EngineParams neutral;
    neutral.limOn = false;
    neutral.eqOn = false;
    for (auto& b : neutral.eq)
        b.enabled = false;

    auto runEngine = [&] (const EngineParams& p, const std::vector<float>& in, std::vector<float>& outL, Engine& e) {
        e.setParams (p);
        outL = in;
        std::vector<float> r = in;
        for (size_t pos = 0; pos < in.size(); pos += block)
        {
            const int n = static_cast<int> (std::min<size_t> (block, in.size() - pos));
            e.process (outL.data() + pos, r.data() + pos, n);
        }
    };

    // 1) neutral settings are a pure delay of latencySamples()
    {
        Engine e;
        e.prepare (fs, block);
        e.setParams (neutral);
        e.reset();
        auto in = whiteNoise (48000, 21, 0.3f);
        std::vector<float> out;
        runEngine (neutral, in, out, e);
        const size_t D = static_cast<size_t> (e.latencySamples());
        double err = 0;
        for (size_t i = D; i < in.size(); ++i)
            err = std::max (err, static_cast<double> (std::abs (out[i] - in[i - D])));
        CHECK (err < 1e-6);
    }

    // 2) full chain keeps true peak under the ceiling on a hot, EQ-boosted signal
    {
        Engine e;
        EngineParams p = neutral;
        p.eqOn = true;
        p.eq[2] = { true, 120.0f, 5.0f, 0.8f, EqTarget::Stereo };
        p.limOn = true;
        p.lim.driveDb = 9.0f;
        p.lim.ceilingDb = -1.0f;
        e.prepare (fs, block);
        e.setParams (p);
        e.reset();
        auto in = pinkNoise (48000 * 6, 4, 0.5f);
        std::vector<float> out;
        runEngine (p, in, out, e);
        CHECK (e.meters().truePeakDb < -1.0f + 0.4f);
        CHECK (e.meters().limiterGrDb < -0.5f);
        CHECK (e.meters().momentaryLufs > -30.0f);
    }

    // 3) hearOriginal returns the (delayed) input, level-matched to the processed loudness
    {
        Engine e;
        EngineParams p = neutral;
        p.outGainDb = 6.0f; // processed is 6 dB louder than the original
        e.prepare (fs, block);
        e.setParams (p);
        e.reset();
        auto in = pinkNoise (48000 * 20, 9, 0.2f);
        std::vector<float> out;
        runEngine (p, in, out, e); // lets the match gain converge
        CHECK_NEAR (e.meters().abMatchDb, 6.0, 0.4);

        EngineParams ab = p;
        ab.hearOriginal = true;
        auto tail = pinkNoise (48000 * 3, 10, 0.2f);
        std::vector<float> out2;
        runEngine (ab, tail, out2, e);
        const double outDb = rmsDb (out2, 96000, out2.size()), inDb = rmsDb (tail, 96000, tail.size());
        CHECK_NEAR (outDb - inDb, e.meters().abMatchDb, 0.3);
    }
}

// ---- mono fold + device translation --------------------------------------------------------------
static BandArray analyseFold (const std::vector<float>& l, const std::vector<float>& r, double fs)
{
    TonalAnalyzer a;
    a.prepare (fs);
    a.setAveragingSeconds (0.0f);
    for (size_t pos = 0; pos < l.size(); pos += 4096)
        a.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min<size_t> (4096, l.size() - pos)));
    CHECK (a.hasMonoFoldData());
    BandArray fold {};
    a.readMonoFoldDb (fold);
    return fold;
}

static double foldAt (const BandArray& f, double hz) { return f[static_cast<size_t> (std::lround (BandGrid::indexForHz (hz)))]; }

TEST (mono_fold_follows_channel_relationship)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 20);
    const auto a = pinkNoise (n, 3), b = pinkNoise (n, 4);
    std::vector<float> inverted (n), silent (n, 0.0f);
    for (size_t i = 0; i < n; ++i)
        inverted[i] = -a[i];

    const auto mono = analyseFold (a, a, fs), wide = analyseFold (a, b, fs), oneSided = analyseFold (a, silent, fs),
               opposed = analyseFold (a, inverted, fs);
    for (double hz : { 63.0, 250.0, 1000.0, 4000.0, 12000.0 })
    {
        CHECK_NEAR (foldAt (mono, hz), 0.0, 0.05);       // nothing lost
        CHECK_NEAR (foldAt (wide, hz), -3.0, 1.0);       // uncorrelated: half the power survives
        CHECK_NEAR (foldAt (oneSided, hz), -3.0, 0.05);  // one channel only: the same
        CHECK (foldAt (opposed, hz) < -30.0);            // cancels
    }
}

TEST (mono_fold_is_level_independent_and_resolves_frequency)
{
    const double fs = 48000;
    const size_t n = static_cast<size_t> (fs * 12);
    // Pink noise common to both channels, a 100 Hz tone in phase, a 4 kHz tone in opposite phase.
    auto l = pinkNoise (n, 5, 0.05f);
    auto r = l;
    const auto low = sine (fs, 100, -10, 12.0), high = sine (fs, 4000, -10, 12.0);
    for (size_t i = 0; i < n; ++i)
    {
        l[i] += low[i] + high[i];
        r[i] += low[i] - high[i];
    }
    const auto fold = analyseFold (l, r, fs);
    CHECK_NEAR (foldAt (fold, 100), 0.0, 0.5);
    CHECK (foldAt (fold, 4000) < -10.0);

    auto quiet = [&] (std::vector<float> v) {
        for (auto& x : v)
            x *= 0.001f;
        return v;
    };
    const auto fold2 = analyseFold (quiet (l), quiet (r), fs);
    for (int i = 0; i <= BandGrid::indexForHz (16000.0); ++i)
        CHECK_NEAR (fold2[static_cast<size_t> (i)], fold[static_cast<size_t> (i)], 0.05);
}

TEST (mono_fold_resets_and_is_zero_without_data)
{
    const double fs = 48000;
    const auto a = pinkNoise (static_cast<size_t> (fs * 6), 3), b = pinkNoise (a.size(), 4);
    TonalAnalyzer t;
    t.prepare (fs);
    CHECK (!t.hasMonoFoldData());
    t.process (a.data(), b.data(), static_cast<int> (a.size()));
    CHECK (t.hasMonoFoldData());
    t.requestReset();
    t.process (a.data(), b.data(), 1024);
    CHECK (!t.hasMonoFoldData());
    BandArray fold {};
    t.readMonoFoldDb (fold);
    for (float v : fold)
        CHECK (v == 0.0f);
}

TEST (device_profiles_have_plausible_responses)
{
    const auto& devices = DeviceProfile::builtIns();
    CHECK (devices.size() >= 5);
    for (const auto& d : devices)
    {
        // roll-off below the bass limit, nearly passband in the mids
        CHECK (d.responseDbAt (20.0) < d.responseDbAt (1000.0) - 6.0);
        CHECK (std::abs (d.responseDbAt (1000.0)) < 6.0);
        double lo = 0, hi = 0;
        d.usableRangeHz (translation::kRangeFloorDb, lo, hi);
        CHECK (lo >= 20.0 && lo < 1000.0 && hi > 5000.0);
    }
    const DeviceProfile& phone = devices[0];
    CHECK (phone.name == "Phone speaker");
    CHECK (phone.responseDbAt (100.0) < -20.0);
    CHECK_NEAR (phone.responseDbAt (450.0), -3.0 + phone.bumps[0].gainDb * std::exp (-0.5 * std::pow (std::log2 (450.0 / 2500.0) / 0.8, 2.0)), 0.1);
    // a bigger speaker reaches lower than a phone
    double phoneLo = 0, phoneHi = 0, monLo = 0, monHi = 0;
    phone.usableRangeHz (translation::kRangeFloorDb, phoneLo, phoneHi);
    DeviceProfile::builtIns().back().usableRangeHz (translation::kRangeFloorDb, monLo, monHi);
    CHECK (monLo < phoneLo);
    // the car profile has a bass lift
    const DeviceProfile* car = nullptr;
    for (const auto& d : devices)
        if (d.name == "Car")
            car = &d;
    CHECK (car != nullptr && car->responseDbAt (60.0) > 2.0);
}

TEST (translation_report_flags_bass_heavy_and_wide_low_end)
{
    const auto target = TargetCurve::builtIns().front();
    const DeviceProfile& phone = DeviceProfile::builtIns().front();

    // the track equals the target: no extra loss
    auto same = analyseTranslation (target.medianDb, nullptr, target, &phone);
    CHECK (same.haveDevice && !same.haveFold);
    CHECK_NEAR (same.extraLossDb, 0.0, 1.0e-4);
    CHECK (same.deviceChangeDb < -1.0); // a phone drops the energy below ~450 Hz
    CHECK (same.deviceLevel == Level::good);

    // +8 dB below 150 Hz: a phone loses much more of this one
    BandArray heavy = target.medianDb;
    for (int i = 0; i < BandGrid::kNum; ++i)
        if (BandGrid::centreHz (i) < 150.0)
            heavy[static_cast<size_t> (i)] += 8.0f;
    const auto bass = analyseTranslation (heavy, nullptr, target, &phone);
    CHECK (bass.extraLossDb > translation::kDeviceBadDb);
    CHECK (bass.deviceLevel == Level::bad);
    // ... a monitor that reaches down to 55 Hz copes much better, and earbuds with a bass lift are unaffected
    const auto onMonitors = analyseTranslation (heavy, nullptr, target, &DeviceProfile::builtIns().back());
    CHECK (onMonitors.extraLossDb < bass.extraLossDb * 0.5f);
    const auto onEarbuds = analyseTranslation (heavy, nullptr, target, &DeviceProfile::builtIns()[2]);
    CHECK (std::abs (onEarbuds.extraLossDb) < 0.5f);

    // overall mono loss of a uniform -3 dB fold, and a low end that cancels while the rest is mono
    BandArray fold {};
    fold.fill (-3.0f);
    auto wide = analyseTranslation (target.medianDb, &fold, target, nullptr);
    CHECK (wide.haveFold && !wide.haveDevice);
    CHECK_NEAR (wide.monoLossDb, 3.0, 0.01);
    CHECK_NEAR (wide.lowMonoLossDb, 3.0, 0.01);
    CHECK (wide.monoLevel == Level::warn && wide.lowMonoLevel == Level::bad);

    fold.fill (0.0f);
    for (int i = 0; i < BandGrid::kNum; ++i)
        if (BandGrid::centreHz (i) < 100.0)
            fold[static_cast<size_t> (i)] = -10.0f;
    fold[static_cast<size_t> (std::lround (BandGrid::indexForHz (60.0)))] = -20.0f;
    const auto cancel = analyseTranslation (target.medianDb, &fold, target, nullptr);
    CHECK (cancel.lowMonoLossDb > translation::kLowMonoBadDb);
    CHECK (cancel.lowMonoLevel == Level::bad);
    CHECK (cancel.monoLossDb < cancel.lowMonoLossDb);
    CHECK (cancel.worstBand == static_cast<int> (std::lround (BandGrid::indexForHz (60.0))));
    CHECK_NEAR (cancel.worstBandLossDb, 20.0, 1.0e-4);

    // single-band verdicts: bass is held to a tighter limit than the highs
    CHECK (translation::bandMonoLevel (0, 2.0f) == Level::warn);
    CHECK (translation::bandMonoLevel (40, 2.0f) == Level::good);
    CHECK (translation::bandMonoLevel (40, 7.0f) == Level::bad);

    // bands the signal does not contain (above Nyquist) are ignored
    BandArray lowRate = target.medianDb;
    for (int i = 50; i < BandGrid::kNum; ++i)
        lowRate[static_cast<size_t> (i)] = -200.0f;
    fold.fill (0.0f);
    const auto clean = analyseTranslation (lowRate, &fold, target, nullptr);
    CHECK_NEAR (clean.monoLossDb, 0.0, 1.0e-4);
    CHECK (clean.worstBand == -1);
}

int main()
{
    for (const auto& t : tests())
    {
        const int before = g_failures;
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf ("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf ("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
