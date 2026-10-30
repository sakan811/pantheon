// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "Audition.h"
#include "ClipMeter.h"
#include "HistoryChecks.h"
#include "Loudness.h"
#include "LowCrestMeter.h"
#include "PhaseCancellation.h"
#include "TargetCurve.h"
#include "Translation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace themis
{
// Turns everything the plugin measures into a short list of things to do to the master, worst first: what to change, by
// how much and where, and why. A pure function on the measurements (so it is tested without a UI); it never touches the
// audio. Every number it recommends comes from a measurement; every limit it judges them by is a hand-authored rule of
// thumb (the ones in `translation::`, `history::`, `lowcrest::` and the few below), not measured data.
namespace master
{
inline constexpr double kMinSeconds = 20.0; // less audio than this and the long-term averages are not worth advice

// Programme loudness against the chosen delivery target, in LU (= dB).
inline constexpr float kLoudnessWarnLu = 2.0f, kLoudnessBadLu = 4.0f;
inline constexpr float kStreamingLufs = -14.0f; // what the big streaming services normalise to (they only turn louder masters down)

// A tonal region is worth a move when it is out of tolerance over at least this many adjacent bands (1/3 octave).
inline constexpr int kMinTonalRunBands = 2;
inline constexpr int kMaxTonalItems = 2;
inline constexpr float kMinMoveDb = 0.5f; // EQ moves are suggested in half-dB steps, never less than this
} // namespace master

// What an item is about; the order is the order of priority among items of the same severity.
enum class Topic { clipping, loudness, phase, lowEndMono, monoFold, tonal, lowCrest, balance, bass, device, swing };

struct Advice
{
    Level level = Level::warn;
    Topic topic = Topic::tonal;
    std::string headline; // what to do, with the numbers
    std::string why;      // what was measured and what it means
    Switches hear;        // the Solo / Mute setting that lets you listen to it (nothing = no suggestion)
};

struct MasterInputs
{
    double analysedSeconds = 0.0; // audio measured since the reset

    bool haveLoudness = false;
    float integratedLufs = LoudnessMeter::kSilence;
    float targetLufs = master::kStreamingLufs; // the delivery the master is for
    std::string deliveryName = "Streaming";

    ClipTotals clip; // peaks and clips since the reset

    bool haveTonal = false;
    BandArray measured {};
    TargetCurve target;
    float tolerance = 1.0f;
    double maxHz = BandGrid::kMaxHz; // bands above this are beyond the analysed signal's Nyquist

    TranslationReport translation; // device (when one is selected) and mono fold
    std::string deviceName;

    bool havePhase = false;
    PhaseReport phase;

    bool haveCrest = false;
    float lowCrestDb = 0.0f;

    std::vector<FrameStats> frames; // the history window, oldest first
};

struct MasterAdvice
{
    bool measuring = true;      // not enough audio yet for the long-term checks
    int checks = 0, passed = 0; // checks that had data, and those that found nothing to do
    std::vector<Advice> items;  // worst first
    Level level() const { return items.empty() ? Level::good : items.front().level; }
};

namespace master
{
inline std::string rounded (float db) { return format ("%.1f", static_cast<double> (std::round (db * 2.0f) / 2.0f)); }

// A move in dB rounded to the half dB an EQ is set in, but never below kMinMoveDb.
inline float eqMoveDb (float db) { return std::max (kMinMoveDb, std::round (db * 2.0f) / 2.0f); }

inline Advice make (Level level, Topic topic, std::string headline, std::string why, Switches hear = {})
{
    return { level, topic, std::move (headline), std::move (why), hear };
}

// A region of adjacent bands that sits on one side of the target, outside its tolerance.
struct TonalRun
{
    int first = 0, last = 0;
    float meanDeltaDb = 0.0f; // measured minus target, mean over the run
    float meanTolDb = 0.0f;
    float weight = 0.0f;      // how far outside the tolerance, summed over the bands: the size of the problem
};

inline std::vector<TonalRun> tonalRuns (const BandArray& measured, const TargetCurve& target, float tolerance, double maxHz)
{
    BandArray sm = measured; // the same light smoothing the curve is drawn with
    smoothBands (sm, 1);
    std::array<int, BandGrid::kNum> side {};
    std::array<float, BandGrid::kNum> delta {}, tol {};
    for (int i = 0; i < BandGrid::kNum; ++i)
    {
        const auto s = static_cast<size_t> (i);
        if (BandGrid::centreHz (i) > maxHz || measured[s] <= kNoDataDb)
            continue;
        delta[s] = sm[s] - target.medianDb[s];
        tol[s] = target.halfWidthDb[s] * tolerance;
        side[s] = std::abs (delta[s]) > tol[s] ? (delta[s] > 0.0f ? 1 : -1) : 0;
    }

    std::vector<TonalRun> runs;
    for (int i = 0; i < BandGrid::kNum;)
    {
        if (side[static_cast<size_t> (i)] == 0)
        {
            ++i;
            continue;
        }
        const int dir = side[static_cast<size_t> (i)];
        int last = i;
        for (int j = i + 1; j < BandGrid::kNum && j <= last + 2; ++j) // one band back inside the tolerance does not end a run
            if (side[static_cast<size_t> (j)] == dir)
                last = j;
        TonalRun r;
        r.first = i;
        r.last = last;
        for (int j = i; j <= last; ++j)
        {
            const auto s = static_cast<size_t> (j);
            r.meanDeltaDb += delta[s];
            r.meanTolDb += tol[s];
            r.weight += side[s] == dir ? std::abs (delta[s]) - tol[s] : 0.0f;
        }
        const float n = static_cast<float> (last - i + 1);
        r.meanDeltaDb /= n;
        r.meanTolDb /= n;
        if (last - i + 1 >= kMinTonalRunBands)
            runs.push_back (r);
        i = last + 1;
    }
    std::sort (runs.begin(), runs.end(), [] (const TonalRun& a, const TonalRun& b) { return a.weight > b.weight; });
    return runs;
}

// The range (kEnergyRanges index) that holds the middle of a run.
inline int rangeOfRun (const TonalRun& r)
{
    const int mid = (r.first + r.last) / 2;
    for (int i = 0; i < kNumEnergyRanges; ++i)
        if (kEnergyRanges[i].contains (mid))
            return i;
    return kNumEnergyRanges - 1;
}

inline Switches soloRange (int range) { return { Switches::bit (static_cast<Source> (kFirstBandSource + range)), 0u }; }
inline Switches soloSideRange (int range) { return { Switches::bit (Source::side) | soloRange (range).solo, 0u }; }
inline Switches soloSide() { return { Switches::bit (Source::side), 0u }; }
} // namespace master

// The list of things to do.
inline MasterAdvice advise (const MasterInputs& in)
{
    using master::make;
    MasterAdvice out;
    out.measuring = in.analysedSeconds < master::kMinSeconds;
    const auto check = [&] (Level level) {
        ++out.checks;
        if (level == Level::good)
            ++out.passed;
    };

    // ---- clipping and peak ceiling: exact, so it is judged as soon as there is any audio ---------
    if (in.clip.have)
    {
        const Level level = history::clip::level (in.clip.truePeakDb, in.clip.clipEvents, in.clip.overSamples);
        check (level);
        const float overDb = in.clip.truePeakDb - history::clip::kTruePeakWarnDb; // how far above the ceiling
        if (in.clip.clipEvents > 0 || in.clip.overSamples > 0)
            out.items.push_back (make (level, Topic::clipping,
                                       overDb >= 0.05f ? format ("Stop the clipping: ease the clipper or limiter drive and lower the ceiling %.1f dB, to %.1f dBTP", static_cast<double> (overDb),
                                                               static_cast<double> (history::clip::kTruePeakWarnDb))
                                                     : std::string ("Stop the clipping: ease the clipper or limiter drive until the flat tops are gone"),
                                       format ("%llu flat-topped clips and %llu samples at 0 dBFS, true peak %+.1f dBTP. Clipping adds distortion that a lossy encode makes worse.",
                                               static_cast<unsigned long long> (in.clip.clipEvents), static_cast<unsigned long long> (in.clip.overSamples),
                                               static_cast<double> (in.clip.truePeakDb))));
        else if (level != Level::good)
            out.items.push_back (make (level, Topic::clipping,
                                       format ("Lower the limiter ceiling by %.1f dB, to %.1f dBTP", static_cast<double> (overDb),
                                               static_cast<double> (history::clip::kTruePeakWarnDb)),
                                       format ("True peak is %+.1f dBTP. AAC, MP3 and Ogg encodes overshoot the peaks they are fed, so a master above %.0f dBTP can clip after conversion.",
                                               static_cast<double> (in.clip.truePeakDb), static_cast<double> (history::clip::kTruePeakWarnDb))));
    }

    // ---- loudness against the delivery target ------------------------------------------------------
    if (in.haveLoudness && !out.measuring)
    {
        const float deltaLu = in.integratedLufs - in.targetLufs; // positive: louder than the target
        const Level level = translation::classify (std::abs (deltaLu), master::kLoudnessWarnLu, master::kLoudnessBadLu);
        check (level);
        if (level != Level::good)
        {
            const float plr = in.clip.have ? in.clip.truePeakDb - in.integratedLufs : 0.0f;
            const std::string plrText = in.clip.have ? format (" Peak-to-loudness ratio %.1f dB.", static_cast<double> (plr)) : std::string();
            if (deltaLu > 0.0f)
            {
                const bool streaming = in.targetLufs <= master::kStreamingLufs + 0.5f;
                out.items.push_back (make (level, Topic::loudness,
                                           format ("Take about %s dB off the limiter's gain: %.1f LUFS against a %.0f LUFS %s target", master::rounded (deltaLu).c_str(),
                                                   static_cast<double> (in.integratedLufs), static_cast<double> (in.targetLufs), in.deliveryName.c_str()),
                                           std::string (streaming ? format ("Streaming services turn a master louder than %.0f LUFS down, so the extra %.1f dB buys no loudness there and costs punch.",
                                                                            static_cast<double> (master::kStreamingLufs), static_cast<double> (in.integratedLufs - master::kStreamingLufs))
                                                                  : format ("%.1f LU above the target.", static_cast<double> (deltaLu)))
                                               + plrText));
            }
            else
            {
                const float headroomDb = in.clip.have ? history::clip::kTruePeakWarnDb - in.clip.truePeakDb : 0.0f; // gain before the true peak reaches the ceiling
                const float raise = -deltaLu;
                std::string why = format ("%.1f LU under the target.", static_cast<double> (raise));
                if (in.clip.have)
                    why += headroomDb >= raise ? format (" The peak (%+.1f dBTP) leaves room to raise it by plain gain.", static_cast<double> (in.clip.truePeakDb))
                                               : format (" Only %.1f dB fits under %.0f dBTP as plain gain; the rest has to come from limiting.", static_cast<double> (std::max (headroomDb, 0.0f)),
                                                         static_cast<double> (history::clip::kTruePeakWarnDb));
                out.items.push_back (make (level, Topic::loudness,
                                           format ("Raise the level about %s dB: %.1f LUFS against a %.0f LUFS %s target", master::rounded (raise).c_str(),
                                                   static_cast<double> (in.integratedLufs), static_cast<double> (in.targetLufs), in.deliveryName.c_str()),
                                           why + plrText));
            }
        }
    }

    if (out.measuring)
        return out; // everything below is a long-term average

    // ---- stereo: phase, then bass, then the whole spectrum --------------------------------------
    if (in.havePhase && in.phase.have)
    {
        const auto& p = in.phase;
        check (p.level);
        if (p.level != Level::good)
        {
            const std::string where = hzText (p.loHz) + " - " + hzText (p.hiHz);
            std::string headline, why;
            switch (p.cause)
            {
                case PhaseCause::polarity:
                    headline = "Flip the polarity of the inverted channel or source (" + where + ")";
                    why = format ("The channels are in antiphase there; a mono sum loses up to %.1f dB (3 dB is what unrelated channels lose).", static_cast<double> (p.worstLossDb));
                    break;
                case PhaseCause::delay:
                    headline = format ("Remove the %.2f ms offset between the channels (%s)", static_cast<double> (p.delayMs), where.c_str());
                    why = format ("The %s channel arrives late, which is a comb filter in mono: first notch at %s, up to %.1f dB lost. Look for a stereo delay, a widener or a spaced-mic recording.",
                                  p.leftLate ? "left" : "right", hzText (static_cast<double> (p.notchHz)).c_str(), static_cast<double> (p.worstLossDb));
                    break;
                case PhaseCause::none: // (a level above good always has a cause; treated like "other")
                case PhaseCause::other:
                    headline = "Find the stereo effect that cancels " + where + " and reduce it";
                    why = format ("A mono sum loses up to %.1f dB there, and it is not a plain delay or polarity flip: suspect a stereo widener, a phase-shifting effect or an out-of-phase source. Bypass them one at a time.",
                                  static_cast<double> (p.worstLossDb));
                    break;
            }
            out.items.push_back (make (p.level, Topic::phase, headline, why, master::soloSide()));
        }
    }

    if (in.translation.haveFold)
    {
        const auto& t = in.translation;
        check (t.lowMonoLevel);
        if (t.lowMonoLevel != Level::good)
            out.items.push_back (make (t.lowMonoLevel, Topic::lowEndMono,
                                       format ("Make the low end mono: high-pass the side channel around %.0f Hz", translation::kLowEndHz),
                                       format ("A mono sum loses %.1f dB below %.0f Hz. Bass and kick should sit in the middle: club systems and phones sum the low end to mono, and out-of-phase bass cancels there.",
                                              static_cast<double> (t.lowMonoLossDb), translation::kLowEndHz),
                                       master::soloSideRange (0)));
        check (t.monoLevel);
        if (t.monoLevel != Level::good)
            out.items.push_back (make (t.monoLevel, Topic::monoFold, format ("Narrow the stereo image: a mono sum loses %.1f dB", static_cast<double> (t.monoLossDb)),
                                       format ("Reduce widening on the master or on the widest buses.%s", t.worstBand >= 0
                                                   ? format (" The worst band is %s (%.1f dB lost).", hzText (BandGrid::centreHz (t.worstBand)).c_str(), static_cast<double> (t.worstBandLossDb)).c_str()
                                                   : ""),
                                       master::soloSide()));
    }

    // ---- tonal balance --------------------------------------------------------------------------
    if (in.haveTonal)
    {
        const auto runs = master::tonalRuns (in.measured, in.target, in.tolerance, in.maxHz);
        const int lastBand = [&] {
            int b = BandGrid::kNum - 1;
            while (b > 0 && BandGrid::centreHz (b) > in.maxHz)
                --b;
            return b;
        }();
        const size_t reported = std::min (runs.size(), static_cast<size_t> (master::kMaxTonalItems));
        ++out.checks;
        if (runs.empty())
            ++out.passed;
        for (size_t i = 0; i < reported; ++i)
        {
            const auto& r = runs[i];
            const Level level = translation::toleranceLevel (r.meanDeltaDb, r.meanTolDb);
            const double lo = std::max (BandGrid::lowEdgeHz (r.first), BandGrid::kF0), hi = std::min (BandGrid::highEdgeHz (r.last), BandGrid::kMaxHz); // the edges of the displayed range
            const bool over = r.meanDeltaDb > 0.0f;
            const float insideDb = master::eqMoveDb (std::abs (r.meanDeltaDb) - r.meanTolDb);
            const char* shape = r.first == 0 ? " (low shelf)" : r.last >= lastBand ? " (high shelf)" : std::log2 (hi / lo) < 1.0 ? " (narrow bell)" : " (broad bell)";
            const std::string where = hzText (lo) + " - " + hzText (hi);
            out.items.push_back (make (level == Level::good ? Level::warn : level, Topic::tonal,
                                       format ("%s about %.1f dB at %s%s", over ? "Cut" : "Boost", static_cast<double> (insideDb), where.c_str(), shape),
                                       format ("%s is %.1f dB %s the %s target (tolerance +/-%.1f dB). That move brings it back inside the band; %.1f dB reaches the target. Levels are relative to "
                                               "the 200 Hz - 5 kHz average, so fix the biggest region first and look again.",
                                               where.c_str(), static_cast<double> (std::abs (r.meanDeltaDb)), over ? "over" : "under", in.target.name.c_str(), static_cast<double> (r.meanTolDb),
                                               static_cast<double> (std::abs (r.meanDeltaDb))),
                                       master::soloRange (master::rangeOfRun (r))));
        }
    }

    // ---- dynamics of the low end ----------------------------------------------------------------
    if (in.haveCrest)
    {
        const Level level = lowcrest::level (in.lowCrestDb);
        check (level);
        const float outside = lowcrest::outsideZoneDb (in.lowCrestDb);
        if (outside < 0.0f)
            out.items.push_back (make (level, Topic::lowCrest, format ("Open up the low end: ease the compression or clipping on the bass by about %.0f dB", static_cast<double> (std::max (1.0f, -outside))),
                                       format ("The 20 - 250 Hz crest factor is %.1f dB, %.1f dB under the typical %.0f - %.0f dB: a squashed low end sounds flat and loses punch.", static_cast<double> (in.lowCrestDb),
                                               static_cast<double> (-outside), static_cast<double> (lowcrest::kZoneLoDb), static_cast<double> (lowcrest::kZoneHiDb)),
                                       master::soloRange (0)));
        else if (outside > 0.0f)
            out.items.push_back (make (level, Topic::lowCrest, format ("Tame the low-end peaks: compress or saturate the kick and bass by about %.0f dB", static_cast<double> (std::max (1.0f, outside))),
                                       format ("The 20 - 250 Hz crest factor is %.1f dB, %.1f dB over the typical %.0f - %.0f dB: isolated low peaks make the limiter work hard and pump the whole mix.", static_cast<double> (in.lowCrestDb),
                                               static_cast<double> (outside), static_cast<double> (lowcrest::kZoneLoDb), static_cast<double> (lowcrest::kZoneHiDb)),
                                       master::soloRange (0)));
    }

    // ---- left / right balance, bass and sections, from the history window ---------------------
    const auto* frames = in.frames.data();
    const int numFrames = static_cast<int> (in.frames.size());
    if (numFrames >= history::kMinFrames)
    {
        const auto balance = history::analyseBalance (frames, numFrames, kAllRanges);
        if (balance.have)
        {
            check (balance.level);
            if (balance.level != Level::good)
            {
                const bool leftLouder = balance.leftMinusRightDb > 0.0f;
                out.items.push_back (make (balance.level, Topic::balance,
                                           format ("Re-centre the image: bring the %s channel down about %.1f dB", leftLouder ? "left" : "right", static_cast<double> (std::abs (balance.leftMinusRightDb))),
                                           format ("%s is %.1f dB louder than %s over the last stretch. Look for a panned element, a one-sided effect or an unbalanced master bus.", leftLouder ? "Left" : "Right",
                                                   static_cast<double> (std::abs (balance.leftMinusRightDb)), leftLouder ? "right" : "left")));
            }
        }

        const auto bass = history::analyseBass (frames, numFrames);
        if (bass.have)
        {
            check (bass.floorLevel);
            if (bass.floorLevel != Level::good)
                out.items.push_back (make (bass.floorLevel, Topic::bass,
                                           format ("Move the bass up or add harmonics: it sits under %.0f Hz for %.0f %% of the time", history::kSubFloorHz, static_cast<double> (bass.belowFloorShare) * 100.0),
                                           format ("Phones, laptops and small monitors cannot play under %.0f Hz, so a bass line that lives there disappears. Saturate the bass so its harmonics carry the note, "
                                                   "and high-pass the master near 30 Hz.", history::kSubFloorHz),
                                           master::soloRange (0)));
            check (bass.balanceLevel);
            if (bass.balanceLevel != Level::good)
            {
                const bool subHeavy = bass.dominant == fundamental::Balance::subHeavy;
                out.items.push_back (make (bass.balanceLevel, Topic::bass,
                                           subHeavy ? std::string ("Add upper harmonics to the bass so it carries on small speakers")
                                                    : format ("Strengthen the bass fundamental around %s", hzText (bass.medianHz).c_str()),
                                           format ("The bass fundamental is %s against its harmonics (median %s, %.0f %% of the time): %s", signedDbText (bass.medianTiltDb).c_str(),
                                                   hzText (bass.medianHz).c_str(), static_cast<double> (bass.share[static_cast<size_t> (bass.dominant)]) * 100.0,
                                                   subHeavy ? "little above it for small speakers to play."
                                                            : "the note is carried by its upper partials; the weight underneath is thin."),
                                           master::soloRange (0)));
            }
        }

        // Only the low end is held to steady section to section: the highs follow the arrangement.
        const auto low = history::summarise (frames, numFrames, [] (const FrameStats& f) { return f.energy (Signal::stereo, 0); });
        if (low.n >= history::kMinFrames)
        {
            const Level level = translation::classify (low.swingDb(), history::kSwingWarnDb, history::kSwingBadDb) == Level::good ? Level::good : Level::warn;
            check (level);
            if (level != Level::good)
                out.items.push_back (make (level, Topic::swing, format ("Even out the low end between sections: it moves %.0f dB", static_cast<double> (low.swingDb())),
                                           "The level of 20 - 120 Hz differs a lot from moment to moment. If a section is meant to lose its bass that is fine; if not, match the bass level between verse and chorus.",
                                           master::soloRange (0)));
        }
    }

    // ---- how it translates to the selected device -----------------------------------------------
    if (in.translation.haveDevice)
    {
        const auto& t = in.translation;
        check (t.deviceLevel);
        if (t.deviceLevel != Level::good)
            out.items.push_back (make (t.deviceLevel, Topic::device,
                                       format ("Make the mix hold up on %s: carry the bass with harmonics instead of sub level", in.deviceName.c_str()),
                                       format ("Through the %s the track loses %.1f dB more than the target does: it relies on frequencies this device cannot reproduce. Saturate the bass and kick so their upper partials "
                                               "carry the note, and trim what sits under the device's range.", in.deviceName.c_str(), static_cast<double> (t.extraLossDb)),
                                       master::soloRange (0)));
    }

    std::stable_sort (out.items.begin(), out.items.end(), [] (const Advice& a, const Advice& b) {
        return a.level != b.level ? a.level > b.level : a.topic < b.topic;
    });
    return out;
}
} // namespace themis
