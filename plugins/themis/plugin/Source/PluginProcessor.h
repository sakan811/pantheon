#pragma once

#include "Parameters.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <themis/LowCrestMeter.h>

#include <atomic>
#include <functional>
#include <memory>

// Audio passes through untouched (zero latency); the plugin only measures the signal's long-term
// tonal balance and shows it against a reference curve.
class ThemisProcessor : public juce::AudioProcessor
{
public:
    ThemisProcessor();
    ~ThemisProcessor() override;

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Themis"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared with the editor (message thread unless noted) ----
    void resetAnalyzer()
    {
        analyzer.requestReset();
        lowCrest.requestReset();
    }

    // Thread-safe read of the long-term spectrum. Returns false until enough audio has been analysed.
    bool readSpectrum (themis::BandArray& out) const;

    // Low-end (20-250 Hz) crest factor in dB. Returns false until enough audio has been measured.
    bool readLowCrest (float& outDb) const;

    // Target curves
    themis::TargetCurve activeTarget() const; // honours the "target" parameter
    void setCustomCurve (const themis::TargetCurve& c, bool selectIt);
    bool hasCustomCurve() const;

    // Reads the given audio files in a background thread, averages their tonal balance and installs the
    // result as the "Custom" target. `onDone` is called on the message thread (error empty on success).
    void learnFromFiles (const juce::Array<juce::File>& files, std::function<void (const juce::String& error)> onDone);

    juce::AudioProcessorValueTreeState apvts;
    params::Reader paramReader;

private:
    // Runs the FFT analysis off the audio thread: processBlock() only queues samples, this drains them.
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (themis::TonalAnalyzer& a) : juce::Thread ("Themis analysis"), analyzer (a) {}
        void run() override
        {
            while (!threadShouldExit())
            {
                analyzer.pump();
                wait (15);
            }
        }

    private:
        themis::TonalAnalyzer& analyzer;
    };

    void setParameterValue (const juce::String& id, float plainValue);
    void stopAnalysis();

    themis::TonalAnalyzer analyzer;
    themis::LowCrestMeter lowCrest;
    std::unique_ptr<AnalysisThread> analysisThread; // exists between prepareToPlay and releaseResources
    std::atomic<bool> prepared { false };

    mutable juce::CriticalSection curveLock;
    themis::TargetCurve customCurve;
    bool customCurveValid = false;

    std::unique_ptr<juce::ThreadPool> pool; // created on first "Learn from tracks", so loading the plugin starts no threads
    std::atomic<bool> learning { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemisProcessor)
};
