#include "Parameters.h"

namespace params
{
using FloatAttr = juce::AudioParameterFloatAttributes;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { target, 1 }, "Target Curve", targetNames(), kDefaultTarget));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tolerance, 1 }, "Target Tolerance",
                                                       NormalisableRange<float> (0.5f, 2.0f, 0.01f), 1.0f,
                                                       FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + "x"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { avgTime, 1 }, "Averaging", avgTimeNames(), kDefaultAvgTime));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { device, 1 }, "Translation Device", deviceNames(), kDefaultDevice));
    return layout;
}

Reader::Reader (juce::AudioProcessorValueTreeState& apvts) : state (apvts), avgTimeRaw (apvts.getRawParameterValue (avgTime))
{
    jassert (avgTimeRaw != nullptr);
}

float Reader::value (const juce::String& id) const
{
    if (auto* p = state.getRawParameterValue (id))
        return p->load();
    return 0.0f;
}

float Reader::averagingSeconds() const
{
    return avgTimes[juce::jlimit (0, kNumAvgTimes - 1, static_cast<int> (avgTimeRaw->load()))].seconds;
}
} // namespace params
