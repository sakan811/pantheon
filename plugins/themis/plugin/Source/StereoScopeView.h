#pragma once

#include "MeterCell.h"
#include "PluginProcessor.h"
#include "ThemisLookAndFeel.h"

#include <array>
#include <functional>

// Vectorscope (goniometer) of the stereo image with a phase-correlation meter underneath.
// Vertical axis = mid, horizontal = side: mono is a vertical line, a wide mix is a fat cloud, an
// out-of-phase signal lies along the horizontal axis. The plot auto-scales to the recent peak so the shape
// stays readable at any level (click it to switch to a fixed 0 dBFS scale); correlation does not depend on level.
class StereoScopeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StereoScopeView (ThemisProcessor& p) : proc (p)
    {
        setTooltip ("Vectorscope: mid (vertical) vs side (horizontal). Click to switch between auto-scale and fixed 0 dBFS scale.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // Pull fresh samples (message thread, ~30 Hz). Repaints only while audio is flowing or the readout changed.
    void update()
    {
        const auto& scope = proc.stereoScope();
        const bool haveNow = scope.hasData();
        const float corr = scope.correlation();
        const uint64_t seq = scope.sequence();
        if (seq == lastSequence && haveNow == haveData && std::equal_to<float>() (corr, correlation)) // exact: only skips an unchanged readout
            return;

        if (seq != lastSequence)
        {
            std::array<float, kPoints> l {}, r {};
            numPoints = scope.copyRecent (l.data(), r.data(), kPoints);
            float radius = 0.0f;
            for (int i = 0; i < numPoints; ++i)
            {
                const auto s = static_cast<size_t> (i);
                themis::StereoScope::toXY (l[s], r[s], points[s].x, points[s].y);
                radius = std::max (radius, std::hypot (points[s].x, points[s].y));
            }
            peakRadius = std::max (radius, peakRadius * kPeakRelease);
        }
        lastSequence = seq;
        haveData = haveNow;
        correlation = corr;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        area.reduce (10.0f, 8.0f);

        g.setColour (colours::dim);
        g.setFont (ui::font (11.5f, juce::Font::bold));
        g.drawText ("STEREO", area.removeFromTop (14.0f).toNearestInt(), juce::Justification::left);

        const auto meter = area.removeFromBottom (40.0f);
        const float side = std::max (0.0f, std::min (area.getWidth(), area.getHeight()) - 24.0f);
        const auto square = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
        drawScope (g, square);
        drawCorrelation (g, meter);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        autoScale = !autoScale;
        repaint();
    }

private:
    static constexpr int kPoints = 1536;
    static constexpr float kPeakRelease = 0.97f;   // per update (~30 Hz): the auto scale relaxes over a few seconds
    static constexpr float kMinRadius = 1.0e-3f;   // -60 dBFS: don't blow noise up to full size
    static constexpr float kFullScaleRadius = 1.41421356f; // full-scale mono

    void drawScope (juce::Graphics& g, juce::Rectangle<float> sq) const
    {
        const auto c = sq.getCentre();
        const float radius = sq.getWidth() * 0.5f;

        g.setColour (colours::background);
        g.fillEllipse (sq);
        g.setColour (colours::panelEdge);
        g.drawEllipse (sq, 1.0f);
        g.drawEllipse (sq.reduced (radius * 0.5f), 1.0f); // half scale
        g.drawLine (c.x - radius, c.y, c.x + radius, c.y, 1.0f);
        g.drawLine (c.x, c.y - radius, c.x, c.y + radius, 1.0f);
        const float d = radius * 0.7071f;
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 1.0f);
        g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, 1.0f);

        g.setColour (colours::dim);
        g.setFont (ui::font (11.0f));
        const auto tag = [&] (const char* text, float x, float y) {
            g.drawText (text, juce::Rectangle<float> (14.0f, 12.0f).withCentre ({ x, y }).toNearestInt(), juce::Justification::centred);
        };
        tag ("M", c.x, sq.getY() - 6.0f);
        tag ("L", c.x - d - 6.0f, c.y - d - 6.0f);
        tag ("R", c.x + d + 6.0f, c.y - d - 6.0f);
        tag ("S", sq.getRight() + 7.0f, c.y);
        g.drawText (autoScale ? "AUTO" : "0 dBFS", juce::Rectangle<int> (juce::roundToInt (sq.getX()), juce::roundToInt (sq.getBottom()) - 13,
                                                                     juce::roundToInt (sq.getWidth()), 12),
                    juce::Justification::centredRight);

        if (!haveData || numPoints == 0)
            return;

        // Plot units -> pixels: auto-scale puts the recent peak at the circle's edge, fixed puts full-scale mono there.
        const float reference = autoScale ? std::max (peakRadius, kMinRadius) : kFullScaleRadius;
        const float k = radius * 0.94f / reference;
        constexpr int kBuckets = 4; // older samples are fainter
        for (int bucket = 0; bucket < kBuckets; ++bucket)
        {
            g.setColour (colours::accent.withAlpha (0.12f + 0.62f * static_cast<float> (bucket) / (kBuckets - 1)));
            const int from = numPoints * bucket / kBuckets, to = numPoints * (bucket + 1) / kBuckets;
            for (int i = from; i < to; ++i)
            {
                const auto& p = points[static_cast<size_t> (i)];
                const float px = juce::jlimit (-radius, radius, p.x * k), py = juce::jlimit (-radius, radius, p.y * k);
                g.fillRect (c.x + px - 0.8f, c.y - py - 0.8f, 1.6f, 1.6f);
            }
        }
    }

    // -1 ... +1, like a hardware phase meter: right = mono-compatible, left = phase problems.
    void drawCorrelation (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        constexpr float kWarnAnti = 0.0f, kBadAnti = 0.3f; // anti-correlation (-correlation) beyond which the verdict worsens
        const ui::MeterCell cell { area };
        cell.drawTitle (g, "CORRELATION");
        const auto bar = cell.drawTrack (g);
        g.setColour (colours::dim.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (bar.getCentreX()), bar.getY() - 1.0f, bar.getBottom() + 1.0f);

        const auto labels = cell.belowBar().toNearestInt();
        g.setFont (ui::font (11.0f));
        g.setColour (colours::dim);
        g.drawText ("-1", labels, juce::Justification::left);
        g.drawText ("+1", labels, juce::Justification::right);
        if (!haveData)
            return;

        const auto c = colours::forLevel (themis::translation::classify (-correlation, kWarnAnti, kBadAnti));
        ui::MeterCell::drawMarker (g, bar, bar.getX() + bar.getWidth() * (correlation + 1.0f) * 0.5f, c);
        g.setColour (c);
        g.drawText (juce::String (correlation, 2), labels, juce::Justification::centred);
    }

    ThemisProcessor& proc;
    std::array<juce::Point<float>, kPoints> points {};
    int numPoints = 0;
    uint64_t lastSequence = 0;
    bool haveData = false;
    bool autoScale = true;
    float correlation = 0.0f;
    float peakRadius = 0.0f;
};
