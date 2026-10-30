// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "AdvicePanel.h"
#include "AuditionPanel.h"
#include "HistoryViews.h"
#include "PluginProcessor.h"
#include "StereoScopeView.h"
#include "ThemisLookAndFeel.h"
#include "TonalBalanceView.h"

#include <functional>
#include <memory>

namespace ui
{
using Apvts = juce::AudioProcessorValueTreeState;

// A rotary knob bound to a parameter, with its name above a digital read-out of the value. A choice parameter turns it into a
// detent switch (the tick ring shows the positions).
class Knob : public juce::Component
{
public:
    Knob (Apvts& state, const juce::String& paramId, const juce::String& labelText, const juce::String& tooltip) : caption (labelText)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (140);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
        attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, slider.getNormalisableRange().convertFrom0to1 (p->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        slider.setBounds (r.removeFromLeft (r.getHeight()));
        r.removeFromLeft (4);
        captionArea = r.removeFromTop (r.getHeight() * 2 / 5).withTrimmedTop (3);
        readout = r.reduced (0, 2);
    }

    void paint (juce::Graphics& g) override
    {
        hw::drawLabel (g, caption, captionArea, juce::Justification::bottomLeft, colours::dim, 9.5f);
        hw::drawGlass (g, readout.toFloat(), 3.0f, colours::amber);
        g.setFont (lcdFont (12.0f));
        g.setColour (colours::amber);
        g.drawText (slider.getTextFromValue (slider.getValue()), readout.reduced (6, 0), juce::Justification::centredLeft);
        hw::drawGlassOverlay (g, readout.toFloat(), 3.0f, false);
    }

private:
    juce::Slider slider;
    juce::String caption;
    juce::Rectangle<int> captionArea, readout;
    std::unique_ptr<Apvts::SliderAttachment> attachment;
};

class Choice : public juce::ComboBox
{
public:
    Choice (Apvts& state, const juce::String& paramId, const juce::StringArray& items)
    {
        addItemList (items, 1);
        attachment = std::make_unique<Apvts::ComboBoxAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<Apvts::ComboBoxAttachment> attachment;
};
} // namespace ui

class ThemisEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThemisEditor (ThemisProcessor&);
    ~ThemisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setStatus (const juce::String& text, bool isError);

    void launchChooser (const juce::String& title, const juce::File& start, const juce::String& patterns, int flags,
                        std::function<void (ThemisEditor&, const juce::FileChooser&)> onChosen);
    void chooseReferenceTracks();
    void saveCurve();
    void loadCurve();

    static constexpr const char* kCurveExtension = ".themiscurve";
    static constexpr int kHistoryHeight = 240; // the two time graphs along the bottom
    static constexpr int kHeaderHeight = 84;   // the control panel along the top
    static constexpr int kTitleWidth = 196;    // the nameplate; the header rows start after it
    static constexpr int kSideWidth = 450;  // the right-hand column: master plan on top, stereo scope and audition below
    static constexpr int kScopeHeight = 228, kAuditionWidth = 226; // the stereo scope and the audition strip along the bottom of that column
    static constexpr int kPlanRefreshTicks = 15; // the plan is re-read twice a second

    ThemisProcessor& proc;
    ThemisLookAndFeel laf;

    TonalBalanceView view;
    StereoScopeView scopeView;
    ui::LevelView levelView;
    ui::FundamentalView fundamentalView;
    AdvicePanel advicePanel;
    AuditionPanel auditionPanel;

    ui::Choice targetBox, deviceBox, loudnessBox;
    juce::Label deviceLabel, loudnessLabel;
    juce::TextButton learnButton { "Learn from tracks..." }, saveButton { "Save curve" }, loadButton { "Load curve" },
        resetButton { "Reset" };
    juce::Label statusLabel;
    ui::Knob avgKnob, toleranceKnob;
    juce::TooltipWindow tooltipWindow { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> nameplate, statusWindow;
    int statusTicks = 0, planTicks = 0;
    bool signalPresent = false; // the SIGNAL lamp: audio is reaching the plugin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemisEditor)
};
