#pragma once

#include "PluginProcessor.h"
#include "UI/Look.h"
#include "UI/SpectrumView.h"
#include <juce_gui_basics/juce_gui_basics.h>

class HephaestusEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HephaestusEditor (HephaestusProcessor&);
    ~HephaestusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    enum class Panel { saturation, bass, restore, master, count };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void addKnob (const char* paramId, const char* caption, const char* tooltip);
    void addToggle (juce::TextButton&, std::unique_ptr<ButtonAttachment>&, const char* paramId, const juce::String& text,
                    const juce::String& tooltip);
    void addChoiceBox (juce::ComboBox&, std::unique_ptr<ComboAttachment>&, const char* paramId, const juce::String& tooltip);
    void layoutPanelKnobs (Panel);
    void timerCallback() override;

    HephaestusProcessor& proc;
    hep::ui::ForgeLookAndFeel look;
    hep::ui::SpectrumView spectrum;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<std::unique_ptr<Knob>> knobs; // same order as the knob table in the .cpp

    juce::ComboBox presetBox, stereoBox, osBox, curveBox;
    std::unique_ptr<ComboAttachment> stereoAttachment, osAttachment, curveAttachment;
    juce::TextButton deltaButton, matchButton, satButton;
    std::unique_ptr<ButtonAttachment> deltaAttachment, matchAttachment, satAttachment;

    float inLevel = 0.0f, outLevel = 0.0f;
    std::array<juce::Rectangle<int>, (size_t) Panel::count> panelBounds;
    juce::Rectangle<int> meterArea;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HephaestusEditor)
};
