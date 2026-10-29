// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "ThemisLookAndFeel.h"

namespace ui
{
// Layout and drawing shared by every small meter in the display: a bold title row, a bar with a marker, and a
// right-aligned readout under it. Keeping the geometry here means all meters line up and change together.
struct MeterCell
{
    static constexpr float kTitleHeight = 14.0f, kBarHeight = 12.0f;
    static constexpr float kMarkerWidth = 4.0f;
    static constexpr int kColumns = 6; // cells per readout strip

    juce::Rectangle<float> area;

    // Cell `index` of a strip divided into kColumns equal columns.
    static MeterCell inStrip (juce::Rectangle<float> strip, int index)
    {
        const float w = strip.getWidth() / static_cast<float> (kColumns);
        return { juce::Rectangle<float> (strip.getX() + w * static_cast<float> (index), strip.getY(), w, strip.getHeight()).reduced (4.0f, 0.0f) };
    }

    void drawTitle (juce::Graphics& g, const juce::String& title) const
    {
        g.setFont (font (11.5f, juce::Font::bold));
        g.setColour (colours::dim);
        g.drawText (title, juce::Rectangle<float> (area).removeFromTop (kTitleHeight).toNearestInt(), juce::Justification::left);
    }

    // Draws the empty bar track and returns its bounds.
    juce::Rectangle<float> drawTrack (juce::Graphics& g) const
    {
        const auto bar = area.withTrimmedTop (kTitleHeight).withHeight (kBarHeight).reduced (0.0f, 3.0f);
        g.setColour (colours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        return bar;
    }

    // Fills the part of `bar` between two x positions (in either order) with `colour`.
    static void fillSpan (juce::Graphics& g, const juce::Rectangle<float>& bar, float x0, float x1, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRect (std::min (x0, x1), bar.getY(), std::abs (x1 - x0), bar.getHeight());
    }

    static void drawMarker (juce::Graphics& g, const juce::Rectangle<float>& bar, float x, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (x - kMarkerWidth * 0.5f, bar.getY() - 1.0f, kMarkerWidth, bar.getHeight() + 2.0f), 2.0f);
    }

    // The line below the bar.
    void drawReadout (juce::Graphics& g, const juce::String& text, juce::Colour colour,
                      juce::Justification justification = juce::Justification::right) const
    {
        g.setFont (font (11.0f));
        g.setColour (colour);
        g.drawText (text, readoutBounds(), justification);
    }

    // Title plus a free-text value, for cells without a bar.
    void drawNote (juce::Graphics& g, const juce::String& title, const juce::String& text, juce::Colour colour) const
    {
        drawTitle (g, title);
        g.setFont (font (11.5f));
        g.setColour (colour);
        g.drawText (text, juce::Rectangle<float> (area).withTrimmedTop (kTitleHeight).withHeight (26.0f).toNearestInt(), juce::Justification::left);
    }

    // What is left of the cell under the title and the bar.
    juce::Rectangle<float> belowBar() const { return area.withTrimmedTop (kTitleHeight + kBarHeight); }

    juce::Rectangle<int> readoutBounds() const
    {
        return { juce::roundToInt (area.getX()), juce::roundToInt (area.getY()) + static_cast<int> (kTitleHeight),
                 juce::roundToInt (area.getWidth()), static_cast<int> (kTitleHeight) };
    }
};
} // namespace ui
