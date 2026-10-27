#pragma once

#include "DSP/Engine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace hep
{
namespace id
{
    inline constexpr const char* inGain = "inGain";
    inline constexpr const char* outGain = "outGain";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* stereo = "stereo";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* delta = "delta";
    inline constexpr const char* match = "match";

    inline constexpr const char* satOn = "satOn";
    inline constexpr const char* curve = "curve";
    inline constexpr const char* even = "even";
    inline constexpr const char* driveLow = "driveLow";
    inline constexpr const char* driveMid = "driveMid";
    inline constexpr const char* driveHigh = "driveHigh";
    inline constexpr const char* xoverLow = "xoverLow";
    inline constexpr const char* xoverHigh = "xoverHigh";

    inline constexpr const char* fundLo = "fundLo";
    inline constexpr const char* fundHi = "fundHi";
    inline constexpr const char* extAmount = "extAmount";
    inline constexpr const char* h2 = "h2";
    inline constexpr const char* h3 = "h3";
    inline constexpr const char* h4 = "h4";
    inline constexpr const char* h5 = "h5";
    inline constexpr const char* ceiling = "ceiling";
    inline constexpr const char* resAmount = "resAmount";
    inline constexpr const char* resFill = "resFill";
}

constexpr float offDb = -60.0f;         // bottom of the Amount controls; treated as off
constexpr float harmonicOffDb = -48.0f; // bottom of the H2..H5 weights; treated as off

// True when a dB control sits at its minimum (within one display step), i.e. the feature is off.
inline bool isOff (float db, float floorDb = offDb) noexcept { return db <= floorDb + 0.05f; }

// Linear gain for a dB control, or 0 when it is switched off at the bottom of its range.
inline double gainOrOff (float db, float floorDb = offDb) noexcept
{
    return isOff (db, floorDb) ? 0.0 : (double) juce::Decibels::decibelsToGain (db);
}

// Session-wide controls: presets leave these alone.
inline bool isGlobalParameter (const juce::String& paramId)
{
    using namespace id;
    for (auto* global : { inGain, outGain, mix, stereo, oversampling, delta, match })
        if (paramId == global)
            return true;
    return false;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Float = AudioParameterFloat;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    const auto dbText = [] (float floorDb) { return [floorDb] (float v, int) { return isOff (v, floorDb) ? String ("Off") : String (v, 1) + " dB"; }; };
    const auto dbValue = [] (float floorDb) { return [floorDb] (const String& s) { return s.trim().startsWithIgnoreCase ("off") ? floorDb : s.getFloatValue(); }; };
    const auto hzText = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; };
    const auto hzValue = [] (const String& s)
    {
        const auto t = s.trim();
        const float v = t.getFloatValue();
        return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
    };
    const auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };

    const auto freq = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi, 0.1f);
        r.setSkewForCentre (centre);
        return r;
    };
    const auto dbAttr = [&] (float floorDb) { return Attr().withStringFromValueFunction (dbText (floorDb)).withValueFromStringFunction (dbValue (floorDb)); };
    const auto hzAttr = [&] { return Attr().withStringFromValueFunction (hzText).withValueFromStringFunction (hzValue); };
    const auto pctAttr = [&] { return Attr().withStringFromValueFunction (pctText); };
    const auto gainAttr = [] { return Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }); };

    const auto amountAttr = [&] { return dbAttr (offDb); };
    const auto harmonicAttr = [&] { return dbAttr (harmonicOffDb); };

    const auto F = [&] (const char* pid, const char* name, NormalisableRange<float> r, float def, Attr a)
    {
        p.push_back (std::make_unique<Float> (ParameterID { pid, 1 }, name, r, def, a));
    };
    const auto Choice = [&] (const char* pid, const char* name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid, 1 }, name, items, def));
    };
    const auto Bool = [&] (const char* pid, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid, 1 }, name, def));
    };

    // Global
    F (id::inGain,  "Input Gain",  { -12.0f, 12.0f, 0.1f }, 0.0f, gainAttr());
    F (id::outGain, "Output Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, gainAttr());
    F (id::mix,     "Mix",         { 0.0f, 100.0f, 1.0f }, 100.0f, pctAttr());
    Choice (id::stereo, "Stereo Mode", { "Stereo (L/R)", "Mid/Side" }, 0);
    Choice (id::oversampling, "Oversampling", { "Off", "2x", "4x", "8x" }, 2);
    Bool (id::delta, "Delta", false);
    Bool (id::match, "Gain Match", false);

    // Saturator
    Bool (id::satOn, "Saturation", true);
    Choice (id::curve, "Curve", { "Tanh", "Atan", "Soft" }, 0);
    F (id::even, "Even Harmonics", { 0.0f, 100.0f, 1.0f }, 25.0f, pctAttr());
    F (id::driveLow,  "Drive Low",  { 0.0f, 24.0f, 0.1f }, 3.0f, gainAttr());
    F (id::driveMid,  "Drive Mid",  { 0.0f, 24.0f, 0.1f }, 3.0f, gainAttr());
    F (id::driveHigh, "Drive High", { 0.0f, 24.0f, 0.1f }, 3.0f, gainAttr());
    F (id::xoverLow,  "Crossover Low",  freq (60.0f, 500.0f, 150.0f), 150.0f, hzAttr());
    F (id::xoverHigh, "Crossover High", freq (1500.0f, 12000.0f, 3500.0f), 3500.0f, hzAttr());

    // Fundamental engine
    F (id::fundLo, "Bass Range Low",  freq (25.0f, 120.0f, 50.0f), 40.0f, hzAttr());
    F (id::fundHi, "Bass Range High", freq (60.0f, 250.0f, 120.0f), 120.0f, hzAttr());
    F (id::extAmount, "Harmonic Bass", { offDb, 6.0f, 0.1f }, offDb, amountAttr());
    F (id::h2, "H2", { harmonicOffDb, 0.0f, 0.1f }, -6.0f, harmonicAttr());
    F (id::h3, "H3", { harmonicOffDb, 0.0f, 0.1f }, -9.0f, harmonicAttr());
    F (id::h4, "H4", { harmonicOffDb, 0.0f, 0.1f }, -15.0f, harmonicAttr());
    F (id::h5, "H5", { harmonicOffDb, 0.0f, 0.1f }, -22.0f, harmonicAttr());
    F (id::ceiling, "Harmonic Ceiling", freq (300.0f, 3000.0f, 1000.0f), 1200.0f, hzAttr());
    F (id::resAmount, "Fundamental Restore", { offDb, 6.0f, 0.1f }, offDb, amountAttr());
    F (id::resFill, "Fill Gaps Only", { 0.0f, 100.0f, 1.0f }, 70.0f, pctAttr());

    return { p.begin(), p.end() };
}

