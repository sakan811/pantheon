#pragma once

#include "PluginProcessor.h"
#include "ui/NoteKeyboard.h"
#include "ui/PitchGraph.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

class HarmoniaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HarmoniaEditor (HarmoniaProcessor&);
    ~HarmoniaEditor() override;

    void resized() override;

private:
    static constexpr int kDesignW = 880, kDesignH = 560;

    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override
        {
            if (onPaint)
                onPaint (g);
        }
    };

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String title;
    };

    void paintContent (juce::Graphics&);
    void timerCallback() override;

    void setChoice (const char* id, int index);
    void setFloat (const char* id, float plainValue);
    void setBool (const juce::String& id, bool value);
    void toggleNote (int pitchClass);
    void applyPreset (int index);
    void onDetectClicked();
    void setupKnob (Knob&, const char* id, const juce::String& title, const juce::String& tip, bool bipolar = false);

    HarmoniaProcessor& proc;
    HarmoniaLookAndFeel laf;
    Content content;
    juce::TooltipWindow tooltips { this, 500 };

    PitchGraph graph;
    NoteKeyboard keyboard;
    juce::ComboBox rangeBox, keyBox, scaleBox;
    juce::TextButton bypassButton { "Bypass" }, detectButton { "Detect key from my vocal" };
    juce::TextButton presetButtons[3];
    juce::Label statusLabel;
    Knob knobs[5];

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rangeAtt, keyAtt, scaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    bool awaitingKeyResult = false;
    juce::uint32 finishClickedAt = 0;
    juce::String detectedText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniaEditor)
};
