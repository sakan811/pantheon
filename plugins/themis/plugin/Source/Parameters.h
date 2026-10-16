#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <themis/TargetCurve.h>
#include <themis/TonalAnalyzer.h>

namespace params
{
inline const juce::StringArray avgTimeNames { "10 s", "30 s", "2 min", "Infinite" };
inline constexpr float avgTimeSeconds[] = { 10.0f, 30.0f, 120.0f, 0.0f };

// Target choices: every built-in curve, then "Custom" (learned from references / loaded from file).
inline juce::StringArray targetNames()
{
    juce::StringArray n;
    for (const auto& c : themis::TargetCurve::builtIns())
        n.add (c.name);
    n.add ("Custom");
    return n;
}

inline constexpr const char* target = "target";
inline constexpr const char* tolerance = "tolerance";
inline constexpr const char* avgTime = "avgTime";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Lock-free reads of the raw parameter values (safe on the audio thread).
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState& apvts);

    float value (const juce::String& id) const;
    float averagingSeconds() const;

private:
    juce::AudioProcessorValueTreeState& state;
    std::atomic<float>* avgTimeRaw = nullptr;
};
} // namespace params
