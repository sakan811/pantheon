#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace harmonia;

namespace
{
juce::String speedText (float v)
{
    return v < 1.0f ? juce::String ("Instant") : juce::String (juce::roundToInt (v)) + " ms";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout HarmoniaProcessor::createLayout()
{
    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;
    using juce::StringArray;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray keys;
    for (auto* n : kNoteNames)
        keys.add (n);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, "Key", keys, 0));

    StringArray scales;
    for (auto& s : kScales)
        scales.add (s.name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale", scales, 0));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::note (i), 1 },
                                                          juce::String ("Custom ") + kNoteNames[i], true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::speed, 1 }, "Retune Speed", NormalisableRange<float> (0.0f, 400.0f, 1.0f, 0.45f), 35.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return speedText (v); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::strength, 1 }, "Strength", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::natural, 1 }, "Natural", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::formant, 1 }, "Formant", NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::tuning, 1 }, "Tuning (A4)", NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::range, 1 }, "Voice",
        StringArray { "Auto", "Low (bass / baritone)", "Mid (tenor / alto)", "High (soprano)" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    return layout;
}

HarmoniaProcessor::HarmoniaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      engine (std::make_unique<AutoTuneEngine>())
{
    pKey = apvts.getRawParameterValue (ids::key);
    pScale = apvts.getRawParameterValue (ids::scale);
    pSpeed = apvts.getRawParameterValue (ids::speed);
    pStrength = apvts.getRawParameterValue (ids::strength);
    pNatural = apvts.getRawParameterValue (ids::natural);
    pFormant = apvts.getRawParameterValue (ids::formant);
    pTuning = apvts.getRawParameterValue (ids::tuning);
    pRange = apvts.getRawParameterValue (ids::range);
    pBypass = apvts.getRawParameterValue (ids::bypass);
    for (int i = 0; i < 12; ++i)
        pNote[i] = apvts.getRawParameterValue (ids::note (i));

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ids::bypass));
}

bool HarmoniaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

uint16_t HarmoniaProcessor::computeMask (int key, int scale) const
{
    if (scale == kCustomScale)
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (pNote[i]->load() >= 0.5f)
                m = (uint16_t) (m | (1u << i));
        return m;
    }
    return scaleMask (key, juce::jlimit (0, kNumScales - 1, scale));
}

void HarmoniaProcessor::prepareToPlay (double sampleRate, int)
{
    const int channels = juce::jlimit (1, 2, getTotalNumInputChannels());
    prepared.store (false);
    engine->prepare (sampleRate, channels);
    setLatencySamples (engine->latencySamples());
    prepared.store (true);
}

void HarmoniaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (! prepared.load())
        return;

    const int key = (int) pKey->load();
    const int scale = (int) pScale->load();
    const uint16_t mask = computeMask (key, scale);

    EngineParams p;
    p.corrector.noteMask = mask;
    p.corrector.retuneMs = pSpeed->load();
    p.corrector.strength = pStrength->load() * 0.01f;
    p.corrector.natural = pNatural->load() * 0.01f;
    p.corrector.refA4 = pTuning->load();
    p.formantSemitones = pFormant->load();
    p.mix = pBypass->load() >= 0.5f ? 0.0f : 1.0f;

    switch ((int) pRange->load())
    {
        case 1: p.minHz = 75.0f;  p.maxHz = 400.0f;  break; // low
        case 2: p.minHz = 100.0f; p.maxHz = 700.0f;  break; // mid
        case 3: p.minHz = 180.0f; p.maxHz = 1200.0f; break; // high
        default: p.minHz = 75.0f; p.maxHz = 1000.0f; break; // auto
    }
    engine->setParams (p);

    // Key detection requests from the UI.
    switch (learnCommand.exchange (0))
    {
        case 1:
            engine->startLearning();
            learning.store (true);
            break;
        case 2:
        {
            const auto guess = engine->stopLearning();
            learning.store (false);
            if (engine->learnedAmount() >= 150.0f) // roughly >1 s of confidently voiced singing
            {
                resultRoot.store (guess.root);
                resultMinor.store (guess.minor);
                resultScore.store (guess.score);
                resultReady.store (true);
            }
            break;
        }
        default: break;
    }
    if (learning.load())
        learnAmount.store (engine->learnedAmount());

    engine->process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), buffer.getNumSamples());
}

bool HarmoniaProcessor::takeKeyResult (KeyResult& out)
{
    if (! resultReady.exchange (false))
        return false;
    out.valid = true;
    out.root = resultRoot.load();
    out.minor = resultMinor.load();
    out.score = resultScore.load();
    return true;
}

juce::AudioProcessorEditor* HarmoniaProcessor::createEditor()
{
    return new HarmoniaEditor (*this);
}

void HarmoniaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmoniaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmoniaProcessor();
}
