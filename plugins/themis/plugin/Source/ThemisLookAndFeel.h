#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace colours
{
inline const juce::Colour background { 0xff12141a };
inline const juce::Colour panel { 0xff1b1e26 };
inline const juce::Colour panelEdge { 0xff2a2f3a };
inline const juce::Colour text { 0xffe5e7eb };
inline const juce::Colour dim { 0xff8b93a1 };
inline const juce::Colour accent { 0xff2dd4bf };
inline const juce::Colour good { 0xff4ade80 };
inline const juce::Colour warn { 0xfff59e0b };
inline const juce::Colour bad { 0xffef4444 };
inline const juce::Colour eqCurve { 0xff93c5fd };
} // namespace colours

class ThemisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ThemisLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::ComboBox::backgroundColourId, colours::panelEdge);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.darker (0.4f));
        setColour (juce::TextButton::buttonColourId, colours::panelEdge);
        setColour (juce::TextButton::buttonOnColourId, colours::accent.darker (0.25f));
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float thickness = juce::jmax (2.5f, radius * 0.16f);
        const float arcRadius = radius - thickness * 0.5f;

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::panelEdge);
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // bipolar parameters (range straddles zero) fill from the centre
        const auto& range = slider.getNormalisableRange();
        const bool bipolar = range.start < 0.0 && range.end > 0.0;
        const float zeroPos = bipolar ? static_cast<float> (range.convertTo0to1 (0.0)) : 0.0f;
        const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, bipolar ? zeroAngle : startAngle, angle, true);
        g.setColour (slider.isEnabled() ? colours::accent : colours::dim);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        const float len = radius * 0.5f;
        pointer.addRoundedRectangle (-1.2f, -radius + thickness + 1.0f, 2.4f, len, 1.0f);
        g.setColour (colours::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : findColour (juce::TextButton::buttonColourId);
        if (highlighted)
            c = c.brighter (0.12f);
        if (down)
            c = c.brighter (0.2f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, static_cast<float> (buttonHeight) * 0.6f)));
    }
};
