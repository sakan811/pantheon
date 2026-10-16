// Self-contained tests for the Themis DSP core (no framework dependency).
#include <themis/Compressor.h>
#include <themis/Engine.h>
#include <themis/EqFit.h>
#include <themis/Limiter.h>
#include <themis/Loudness.h>
#include <themis/StereoTools.h>
#include <themis/TargetCurve.h>
#include <themis/TonalAnalyzer.h>

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
