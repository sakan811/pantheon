#pragma once

#include "PluginProcessor.h"
#include "ThemisLookAndFeel.h"

// The reference-curve display: long-term spectrum of the signal against the target band.
class TonalBalanceView : public juce::Component
{
public:
    explicit TonalBalanceView (ThemisProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    // Pull fresh data from the processor (message thread, ~30 Hz).
    void update()
    {
        haveData = proc.readSpectrum (measured);
        target = proc.activeTarget();
        tolerance = proc.paramReader.value (params::tolerance);
        fs = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);

        auto strip = area.removeFromBottom (44.0f).reduced (8.0f, 4.0f);
        plot = area.reduced (40.0f, 14.0f).withTrimmedBottom (14.0f);

        drawGrid (g);
        drawTarget (g);
        if (haveData)
            drawMeasured (g);
        else
        {
            g.setColour (colours::dim);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText ("Play your track - the curve builds up as audio flows through", plot.toNearestInt(),
                        juce::Justification::centred);
        }
        drawBroadband (g, strip);
        drawHover (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverBand = plot.contains (e.position) ? bandForX (e.position.x) : -1;
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hoverBand = -1;
        repaint();
    }

private:
    static constexpr float kTopDb = 12.0f, kBottomDb = -30.0f;
    static constexpr int kBandCount = themis::BandGrid::kNum;

    float xFor (double hz) const
    {
        return plot.getX() + plot.getWidth() * static_cast<float> (std::log (hz / 20.0) / std::log (1000.0));
    }
    float yFor (float db) const
    {
        return plot.getY() + plot.getHeight() * (kTopDb - juce::jlimit (kBottomDb - 6.0f, kTopDb + 6.0f, db)) / (kTopDb - kBottomDb);
    }
    int bandForX (float x) const
    {
        const double hz = 20.0 * std::pow (1000.0, (x - plot.getX()) / plot.getWidth());
        return juce::jlimit (0, kBandCount - 1, static_cast<int> (std::lround (themis::BandGrid::indexForHz (hz))));
    }
    int lastDrawableBand() const
    {
        int last = kBandCount - 1;
        while (last > 0 && themis::BandGrid::centreHz (last) > fs * 0.47)
            --last;
        return last;
    }

    void drawGrid (juce::Graphics& g) const
    {
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xFor (hz);
            g.setColour (colours::panelEdge);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (colours::dim);
            const juce::String label = hz >= 1000.0 ? juce::String (static_cast<int> (hz / 1000.0)) + "k" : juce::String (static_cast<int> (hz));
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 13.0f).toNearestInt(),
                        juce::Justification::centred);
        }
        for (float db = kTopDb; db >= kBottomDb - 0.1f; db -= 6.0f)
        {
            const float y = yFor (db);
            g.setColour (std::abs (db) < 0.01f ? colours::dim.withAlpha (0.5f) : colours::panelEdge);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (colours::dim);
            g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (2.0f, y - 7.0f, 34.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
    }

    void drawTarget (juce::Graphics& g) const
    {
        const int last = lastDrawableBand();
        juce::Path band, median;
        for (int i = 0; i <= last; ++i)
        {
            const auto s = static_cast<size_t> (i);
            const float x = xFor (themis::BandGrid::centreHz (i));
            const float y = yFor (target.medianDb[s] + target.halfWidthDb[s] * tolerance);
            i == 0 ? band.startNewSubPath (x, y) : band.lineTo (x, y);
        }
        for (int i = last; i >= 0; --i)
        {
            const auto s = static_cast<size_t> (i);
            band.lineTo (xFor (themis::BandGrid::centreHz (i)), yFor (target.medianDb[s] - target.halfWidthDb[s] * tolerance));
        }
        band.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.16f));
        g.fillPath (band);
        g.setColour (colours::accent.withAlpha (0.45f));
        g.strokePath (band, juce::PathStrokeType (1.0f));

        for (int i = 0; i <= last; ++i)
        {
            const float x = xFor (themis::BandGrid::centreHz (i)), y = yFor (target.medianDb[static_cast<size_t> (i)]);
            i == 0 ? median.startNewSubPath (x, y) : median.lineTo (x, y);
        }
        g.setColour (colours::accent.withAlpha (0.9f));
        g.strokePath (median, juce::PathStrokeType (1.5f));

        g.setColour (colours::accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Target: " + juce::String (target.name), juce::Rectangle<int> (static_cast<int> (plot.getX()) + 6, static_cast<int> (plot.getY()) + 2, 320, 16),
                    juce::Justification::left);
    }

    void drawMeasured (juce::Graphics& g) const
    {
        const int last = lastDrawableBand();
        // light display smoothing (3-band moving average)
        std::array<float, kBandCount> sm {};
        for (int i = 0; i < kBandCount; ++i)
        {
            float s = 0;
            int n = 0;
            for (int k = -1; k <= 1; ++k)
                if (i + k >= 0 && i + k < kBandCount)
                {
                    s += measured[static_cast<size_t> (i + k)];
                    ++n;
                }
            sm[static_cast<size_t> (i)] = s / static_cast<float> (n);
        }
        for (int i = 0; i < last; ++i)
        {
            const auto s = static_cast<size_t> (i);
            const float delta = sm[s] - target.medianDb[s];
            const float tol = target.halfWidthDb[s] * tolerance;
            const auto c = std::abs (delta) <= tol ? colours::good : std::abs (delta) <= tol * 2.0f ? colours::warn : colours::bad;
            g.setColour (c);
            g.drawLine (xFor (themis::BandGrid::centreHz (i)), yFor (sm[s]), xFor (themis::BandGrid::centreHz (i + 1)),
                        yFor (sm[s + 1]), 2.2f);
        }
    }

    void drawBroadband (juce::Graphics& g, juce::Rectangle<float> strip) const
    {
        struct Range { const char* name; double lo, hi; };
        static constexpr Range ranges[4] = { { "LOW", 20, 120 }, { "LOW-MID", 120, 1000 }, { "HIGH-MID", 1000, 6000 }, { "HIGH", 6000, 20000 } };
        const float cellW = strip.getWidth() / 4.0f;
        for (int r = 0; r < 4; ++r)
        {
            auto cell = juce::Rectangle<float> (strip.getX() + cellW * static_cast<float> (r), strip.getY(), cellW, strip.getHeight()).reduced (4.0f, 0.0f);
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            g.setColour (colours::dim);
            g.drawText (ranges[r].name, cell.removeFromTop (14.0f).toNearestInt(), juce::Justification::left);

            double delta = 0, tol = 0;
            int n = 0;
            for (int i = 0; i <= lastDrawableBand(); ++i)
            {
                const double f = themis::BandGrid::centreHz (i);
                if (f >= ranges[r].lo && f < ranges[r].hi)
                {
                    const auto s = static_cast<size_t> (i);
                    delta += measured[s] - target.medianDb[s];
                    tol += target.halfWidthDb[s] * tolerance;
                    ++n;
                }
            }
            if (n == 0 || !haveData)
                continue;
            delta /= n;
            tol /= n;

            const auto c = std::abs (delta) <= tol ? colours::good : std::abs (delta) <= 2.0 * tol ? colours::warn : colours::bad;
            auto bar = cell.removeFromTop (12.0f).reduced (0.0f, 3.0f);
            g.setColour (colours::panelEdge);
            g.fillRoundedRectangle (bar, 2.0f);
            const float centre = bar.getCentreX();
            const float half = bar.getWidth() * 0.5f;
            const float pos = static_cast<float> (juce::jlimit (-1.0, 1.0, delta / (tol * 3.0))) * half;
            g.setColour (colours::accent.withAlpha (0.35f)); // in-tolerance zone
            g.fillRect (centre - half / 3.0f, bar.getY(), half * 2.0f / 3.0f, bar.getHeight());
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (centre + pos - 2.0f, bar.getY() - 1.0f, 4.0f, bar.getHeight() + 2.0f), 2.0f);

            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.setColour (c);
            g.drawText ((delta >= 0 ? "+" : "") + juce::String (delta, 1) + " dB",
                        juce::Rectangle<int> (juce::roundToInt (cell.getX()), juce::roundToInt (strip.getY()) + 14,
                                              juce::roundToInt (cell.getWidth()), 14),
                        juce::Justification::right);
        }
    }

    void drawHover (juce::Graphics& g) const
    {
        if (hoverBand < 0 || !haveData)
            return;
        const auto s = static_cast<size_t> (hoverBand);
        const double hz = themis::BandGrid::centreHz (hoverBand);
        const float x = xFor (hz);
        g.setColour (colours::text.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        const float d = measured[s] - target.medianDb[s];
        const juce::String f = hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String (static_cast<int> (hz)) + " Hz";
        g.setColour (colours::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (f + "   signal " + juce::String (measured[s], 1) + "   target " + juce::String (target.medianDb[s], 1) + "   delta "
                        + (d >= 0 ? "+" : "") + juce::String (d, 1) + " dB",
                    juce::Rectangle<int> (static_cast<int> (plot.getRight()) - 380, static_cast<int> (plot.getY()) + 2, 376, 16),
                    juce::Justification::right);
    }

    ThemisProcessor& proc;
    themis::BandArray measured {};
    themis::TargetCurve target;
    bool haveData = false;
    float tolerance = 1.0f;
    double fs = 48000.0;
    juce::Rectangle<float> plot;
    int hoverBand = -1;
};
