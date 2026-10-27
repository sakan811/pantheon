#pragma once

#include "PluginProcessor.h"
#include "ThemisLookAndFeel.h"

#include <array>
#include <functional>
#include <vector>

namespace ui
{
// A scrolling graph of the last minute of analysed frames (themis::FrameHistory): a legend row that doubles as the
// readout (latest values, or those under the cursor), a dB grid and one line per series. The two graphs in the editor
// differ only in their series, so they are two small subclasses of this.
class HistoryPanel : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Series
    {
        const char* name;
        juce::Colour colour;
        std::function<float (const themis::FrameStats&)> level; // dB; a value without themis::hasLevel() means "no value"
    };

    // Pull fresh frames (message thread, ~30 Hz). Repaints only when the analysis has produced something new.
    void update()
    {
        const auto& history = proc.frameHistory();
        const auto sequence = history.sequence();
        if (sequence == lastSequence)
            return;
        lastSequence = sequence;
        period = std::max (history.framePeriodSeconds(), 1.0e-3);
        const int wanted = std::min (themis::FrameHistory::kCapacity, static_cast<int> (std::ceil (kSpanSeconds / period)) + 1);
        numFrames = history.copyRecent (frames.data(), wanted);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        area.reduce (10.0f, 8.0f);
        const auto header = area.removeFromTop (16.0f);
        plot = area.withTrimmedLeft (30.0f).withTrimmedTop (6.0f).withTrimmedBottom (14.0f);

        drawGrid (g);
        for (const auto& s : series)
            drawSeries (g, s);
        drawHeader (g, header);
        if (numFrames == 0)
        {
            g.setColour (colours::dim);
            g.setFont (font (13.0f));
            g.drawText ("Waiting for audio", plot.toNearestInt(), juce::Justification::centred);
        }
        else if (hoverFrame >= 0)
        {
            g.setColour (colours::text.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (xFor (hoverFrame)), plot.getY(), plot.getBottom());
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int frame = -1;
        if (numFrames > 0 && plot.contains (e.position))
        {
            const double secondsAgo = (plot.getRight() - e.position.x) / plot.getWidth() * kSpanSeconds;
            frame = juce::jlimit (0, numFrames - 1, numFrames - 1 - static_cast<int> (std::lround (secondsAgo / period)));
        }
        if (frame != hoverFrame)
        {
            hoverFrame = frame;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hoverFrame = -1;
        repaint();
    }

protected:
    HistoryPanel (ThemisProcessor& p, juce::String titleText, std::vector<Series> s) : proc (p), title (std::move (titleText)), series (std::move (s))
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    // Text shown at the right end of the header for the frame under the cursor (or the latest one).
    virtual juce::String extraReadout (const themis::FrameStats&) const { return {}; }

    ThemisProcessor& proc;

private:
    static constexpr double kSpanSeconds = 60.0;
    static constexpr float kTopDb = 0.0f, kBottomDb = -72.0f, kGridDb = 12.0f;
    static constexpr float kLegendWidth = 122.0f;

    float xFor (int frame) const
    {
        const double secondsAgo = static_cast<double> (numFrames - 1 - frame) * period;
        return plot.getRight() - static_cast<float> (secondsAgo / kSpanSeconds) * plot.getWidth();
    }
    float yFor (float db) const
    {
        return plot.getY() + plot.getHeight() * (kTopDb - juce::jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb);
    }

    void drawGrid (juce::Graphics& g) const
    {
        g.setFont (font (11.0f));
        for (float db = kTopDb; db >= kBottomDb - 0.1f; db -= kGridDb)
        {
            const float y = yFor (db);
            g.setColour (colours::panelEdge);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (colours::dim);
            g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (plot.getX() - 32.0f, y - 7.0f, 28.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
        for (int s = 0; s <= static_cast<int> (kSpanSeconds); s += 20)
        {
            const float x = plot.getRight() - plot.getWidth() * static_cast<float> (s) / static_cast<float> (kSpanSeconds);
            g.setColour (colours::panelEdge);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (colours::dim);
            g.drawText (s == 0 ? juce::String ("now") : "-" + juce::String (s) + " s",
                        juce::Rectangle<float> (x - 22.0f, plot.getBottom() + 1.0f, 44.0f, 13.0f).toNearestInt(), juce::Justification::centred);
        }
    }

    // Lines break wherever a series has no value (silence; no fundamental found).
    void drawSeries (juce::Graphics& g, const Series& s) const
    {
        juce::Path p;
        bool pen = false;
        for (int i = 0; i < numFrames; ++i)
        {
            const float db = s.level (frames[static_cast<size_t> (i)]);
            if (!themis::hasLevel (db))
            {
                pen = false;
                continue;
            }
            const float x = xFor (i), y = yFor (db);
            pen ? p.lineTo (x, y) : p.startNewSubPath (x, y);
            pen = true;
        }
        g.setColour (s.colour);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawHeader (juce::Graphics& g, juce::Rectangle<float> row) const
    {
        g.setFont (font (11.5f, juce::Font::bold));
        g.setColour (colours::dim);
        g.drawText (title, row.removeFromLeft (92.0f).toNearestInt(), juce::Justification::left);

        const bool haveFrame = numFrames > 0;
        const themis::FrameStats* shown = haveFrame ? &frames[static_cast<size_t> (hoverFrame >= 0 ? hoverFrame : numFrames - 1)] : nullptr;
        g.setFont (font (11.0f));
        for (const auto& s : series)
        {
            auto cell = row.removeFromLeft (kLegendWidth);
            g.setColour (s.colour);
            g.fillRoundedRectangle (cell.getX(), cell.getCentreY() - 1.0f, 12.0f, 2.0f, 1.0f); // a line sample, like the series itself
            const float db = shown != nullptr ? s.level (*shown) : themis::kHistoryFloorDb;
            g.setColour (colours::text); // text stays in the text colour; the swatch carries the identity
            g.drawText (juce::String (s.name) + "  " + (themis::hasLevel (db) ? juce::String (db, 1) : juce::String ("-")),
                        cell.withTrimmedLeft (16.0f).toNearestInt(), juce::Justification::left);
        }
        if (shown != nullptr)
        {
            g.setColour (colours::dim);
            g.drawText (extraReadout (*shown), row.toNearestInt(), juce::Justification::right);
        }
    }

    juce::String title;
    std::vector<Series> series;
    std::array<themis::FrameStats, themis::FrameHistory::kCapacity> frames {};
    int numFrames = 0;
    int hoverFrame = -1;
    uint64_t lastSequence = ~uint64_t (0);
    double period = 0.1;
    juce::Rectangle<float> plot;
};

// Categorical series colours: the first four slots, in order, of a dark-surface palette (blue, orange, aqua, yellow).
namespace palette
{
inline const juce::Colour blue { 0xff3987e5 }, orange { 0xffd95926 }, aqua { 0xff199e70 }, yellow { 0xffc98500 };
}

// Stereo energy (mean-square dBFS) in the four ranges of the tonal view, over time.
class EnergyView : public HistoryPanel
{
public:
    explicit EnergyView (ThemisProcessor& p)
        : HistoryPanel (p, "ENERGY", { range (0, palette::blue), range (1, palette::orange), range (2, palette::aqua), range (3, palette::yellow) })
    {
        setTooltip ("Energy of the stereo signal (mid + side) in each frequency range over the last minute, in dB re full scale. "
                    "Unlike the tonal curve this is absolute: it follows the loudness of the programme.");
    }

private:
    static Series range (int i, juce::Colour c)
    {
        const auto idx = static_cast<size_t> (i);
        return { themis::kEnergyRanges[i].name, c, [idx] (const themis::FrameStats& f) { return f.rangeDb[idx]; } };
    }
};

// Level of the bass fundamental against the summed level of its harmonics, over time.
class FundamentalView : public HistoryPanel
{
public:
    explicit FundamentalView (ThemisProcessor& p)
        : HistoryPanel (p, "FUNDAMENTAL",
                        { { "FUND", palette::blue, [] (const themis::FrameStats& f) { return f.fundamentalLevelDb(); } },
                          { "HARMONICS", palette::orange, [] (const themis::FrameStats& f) { return f.harmonicsLevelDb(); } } })
    {
        setTooltip ("The lowest tonal peak between 30 and 250 Hz (the bass fundamental) and the summed level of its harmonics 2-8, "
                    "mean-square dBFS, measured on the mid signal. The lines break where no tonal bass is found. A fundamental far above its "
                    "harmonics will not survive small speakers; far below, the bass is carried by its harmonics.");
    }

    static juce::String noteName (double hz)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

private:
    juce::String extraReadout (const themis::FrameStats& f) const override
    {
        return f.hasFundamental() ? juce::String (juce::roundToInt (f.f0Hz)) + " Hz  " + noteName (f.f0Hz) : juce::String ("no tonal bass");
    }
};
} // namespace ui
