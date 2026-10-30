// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

// The look of the plugins: a piece of studio hardware with a digital brain. Brushed-metal faceplates with screws, recessed
// glass windows for everything that is a display, lamps for every verdict, switches that light up. Everything here is
// drawn from code (no image or font files ship with the plugin), and nothing is random at run time, so the screenshots in
// docs/ are reproducible.
//
// Colour roles: warm graphite metal for the chassis, cream "silkscreen" for engraved labels, amber for the digital
// read-outs (the numbers), teal phosphor for the graphs and the scope (the data), and the three lamp colours for verdicts.
namespace colours
{
inline const juce::Colour background { 0xff1d1c1a };   // chassis
inline const juce::Colour panel { 0xff2c2a27 };        // module faceplate
inline const juce::Colour panelEdge { 0xff403d38 };    // grooves, slots, switch caps
inline const juce::Colour text { 0xffeae3d0 };         // silkscreen / bright read-out
inline const juce::Colour dim { 0xff9f9680 };          // engraved secondary text
inline const juce::Colour accent { 0xff37e3c5 };       // phosphor: the target band, the scope trace
inline const juce::Colour amber { 0xffffb347 };        // digital read-outs and lit switches
inline const juce::Colour good { 0xff5fd68c };
inline const juce::Colour warn { 0xfff2a33a };
inline const juce::Colour bad { 0xffef4a3d };
inline const juce::Colour glass { 0xff090d0d };        // a display that is off
inline const juce::Colour glassGrid { 0xff1c2a29 };    // graticule lines on the glass
} // namespace colours

namespace ui
{
inline juce::Font font (float height, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (height, styleFlags));
}

// Engraved / silkscreened label type: small, bold, letter-spaced capitals.
inline juce::Font labelFont (float height = 10.5f)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold).withKerningFactor (0.07f));
}

// The type of a digital read-out.
inline juce::Font lcdFont (float height = 11.5f, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, styleFlags));
}

// The wordmark.
inline juce::Font wordmarkFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, juce::Font::bold).withKerningFactor (0.18f));
}

namespace hw
{
// A seamless tile of fine horizontal streaks, drawn over a base colour to make it look brushed.
inline const juce::Image& brushedTile()
{
    static const juce::Image tile = [] {
        constexpr int size = 256, blur = 14;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
        juce::Random rng (20240607);
        for (int y = 0; y < size; ++y)
        {
            float row[size], smooth[size];
            const float rowLevel = rng.nextFloat() * 2.0f - 1.0f;
            for (float& v : row)
                v = rng.nextFloat() * 2.0f - 1.0f;
            for (int x = 0; x < size; ++x) // circular box blur along x: streaks, and the tile wraps
            {
                float sum = 0.0f;
                for (int k = -blur; k <= blur; ++k)
                    sum += row[(x + k + size) % size];
                smooth[x] = sum / static_cast<float> (2 * blur + 1);
            }
            for (int x = 0; x < size; ++x)
            {
                const float v = juce::jlimit (-1.0f, 1.0f, smooth[x] * 3.0f + rowLevel * 0.35f);
                data.setPixelColour (x, y, v > 0.0f ? juce::Colours::white.withAlpha (v * 0.075f) : juce::Colours::black.withAlpha (-v * 0.11f));
            }
        }
        return img;
    }();
    return tile;
}

inline void fillMetal (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour base, float corner = 0.0f)
{
    juce::Path shape;
    shape.addRoundedRectangle (area, corner);
    g.setColour (base);
    g.fillPath (shape);
    g.saveState();
    g.reduceClipRegion (shape);
    g.setTiledImageFill (brushedTile(), 0, 0, 1.0f);
    g.fillRect (area);
    g.restoreState();
}

inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float radius = 3.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 1.0f, radius * 2.0f + 1.0f).withCentre (c + juce::Point<float> (0.0f, 0.7f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8c877c), c.x - radius, c.y - radius, juce::Colour (0xff3a3833), c.x + radius, c.y + radius, false));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
    // the slot, at an angle that depends on where the screw is, as no two are ever turned the same
    const float angle = std::fmod (c.x * 0.37f + c.y * 0.91f, 3.1416f);
    const juce::Point<float> d (std::cos (angle) * radius * 0.78f, std::sin (angle) * radius * 0.78f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (c.x - d.x, c.y - d.y, c.x + d.x, c.y + d.y, 1.0f);
}

