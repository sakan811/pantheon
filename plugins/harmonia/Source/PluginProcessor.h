#pragma once

#include "dsp/AutoTuneEngine.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace ids
{
inline constexpr const char* key = "key";
inline constexpr const char* scale = "scale";
inline constexpr const char* speed = "speed";
inline constexpr const char* strength = "strength";
inline constexpr const char* natural = "natural";
inline constexpr const char* formant = "formant";
inline constexpr const char* hold = "hold";
inline constexpr const char* tuning = "tuning";
inline constexpr const char* range = "range";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* autoKey = "autokey";
inline juce::String note (int pitchClass) { return "note" + juce::String (pitchClass); }
} // namespace ids

class HarmoniaProcessor : public juce::AudioProcessor
{
public:
    HarmoniaProcessor();
    ~HarmoniaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The last `latency` of the vocal is still inside the engine when the input ends.
    double getTailLengthSeconds() const override { return getSampleRate() > 0.0 ? getLatencySamples() / getSampleRate() : 0.0; }

    // The host restarted or jumped: drop the audio history (done on the audio thread, in processBlock).
    void reset() override { resetRequested.store (true); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    // ---- shared with the editor ----
    // Notes the tuner is currently allowed to land on (bit 0 = C): the selected key and scale,
    // or the custom note switches for the "Custom" scale. With Auto Key on, the key being followed.
    uint16_t currentNoteMask() const;
    // The key the vocal is being followed in, when Auto Key is on and the key is known.
    bool followedKey (int& root, bool& minor) const;
    int scaleIndex() const { return (int) pScale->load(); }
    float paramValue (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    // Latest hop information for the pitch display.
    int historyWriteIndex() const { return engine->historyWriteIndex(); }
    harmonia::AutoTuneEngine::HopInfo historyAt (int index) const { return engine->historyAt (index); }
    double hopRateHz() const { return prepared.load() ? engine->hopRateHz() : 250.0; }
    bool engineReady() const { return prepared.load(); }

    // Key detection ("listen to my vocal and tell me the key").
    enum class LearnState { idle, listening };
    void startKeyLearn() { learnCommand.store (1); }
    void finishKeyLearn() { learnCommand.store (2); }
    LearnState learnState() const { return learning.load() ? LearnState::listening : LearnState::idle; }
    float learnedAmount() const { return learnAmount.load(); }

    struct KeyResult
    {
        bool valid = false;
        int root = 0;
        bool minor = false;
        float score = 0.0f;
    };
    // Returns true once when a new result has been produced.
    bool takeKeyResult (KeyResult& out);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    harmonia::EngineParams readParams() const;

    std::unique_ptr<harmonia::AutoTuneEngine> engine;
    std::atomic<bool> prepared { false };
    std::atomic<bool> resetRequested { false };
    juce::AudioParameterBool* bypassParam = nullptr;

    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pStrength = nullptr;
    std::atomic<float>* pNatural = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pTuning = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pAutoKey = nullptr;
    std::atomic<float>* pNote[12] {};

    std::atomic<int> learnCommand { 0 };
    std::atomic<bool> learning { false };
    std::atomic<float> learnAmount { 0.0f };
    std::atomic<bool> resultReady { false };
    std::atomic<int> resultRoot { 0 };
    std::atomic<bool> resultMinor { false };
    std::atomic<float> resultScore { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniaProcessor)
};
