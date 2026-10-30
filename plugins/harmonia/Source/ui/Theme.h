// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
inline const juce::Colour bg        { 0xff0d0f13 };
inline const juce::Colour panel     { 0xff151921 };
inline const juce::Colour panelEdge { 0xff252b38 };
inline const juce::Colour text      { 0xffe8ecf4 };
inline const juce::Colour dim       { 0xff8892a6 };
inline const juce::Colour faint     { 0xff3a4252 };
inline const juce::Colour accent    { 0xff3de0bf }; // corrected pitch / "on"
inline const juce::Colour sung      { 0xff6fa8ff }; // the singer's own pitch
inline const juce::Colour warn      { 0xffffb454 };
inline const juce::Colour whiteKeyOff { 0xff232936 };
inline const juce::Colour blackKeyOff { 0xff12151b };
inline const juce::Colour knobTop   { 0xff2a3140 };
inline const juce::Colour knobBottom { 0xff171b24 };

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::PathStrokeType stroke (float width)
{
    return juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}
} // namespace theme

class HarmoniaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HarmoniaLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.35f));
        setColour (juce::ComboBox::backgroundColourId, theme::bg);
        setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::arrowColourId, theme::dim);
        setColour (juce::PopupMenu::backgroundColourId, theme::panel);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
        setColour (juce::TooltipWindow::backgroundColourId, theme::panel);
        setColour (juce::TooltipWindow::textColourId, theme::text);
        setColour (juce::TooltipWindow::outlineColourId, theme::panelEdge);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::bg);
    }

    juce::Font getLabelFont (juce::Label&) override { return theme::font (13.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (14.0f); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (14.0f, true); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                           float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float lineW = 5.0f;
        const float arcR = radius - lineW * 0.5f;
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::faint.withAlpha (0.7f));
        g.strokePath (track, theme::stroke (lineW));

        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.001f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (s.isEnabled() ? theme::accent : theme::dim);
            g.strokePath (val, theme::stroke (lineW));
        }

        const float bodyR = arcR - 9.0f;
        g.setGradientFill (juce::ColourGradient (theme::knobTop, c.x, c.y - bodyR,
                                                 theme::knobBottom, c.x, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (theme::panelEdge);
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -bodyR + 4.0f, 3.0f, bodyR * 0.45f, 1.5f);
        g.setColour (theme::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.isEnabled() ? theme::bg : theme::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.hasKeyboardFocus (true) ? theme::accent : theme::panelEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.isEnabled() ? theme::dim : theme::faint);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const juce::Colour onColour = (bool) b.getProperties().getWithDefault ("warn", false) ? theme::warn : theme::accent;
        juce::Colour fill = on ? onColour : theme::bg;
        if (over && ! on)
            fill = fill.brighter (0.08f);
        if (down)
            fill = fill.brighter (0.12f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? onColour : theme::panelEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? theme::bg : theme::text);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::panelEdge);
        g.drawRect (0, 0, w, h, 1);
    }
};