// Reads the current parameter values into the plain struct the engine consumes.
inline EngineParams readParams (juce::AudioProcessorValueTreeState& s)
{
    const auto v = [&] (const char* pid) { return s.getRawParameterValue (pid)->load(); };
    const auto gain = [] (float db) { return (double) juce::Decibels::decibelsToGain (db); };

    EngineParams e;
    e.inGainDb = v (id::inGain);
    e.outGainDb = v (id::outGain);
    e.mix = v (id::mix) * 0.01f;
    e.midSide = (int) v (id::stereo) == 1;
    e.osMode = (int) v (id::oversampling);
    e.delta = v (id::delta) > 0.5f;
    e.match = v (id::match) > 0.5f;
    e.satOn = v (id::satOn) > 0.5f;

    e.sat.curve = (int) v (id::curve);
    e.sat.bias = 0.8 * v (id::even) * 0.01;
    e.sat.drive[0] = gain (v (id::driveLow));
    e.sat.drive[1] = gain (v (id::driveMid));
    e.sat.drive[2] = gain (v (id::driveHigh));
    e.sat.xoverLow = v (id::xoverLow);
    e.sat.xoverHigh = v (id::xoverHigh);

    e.fund.lo = v (id::fundLo);
    e.fund.hi = v (id::fundHi); // the engine applies effectiveRangeHigh()
    e.fund.extGain = gainOrOff (v (id::extAmount));
    int h = 0;
    for (auto* pid : { id::h2, id::h3, id::h4, id::h5 })
        e.fund.harmonic[h++] = gainOrOff (v (pid), harmonicOffDb);
    e.fund.ceiling = v (id::ceiling);
    e.fund.resGain = gainOrOff (v (id::resAmount));
    e.fund.fill = v (id::resFill) * 0.01;
    return e;
}

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // everything not listed falls back to default
};

inline const std::vector<Preset>& presets()
{
    using namespace id;
    static const std::vector<Preset> list
    {
        { "Init", {} },
        { "Master Glue",         { { curve, 0 }, { even, 20 }, { driveLow, 3 }, { driveMid, 3 }, { driveHigh, 2 } } },
        { "Warm Tube",           { { curve, 0 }, { even, 65 }, { driveLow, 5 }, { driveMid, 5 }, { driveHigh, 3 } } },
        { "Tape Body",           { { curve, 1 }, { even, 10 }, { driveLow, 7 }, { driveMid, 4 }, { driveHigh, 2 },
                                   { extAmount, -12 }, { fundLo, 40 }, { fundHi, 100 }, { h2, -6 }, { h3, -12 } } },
        { "Air Exciter",         { { curve, 2 }, { even, 35 }, { driveLow, 0 }, { driveMid, 2 }, { driveHigh, 10 } } },
        { "Missing Fundamental", { { driveLow, 1 }, { driveMid, 2 }, { driveHigh, 1 }, { even, 20 },
                                   { extAmount, -6 }, { fundLo, 35 }, { fundHi, 110 },
                                   { resAmount, -9 }, { resFill, 80 } } },
        { "Small-Speaker Bass",  { { driveLow, 2 }, { driveMid, 2 }, { driveHigh, 1 },
                                   { extAmount, 0 }, { fundLo, 50 }, { fundHi, 160 }, { ceiling, 1500 },
                                   { h2, -4 }, { h3, -6 }, { h4, -12 }, { h5, -18 } } },
        { "808 Sub Fill",        { { driveLow, 4 }, { driveMid, 1 }, { driveHigh, 0 },
                                   { extAmount, -8 }, { fundLo, 30 }, { fundHi, 90 }, { ceiling, 800 },
                                   { h2, -3 }, { h3, -10 }, { h4, -48 }, { h5, -48 } } },
    };
    return list;
}

// Sets a parameter from the UI/host thread the way a user gesture would, so hosts record automation.
inline void setParameter (juce::RangedAudioParameter& param, float normalised)
{
    param.beginChangeGesture();
    param.setValueNotifyingHost (normalised);
    param.endChangeGesture();
}

inline void applyPreset (juce::AudioProcessorValueTreeState& s, int index)
{
    const auto& list = presets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    // Start from defaults so presets don't inherit leftovers from the previous one.
    for (auto* param : s.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! isGlobalParameter (ranged->paramID))
                setParameter (*ranged, ranged->getDefaultValue());

    for (auto& [pid, value] : list[(size_t) index].values)
        if (auto* ranged = s.getParameter (pid))
            setParameter (*ranged, ranged->convertTo0to1 (value));
}
}
