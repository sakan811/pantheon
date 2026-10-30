// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "../DSP/Band.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace hep::ui
{
namespace colour
{
    const juce::Colour background { 0xff0e0c0b };
    const juce::Colour panel      { 0xff191513 };
    const juce::Colour panelEdge  { 0xff2d251f };
    const juce::Colour ember      { 0xffff6a1f };
    const juce::Colour glow       { 0xffffb04a };
    const juce::Colour copper     { 0xffc87941 };
    const juce::Colour text       { 0xffeadfd2 };
    const juce::Colour dim        { 0xff8c7d6e };
    const juce::Colour trace      { 0xff6d7b8a };
    const juce::Colour track      { 0xff2a221c };
    const juce::Colour fillMode   { 0xff3fc1c9 }; // harmonic fill bands; saturate bands are ember

    inline juce::Colour forMode (hep::BandMode mode) { return mode == hep::BandMode::fill ? fillMode : ember; }
}

class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel()
    {
        using namespace colour;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonOnColourId, ember);
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::outlineColourId, panelEdge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, ember);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ember.darker (0.3f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace colour;
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcRadius = radius - 3.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = slider.isEnabled();

        // Track.
        juce::Path trackPath;
        trackPath.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (colour::track);
        g.strokePath (trackPath, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc, with a soft glow underneath. Bipolar sliders fill from the centre.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 && slider.getMinimum() >= -13.0;
        const float zero = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        const float fromAngle = startAngle + zero * (endAngle - startAngle);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (fromAngle, angle),
                             std::max (fromAngle, angle), true);
        if (enabled)
        {
            g.setColour (ember.withAlpha (0.18f));
            g.strokePath (value, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (enabled ? ember : dim);
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body.
        const float bodyR = arcRadius - 7.0f;
        juce::ColourGradient body (juce::Colour (0xff3a302a), centre.x, centre.y - bodyR,
                                   juce::Colour (0xff17120f), centre.x, centre.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (panelEdge.brighter (0.2f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer.
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -bodyR + 3.0f, 3.0f, bodyR * 0.55f, 1.5f);
        g.setColour (enabled ? glow : dim);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (12.0f));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, colour::text);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return l;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        using namespace colour;
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        // Buttons that carry a mode colour (the band mode switch) light up in it; the rest in ember.
        const auto lit = b.findColour (juce::TextButton::buttonOnColourId, false);
        auto fill = on ? lit.withMultipliedBrightness (hover ? 1.1f : 0.95f) : panel.brighter (hover ? 0.15f : 0.05f);
        if (down) fill = fill.darker (0.2f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? lit.brighter (0.3f).withAlpha (0.7f) : panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour (b.getToggleState() ? juce::Colour (0xff1a0d05) : colour::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace colour;
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (panel.brighter (0.05f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? ember : panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (ember);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};
}
