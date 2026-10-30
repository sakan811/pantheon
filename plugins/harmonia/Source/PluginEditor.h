// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

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
    static inline const juce::Rectangle<int> kSubtitleBounds { 22, 40, 400, 18 };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

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

    // Sets any parameter from its plain value (choice index, bool as 0/1, or float), as one host gesture.
    void setParam (const juce::String& id, float plainValue);
    void toggleNote (int pitchClass);
    void applyPreset (int index);
    void onDetectClicked();
    void setupChoice (juce::ComboBox&, std::unique_ptr<ComboAttachment>&, const char* id, const juce::String& tip);
    juce::String makeSubtitle() const;
    void setupKnob (Knob&, const char* id, const juce::String& title, const juce::String& tip, bool bipolar = false);

    HarmoniaProcessor& proc;
    HarmoniaLookAndFeel laf;
    Content content;
    juce::TooltipWindow tooltips { this, 500 };

    PitchGraph graph;
    NoteKeyboard keyboard;
    juce::ComboBox rangeBox, keyBox, scaleBox;
    juce::TextButton bypassButton { "Bypass" }, detectButton { "Detect key" }, autoKeyButton { "Auto key" };
    juce::TextButton presetButtons[3];
    juce::Label statusLabel;
    Knob knobs[6];

    std::unique_ptr<ComboAttachment> rangeAtt, keyAtt, scaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt, autoKeyAtt;

    bool keysLocked = false; // the key follows the vocal: the piano keys cannot be edited
    bool awaitingKeyResult = false;
    juce::uint32 finishClickedAt = 0;
    juce::String detectedText, subtitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmoniaEditor)
};
