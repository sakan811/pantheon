// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "Hardware.h"

#include <themis/Common.h>
#include <themis/Translation.h>

namespace ui
{
// The two words of the plugin's name. The full name ("Themis Verdict") is set once, in CMakeLists.txt (PLUGIN_NAME), and
// reaches the code as JucePlugin_Name; the smoke test checks that the two agree.
inline constexpr const char* kProjectWord = "Themis";
inline constexpr const char* kPluginWord = "Verdict";

inline juce::String signedDb (float v) { return juce::String (themis::signedDbText (v)); }

inline juce::String hzText (double hz) { return juce::String (themis::hzText (hz)); }
} // namespace ui

// How every standard component looks: push switches with lamps, knurled knobs, recessed glass selectors.
class ThemisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ThemisLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Slider::textBoxTextColourId, colours::amber);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::ComboBox::backgroundColourId, colours::glass);
        setColour (juce::ComboBox::textColourId, colours::amber);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::glass);
        setColour (juce::PopupMenu::textColourId, colours::amber);
        setColour (juce::PopupMenu::headerTextColourId, colours::dim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::amber.darker (0.55f));
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::TextButton::buttonColourId, colours::panelEdge);
        setColour (juce::TextButton::buttonOnColourId, colours::amber);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1300));
        setColour (juce::TooltipWindow::backgroundColourId, colours::glass);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, colours::amber.withAlpha (0.45f));
    }

    // ---- push switches --------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        ui::hw::drawPushButton (g, b.getLocalBounds().toFloat(), b.getToggleState(), findColour (juce::TextButton::buttonOnColourId), down, highlighted, b.isEnabled());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const auto lit = b.getToggleState();
        const auto litColour = findColour (juce::TextButton::buttonOnColourId);
        // dark print on a bright lamp, light print on a dark one
        const auto on = litColour.getPerceivedBrightness() > 0.5f ? findColour (juce::TextButton::textColourOnId) : colours::text;
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((lit ? on : findColour (juce::TextButton::textColourOffId)).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return ui::labelFont (juce::jmin (10.5f, static_cast<float> (buttonHeight) * 0.5f));
    }

    // ---- knobs ----------------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float startAngle, float endAngle, juce::Slider& s) override
    {
        const double steps = s.getInterval() > 0.0 ? (s.getMaximum() - s.getMinimum()) / s.getInterval() : 0.0;
        const int ticks = steps >= 1.0 && steps <= 11.0 ? static_cast<int> (std::lround (steps)) + 1 : 11; // a detent switch shows its positions
        ui::hw::drawKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), position, startAngle, endAngle, ticks, s.isMouseOverOrDragging());
    }

    // ---- selectors: a window of glass with the choice written in it ------------------------------
    void drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (1.0f);
        ui::hw::drawGlass (g, area, 3.0f, colours::amber);
        const auto c = juce::Point<float> (area.getRight() - 12.0f, area.getCentreY());
        juce::Path arrows;
        arrows.addTriangle (c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y - 5.0f);
        arrows.addTriangle (c.x - 3.5f, c.y + 1.5f, c.x + 3.5f, c.y + 1.5f, c.x, c.y + 5.0f);
        g.setColour (box.isMouseOver() || down ? colours::amber : colours::dim);
        g.fillPath (arrows);
        ui::hw::drawGlassOverlay (g, area, 3.0f, false);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui::lcdFont (12.0f); }
    juce::Font getPopupMenuFont() override { return ui::lcdFont (12.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};
