#pragma once

#include "Parameters.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <themis/Audition.h>
#include <themis/LoudnessMonitor.h>
#include <themis/LowCrestMeter.h>
#include <themis/MasterAdvisor.h>
#include <themis/PhaseCancellation.h>
#include <themis/StereoScope.h>

#include <atomic>
#include <functional>
#include <memory>

// Audio passes through untouched (zero latency) and the plugin measures it: the long-term tonal balance against a
// reference curve, translation, phase, clipping, loudness and so on. The one exception is the Solo / Mute audition, which
// only changes the output while a button is pressed (see themis::Audition); the measurements always see the input.
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
        analyzer.requestReset(); // also restarts the clip totals and the history graphs
        lowCrest.requestReset();
        scope.requestReset();
        loudness.requestReset();
    }

    // Solo / Mute audition: what is played instead of the input (nothing soloed or muted = the input, bit for bit). Not
    // saved with the session; ignored while the host renders offline so an export can never carry it.
    void setAudition (const themis::Switches& s) { audition.setSwitches (s); }
    themis::Switches auditionSwitches() const { return audition.switches(); }

    // Everything the master plan is made of, read from the measurements (message thread).
    themis::MasterInputs masterInputs() const;

    // Thread-safe read of the long-term spectrum. Returns false until enough audio has been analysed.
    bool readSpectrum (themis::BandArray& out) const;

    // Per-band mono fold in dB (<= 0): how much of each band's power survives a sum to mono.
    // Returns false until enough audio has been analysed.
    bool readMonoFold (themis::BandArray& out) const;

    // Per-band phase relationship between the channels (see themis::analysePhase). Returns false until enough audio
    // has been analysed.
    bool readPhase (themis::PhaseBands& out) const;

    // Energy per range and bass fundamental of every analysed frame, for the time graphs.
    const themis::FrameHistory& frameHistory() const { return analyzer.history(); }

    // The playback device selected for the translation preview, or nullptr when it is "Off".
    const themis::DeviceProfile* activeDevice() const;

    // Low-end (20-250 Hz) crest factor in dB. Returns false until enough audio has been measured.
    bool readLowCrest (float& outDb) const;

    // Peaks and clipping of everything that has played since the last reset. Returns false until audio has been measured.
    bool readClip (themis::ClipTotals& out) const;

    // Stereo image: raw L/R samples for the vectorscope and the smoothed phase correlation (-1..+1).
    const themis::StereoScope& stereoScope() const { return scope; }

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
    themis::StereoScope scope;
    themis::LoudnessMonitor loudness;
    themis::Audition audition;
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