// A module of the rack: brushed faceplate, bevelled edge, a screw in each corner.
inline void drawModule (juce::Graphics& g, juce::Rectangle<float> area, bool screws = true)
{
    constexpr float corner = 6.0f;
    g.setColour (juce::Colours::black.withAlpha (0.45f)); // the gap it sits in
    g.fillRoundedRectangle (area.expanded (1.0f), corner + 1.0f);
    fillMetal (g, area, colours::panel, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, area.getY(), juce::Colours::black.withAlpha (0.20f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (0.10f)); // lit top edge
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (area.reduced (1.5f), corner - 1.0f, 1.0f);
    if (screws && area.getWidth() > 90.0f && area.getHeight() > 60.0f)
        for (const float fx : { 0.0f, 1.0f })
            for (const float fy : { 0.0f, 1.0f })
                drawScrew (g, { area.getX() + 7.0f + fx * (area.getWidth() - 14.0f), area.getY() + 7.0f + fy * (area.getHeight() - 14.0f) }, 2.6f);
}

// A recessed display window, dark glass with a lit centre and a bezel. Draw the content over it, then drawGlassOverlay.
inline void drawGlass (juce::Graphics& g, juce::Rectangle<float> area, float corner = 4.0f, juce::Colour tint = colours::accent)
{
    g.setColour (juce::Colours::white.withAlpha (0.10f)); // light catching the lower lip of the bezel
    g.fillRoundedRectangle (area.expanded (1.0f).translated (0.0f, 1.0f), corner + 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.fillRoundedRectangle (area.expanded (1.0f), corner + 1.0f);
    g.setGradientFill (juce::ColourGradient (colours::glass.interpolatedWith (tint, 0.06f), area.getCentreX(), area.getCentreY(), colours::glass.darker (0.35f),
                                             area.getX(), area.getY(), true));
    g.fillRoundedRectangle (area, corner);
}

// Over the content of a window: scan lines, a vignette, and a streak of reflected light.
inline void drawGlassOverlay (juce::Graphics& g, juce::Rectangle<float> area, float corner = 4.0f, bool scanLines = true)
{
    juce::Path shape;
    shape.addRoundedRectangle (area, corner);
    g.saveState();
    g.reduceClipRegion (shape);
    if (scanLines)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        for (float y = area.getY() + 1.0f; y < area.getBottom(); y += 3.0f)
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
    }
    for (int i = 0; i < 6; ++i) // the shadow the bezel casts on the glass, deepest at the edge
    {
        const float fade = 1.0f - static_cast<float> (i) / 6.0f;
        g.setColour (juce::Colours::black.withAlpha (0.34f * fade * fade));
        g.drawRoundedRectangle (area.reduced (0.5f + static_cast<float> (i)), std::max (0.0f, corner - static_cast<float> (i)), 1.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), area.getX(), area.getY(), juce::Colours::white.withAlpha (0.0f),
                                             area.getX() + area.getWidth() * 0.45f, area.getY() + area.getHeight() * 0.55f, false));
    g.fillRect (area);
    g.restoreState();
}

// An indicator lamp. Lit it glows; unlit it is a dark lens.
inline void drawLamp (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, bool lit = true)
{
    if (lit)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.50f), c.x, c.y, colour.withAlpha (0.0f), c.x + radius * 2.8f, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (radius * 5.6f, radius * 5.6f).withCentre (c));
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 2.4f, radius * 2.0f + 2.4f).withCentre (c));
    const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    g.setGradientFill (lit ? juce::ColourGradient (colour.brighter (0.7f), c.x - radius * 0.4f, c.y - radius * 0.5f, colour.darker (0.35f), c.x + radius, c.y + radius, true)
                           : juce::ColourGradient (colour.withMultipliedBrightness (0.30f), c.x - radius * 0.4f, c.y - radius * 0.5f, colour.withMultipliedBrightness (0.10f),
                                                   c.x + radius, c.y + radius, true));
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.55f : 0.18f)); // the glint
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.5f).withCentre (c + juce::Point<float> (-radius * 0.3f, -radius * 0.45f)));
}

