// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "Hardware.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace ui
{
using Apvts = juce::AudioProcessorValueTreeState;

// A rotary knob bound to a parameter, with its name and a digital read-out of the value in a window of glass. A choice
// parameter turns it into a detent switch (the tick ring shows the positions). `beside` puts the knob at the left with the name
// and read-out to its right; `below` stacks name, knob and read-out for a tall cell. A knob can be bound later and re-bound to
// another parameter (a panel whose knobs change meaning).
class Knob : public juce::Component
{
public:
    enum class Layout { beside, below };

    explicit Knob (Layout layoutToUse = Layout::beside) : layout (layoutToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (140);
        addAndMakeVisible (slider);
        slider.onValueChange = [this] { repaint(); };
    }

    Knob (Apvts& state, const juce::String& paramId, const juce::String& labelText, const juce::String& tooltip, Layout layoutToUse = Layout::beside)
        : Knob (layoutToUse)
    {
        setCaption (labelText);
        setKnobTooltip (tooltip);
        bind (state, paramId);
    }

    void setCaption (const juce::String& text) { caption = text; repaint(); }
    void setKnobTooltip (const juce::String& text) { slider.setTooltip (text); }

    // (Re-)attaches the knob to a parameter; any earlier binding ends.
    void bind (Apvts& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, slider.getNormalisableRange().convertFrom0to1 (p->getDefaultValue()));
    }

    void unbind() { attachment.reset(); }

    void resized() override
    {
        auto r = getLocalBounds();
        if (layout == Layout::beside)
        {
            slider.setBounds (r.removeFromLeft (r.getHeight()));
            r.removeFromLeft (4);
            captionArea = r.removeFromTop (r.getHeight() * 2 / 5).withTrimmedTop (3);
            readout = r.reduced (0, 2);
        }
        else
        {
            captionArea = r.removeFromTop (16);
            readout = r.removeFromBottom (24).reduced (juce::jmax (0, (r.getWidth() - 84) / 2), 2);
            slider.setBounds (r.reduced (juce::jmax (0, (r.getWidth() - r.getHeight()) / 2), 2));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const bool beside = layout == Layout::beside;
        hw::drawLabel (g, caption, captionArea, beside ? juce::Justification::bottomLeft : juce::Justification::centred, colours::dim, 9.5f);
        hw::drawGlass (g, readout.toFloat(), 3.0f, colours::amber);
        g.setFont (lcdFont (12.0f));
        g.setColour (colours::amber);
        g.drawText (slider.getTextFromValue (slider.getValue()), readout.reduced (6, 0), beside ? juce::Justification::centredLeft : juce::Justification::centred);
        hw::drawGlassOverlay (g, readout.toFloat(), 3.0f, false);
    }

private:
    Layout layout;
    juce::Slider slider;
    juce::String caption;
    juce::Rectangle<int> captionArea, readout;
    std::unique_ptr<Apvts::SliderAttachment> attachment;
};

// A selector bound to a choice parameter, drawn by HardwareLookAndFeel as a window of glass.
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
