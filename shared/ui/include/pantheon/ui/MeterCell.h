// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "Hardware.h"

namespace ui
{
// Layout and drawing shared by every small meter in the display: a silkscreened title on the faceplate, and under it a window
// of glass with a slot (the bar, with a lit marker) and a digital read-out. Keeping the geometry here means all meters line
// up and change together.
struct MeterCell
{
    static constexpr float kTitleHeight = 13.0f, kWindowHeight = 30.0f, kBarHeight = 8.0f;
    static constexpr float kMarkerWidth = 4.0f;
    static constexpr int kColumns = 6; // cells per readout strip
    static constexpr float kStripHeight = kTitleHeight + kWindowHeight + 10.0f; // a row of cells, with its padding

    juce::Rectangle<float> area;

    // Cell `index` of a strip divided into kColumns equal columns.
    static MeterCell inStrip (juce::Rectangle<float> strip, int index)
    {
        const float w = strip.getWidth() / static_cast<float> (kColumns);
        return { juce::Rectangle<float> (strip.getX() + w * static_cast<float> (index), strip.getY(), w, strip.getHeight()).reduced (4.0f, 0.0f) };
    }

    void drawTitle (juce::Graphics& g, const juce::String& title) const
    {
        hw::drawLabel (g, title, juce::Rectangle<float> (area).removeFromTop (kTitleHeight).toNearestInt(), juce::Justification::centredLeft, colours::dim, 10.0f);
    }

    // The glass under the title, which the bar and the readout live in.
    juce::Rectangle<float> window() const { return area.withTrimmedTop (kTitleHeight).withHeight (kWindowHeight); }

    // Draws the window and the empty slot in it, and returns the slot's bounds.
    juce::Rectangle<float> drawTrack (juce::Graphics& g) const
    {
        drawWindow (g);
        const auto bar = window().reduced (5.0f, 0.0f).withTrimmedTop (5.0f).withHeight (kBarHeight);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.expanded (0.0f, 1.0f), 2.0f);
        g.setColour (colours::dim.withAlpha (0.55f)); // scale ticks along the bottom of the slot
        for (int i = 0; i <= 4; ++i)
        {
            const float x = bar.getX() + bar.getWidth() * static_cast<float> (i) * 0.25f;
            g.drawVerticalLine (juce::roundToInt (x), bar.getBottom() + 1.0f, bar.getBottom() + 3.0f);
        }
        return bar;
    }

    // Fills the part of `bar` between two x positions (in either order) with `colour`.
    static void fillSpan (juce::Graphics& g, const juce::Rectangle<float>& bar, float x0, float x1, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRect (std::min (x0, x1), bar.getY(), std::abs (x1 - x0), bar.getHeight());
    }

    // A lit slider cap in the slot.
    static void drawMarker (juce::Graphics& g, const juce::Rectangle<float>& bar, float x, juce::Colour colour)
    {
        const auto cap = juce::Rectangle<float> (x - kMarkerWidth * 0.5f, bar.getY() - 1.5f, kMarkerWidth, bar.getHeight() + 3.0f);
        g.setColour (colour.withAlpha (0.28f));
        g.fillRoundedRectangle (cap.expanded (2.5f, 1.0f), 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (cap, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRect (cap.withWidth (1.0f).translated (1.0f, 1.0f).withTrimmedBottom (2.0f));
    }

    // The digital line under the slot.
    void drawReadout (juce::Graphics& g, const juce::String& text, juce::Colour colour,
                      juce::Justification justification = juce::Justification::right) const
    {
        g.setFont (lcdFont (11.0f));
        g.setColour (colour);
        g.drawText (text, readoutBounds(), justification);
        drawGlassOverlay (g);
    }

    // Title plus a free-text value in a window of its own, for cells without a bar.
    void drawNote (juce::Graphics& g, const juce::String& title, const juce::String& text, juce::Colour colour) const
    {
        drawTitle (g, title);
        drawWindow (g);
        g.setFont (lcdFont (11.5f));
        g.setColour (colour);
        g.drawText (text, window().reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        drawGlassOverlay (g);
    }

    // The window with nothing in it yet.
    void drawEmpty (juce::Graphics& g) const
    {
        drawWindow (g);
        drawGlassOverlay (g);
    }

    // What is left of the window under the slot.
    juce::Rectangle<float> belowBar() const { return window().withTrimmedTop (5.0f + kBarHeight); }

    juce::Rectangle<int> readoutBounds() const { return belowBar().reduced (5.0f, 0.0f).toNearestInt(); }

private:
    void drawWindow (juce::Graphics& g) const { hw::drawGlass (g, window(), 3.0f, colours::amber); }
    void drawGlassOverlay (juce::Graphics& g) const { hw::drawGlassOverlay (g, window(), 3.0f, false); }
};
} // namespace ui