// A push switch: a metal cap that lights up in `litColour` while it is on.
inline void drawPushButton (juce::Graphics& g, juce::Rectangle<float> area, bool lit, juce::Colour litColour, bool down = false, bool hover = false, bool enabled = true)
{
    constexpr float corner = 3.0f;
    area = area.reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); // the well it sits in
    g.fillRoundedRectangle (area.expanded (1.0f), corner + 1.0f);
    if (down)
        area = area.translated (0.0f, 1.0f);
    juce::Colour top = lit ? litColour.brighter (0.30f) : juce::Colour (0xff55524b), bottom = lit ? litColour.darker (0.50f) : juce::Colour (0xff35322e);
    if (hover && enabled)
    {
        top = top.brighter (0.12f);
        bottom = bottom.brighter (0.12f);
    }
    if (down)
        std::swap (top, bottom);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, area.getY(), bottom, 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.03f : 0.16f));
    g.drawHorizontalLine (juce::roundToInt (area.getY() + 1.5f), area.getX() + corner, area.getRight() - corner);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (area, corner, 1.0f);
    if (lit)
    {
        g.setColour (litColour.withAlpha (0.30f));
        g.drawRoundedRectangle (area.expanded (1.5f), corner + 1.5f, 1.5f);
    }
    if (!enabled)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (area, corner);
    }
}

// A rotary knob: lit scale ticks round it (up to the current position), a knurled aluminium body and a pointer.
inline void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float position, float startAngle, float endAngle, int numTicks, bool hover = false)
{
    const auto c = area.getCentre();
    const float outer = std::min (area.getWidth(), area.getHeight()) * 0.5f - 1.0f;
    const float body = outer - 5.5f;
    const auto dir = [] (float angle) { return juce::Point<float> (std::sin (angle), -std::cos (angle)); };

    for (int i = 0; i < numTicks; ++i)
    {
        const float t = numTicks > 1 ? static_cast<float> (i) / static_cast<float> (numTicks - 1) : 0.0f;
        const auto d = dir (startAngle + t * (endAngle - startAngle));
        const bool on = t <= position + 1.0e-4f;
        g.setColour (on ? colours::amber : colours::dim.withAlpha (0.45f));
        g.drawLine (c.x + d.x * (outer - 3.5f), c.y + d.y * (outer - 3.5f), c.x + d.x * outer, c.y + d.y * outer, on ? 1.8f : 1.2f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f)); // drop shadow
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f + 2.0f, body * 2.0f + 2.0f).withCentre (c + juce::Point<float> (0.0f, 1.8f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9b968a), c.x - body, c.y - body, juce::Colour (0xff2b2925), c.x + body, c.y + body, false));
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c));
    for (int i = 0; i < 48; ++i) // knurling
    {
        const auto d = dir (static_cast<float> (i) * juce::MathConstants<float>::twoPi / 48.0f);
        g.setColour ((i % 2 == 0 ? juce::Colours::black : juce::Colours::white).withAlpha (0.20f));
        g.drawLine (c.x + d.x * body * 0.86f, c.y + d.y * body * 0.86f, c.x + d.x * body, c.y + d.y * body, 1.0f);
    }
    const float cap = body * 0.74f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff5c5850 : 0xff4d4a43), c.x - cap, c.y - cap, juce::Colour (0xff25231f), c.x + cap, c.y + cap, false));
    g.fillEllipse (juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c), 1.0f);

    const auto d = dir (startAngle + position * (endAngle - startAngle));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (c.x + d.x * cap * 0.20f, c.y + d.y * cap * 0.20f + 1.0f, c.x + d.x * cap * 0.92f, c.y + d.y * cap * 0.92f + 1.0f, 3.0f);
    g.setColour (colours::text);
    g.drawLine (c.x + d.x * cap * 0.20f, c.y + d.y * cap * 0.20f, c.x + d.x * cap * 0.92f, c.y + d.y * cap * 0.92f, 2.2f);
}

// Small silkscreened caption.
inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification j = juce::Justification::left,
                       juce::Colour colour = colours::dim, float height = 10.5f)
{
    g.setFont (labelFont (height));
    g.setColour (colour);
    g.drawText (text.toUpperCase(), area, j);
}
} // namespace hw
} // namespace ui
