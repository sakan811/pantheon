#pragma once

#include "PluginProcessor.h"
#include "StereoScopeView.h"
#include "ThemisLookAndFeel.h"
#include "TonalBalanceView.h"

#include <memory>

namespace ui
{
using Apvts = juce::AudioProcessorValueTreeState;

// A horizontal slider with its label on the left (used in the header).
class HSlider : public juce::Component
{
public:
    HSlider (Apvts& state, const juce::String& paramId, const juce::String& labelText)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        addAndMakeVisible (slider);
        label.setText (labelText, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, colours::dim);
        label.setFont (juce::Font (juce::FontOptions (11.5f)));
        addAndMakeVisible (label);
        attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, slider.getNormalisableRange().convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromLeft (62));
        slider.setBounds (r);
    }

private:
    juce::Slider slider;
    juce::Label label;
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

    void chooseReferenceTracks();
    void saveCurve();
    void loadCurve();

    ThemisProcessor& proc;
    ThemisLookAndFeel laf;

    TonalBalanceView view;
    StereoScopeView scopeView;

    ui::Choice targetBox, avgBox;
    juce::TextButton learnButton { "Learn from tracks..." }, saveButton { "Save curve" }, loadButton { "Load curve" },
        resetButton { "Reset" };
    juce::Label statusLabel;
    ui::HSlider toleranceSlider;
    juce::TooltipWindow tooltipWindow { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    int statusTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemisEditor)
};
