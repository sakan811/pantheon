#pragma once

#include "PluginProcessor.h"
#include "ThemisLookAndFeel.h"

#include <cstring>

// The reference-curve display: long-term spectrum of the signal against the target band.
class TonalBalanceView : public juce::Component, public juce::TooltipClient
{
public:
    explicit TonalBalanceView (ThemisProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    // Pull fresh data from the processor (message thread, ~30 Hz). Repaints only if something changed.
    void update()
    {
        themis::BandArray m {};
        const bool have = proc.readSpectrum (m);
        auto t = proc.activeTarget();
        const float tol = proc.paramReader.value (params::tolerance);
        const double rate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        float crest = 0.0f;
        const bool haveCrestNow = proc.readLowCrest (crest);
        themis::BandArray f {};
        const bool haveFoldNow = proc.readMonoFold (f);
        const int dev = static_cast<int> (proc.paramReader.value (params::device));

        const bool changed = have != haveData || haveFoldNow != haveFold || (haveFoldNow && differs (f, fold)) || dev != deviceIndex || (have && differs (m, measured)) || haveCrestNow != haveCrest
                             || (haveCrestNow && differs (crest, lowCrestDb)) || t.name != target.name
                             || differs (t.medianDb, target.medianDb) || differs (t.halfWidthDb, target.halfWidthDb)
                             || differs (tol, tolerance) || differs (rate, fs);
        if (!changed)
            return;
        haveData = have;
        haveFold = haveFoldNow;
        deviceIndex = dev;
        haveCrest = haveCrestNow;
        if (haveCrestNow)
            lowCrestDb = crest;
        if (have)
            measured = m;
        if (haveFoldNow)
            fold = f;
        target = std::move (t);
        tolerance = tol;
        fs = rate;

        device = proc.activeDevice();
        deviceResponse = device != nullptr ? device->responseDb() : themis::BandArray {};
        if (device != nullptr)
            device->usableRangeHz (themis::translation::kRangeFloorDb, deviceLoHz, deviceHiHz);
        report = haveData ? themis::analyseTranslation (measured, haveFold ? &fold : nullptr, target, device) : themis::TranslationReport {};
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);

        auto strips = area.removeFromBottom (2.0f * kStripHeight);
        auto strip = strips.removeFromTop (kStripHeight).reduced (8.0f, 4.0f);
        translationStrip = strips.reduced (8.0f, 4.0f);
        plot = area.reduced (40.0f, 14.0f).withTrimmedBottom (14.0f);

        drawGrid (g);
        drawDeviceRange (g);
        drawTarget (g);
        if (haveData)
        {
            drawMeasured (g);
            drawMeasuredThroughDevice (g);
            drawMonoFold (g);
        }
        else
        {
            g.setColour (colours::dim);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText ("Play your track - the curve builds up as audio flows through", plot.toNearestInt(),
                        juce::Justification::centred);
        }
        drawBroadband (g, strip);
        drawTranslation (g, translationStrip);
        drawHover (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverBand = plot.contains (e.position) ? bandForX (e.position.x) : -1;
        overTranslationStrip = translationStrip.contains (e.position);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hoverBand = -1;
        overTranslationStrip = false;
        repaint();
    }

    // Tooltip over the translation readouts: the numbers behind the bars.
    juce::String getTooltip() override
    {
        if (!overTranslationStrip || !haveData)
            return {};
        juce::StringArray lines;
        if (report.haveDevice && device != nullptr)
            lines.add (juce::String (device->name) + ": the track changes by " + signedDb (report.deviceChangeDb) + " overall, the target by "
                       + signedDb (report.referenceChangeDb) + ". Positive DEVICE LOSS means this track depends more on what the device cannot "
                         "reproduce than the target does.");
        if (report.haveFold)
            lines.add ("MONO FOLD is the power lost when the channels are summed (0 dB = mono-safe, 3 dB = uncorrelated, more = phase "
                       "cancellation). The coloured strip under the curve shows it per band.");
        lines.add ("Device responses are generic approximations, and the limits are rules of thumb; neither is measured data. "
                   "Nothing here changes the audio.");
        return lines.joinIntoString ("\n");
    }

private:
    // Bitwise comparison: only used to decide whether a repaint is needed.
    template <typename T>
    static bool differs (const T& a, const T& b) { return std::memcmp (&a, &b, sizeof (T)) != 0; }

    static constexpr float kTopDb = 12.0f, kBottomDb = -30.0f;
    static constexpr float kStripHeight = 44.0f;

    static juce::String signedDb (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
    static juce::Colour colourFor (themis::Level l)
    {
        return l == themis::Level::good ? colours::good : l == themis::Level::warn ? colours::warn : colours::bad;
    }
    static juce::String hzText (double hz)
    {
        if (hz >= 10000.0)
            return juce::String (juce::roundToInt (hz / 1000.0)) + " kHz";
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    // Display slope in the style of FabFilter Pro-Q's analyzer (4.5 dB/oct). The analysis is constant-Q
    // band energy, where pink noise (-3 dB/oct power density) is flat, so Pro-Q's 4.5 dB/oct reads as
    // +1.5 dB/oct on this grid. Applied identically to signal and target, pivoting at 1 kHz; deltas and
    // colours are unaffected.
    static constexpr float kDisplaySlopeDbPerOct = 4.5f, kPinkSlopeDbPerOct = 3.0f;
    static constexpr float kTiltDbPerOct = kDisplaySlopeDbPerOct - kPinkSlopeDbPerOct;
    static float tiltDb (int band)
    {
        return kTiltDbPerOct * static_cast<float> (std::log2 (themis::BandGrid::centreHz (band) / 1000.0));
    }
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
            const float y = yFor (target.medianDb[s] + target.halfWidthDb[s] * tolerance + tiltDb (i));
            i == 0 ? band.startNewSubPath (x, y) : band.lineTo (x, y);
        }
        for (int i = last; i >= 0; --i)
        {
            const auto s = static_cast<size_t> (i);
            band.lineTo (xFor (themis::BandGrid::centreHz (i)), yFor (target.medianDb[s] - target.halfWidthDb[s] * tolerance + tiltDb (i)));
        }
        band.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.16f));
        g.fillPath (band);
        g.setColour (colours::accent.withAlpha (0.45f));
        g.strokePath (band, juce::PathStrokeType (1.0f));

        for (int i = 0; i <= last; ++i)
        {
            const float x = xFor (themis::BandGrid::centreHz (i)), y = yFor (target.medianDb[static_cast<size_t> (i)] + tiltDb (i));
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
            g.drawLine (xFor (themis::BandGrid::centreHz (i)), yFor (sm[s] + tiltDb (i)), xFor (themis::BandGrid::centreHz (i + 1)),
                        yFor (sm[s + 1] + tiltDb (i + 1)), 2.2f);
        }
    }

    void drawBroadband (juce::Graphics& g, juce::Rectangle<float> strip) const
    {
        struct Range { const char* name; double lo, hi; };
        static constexpr Range ranges[4] = { { "LOW", 20, 120 }, { "LOW-MID", 120, 1000 }, { "HIGH-MID", 1000, 6000 }, { "HIGH", 6000, 20000 } };
        const float cellW = strip.getWidth() / 5.0f; // four tonal bands + the low-end crest meter
        drawLowCrest (g, juce::Rectangle<float> (strip.getX() + cellW * 4.0f, strip.getY(), cellW, strip.getHeight()).reduced (4.0f, 0.0f));
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

    // Crest factor (peak/RMS) of the 20-250 Hz band, laid out like TBC's Low End Crest Factor view: a
    // low-end that is too dynamic sits to the left, one that is too compressed to the right. The
    // "typical" zone is a hand-authored guess, not measured data.
    void drawLowCrest (juce::Graphics& g, juce::Rectangle<float> cell) const
    {
        constexpr float kDynamicDb = 15.0f, kCompressedDb = 3.0f; // scale ends (left, right)
        constexpr float kZoneLoDb = 6.0f, kZoneHiDb = 10.0f;      // typical range for mastered music

        const float top = cell.getY();
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.setColour (colours::dim);
        g.drawText ("LOW CREST", cell.removeFromTop (14.0f).toNearestInt(), juce::Justification::left);
        if (!haveCrest)
            return;

        const auto xForCrest = [&] (float db, const juce::Rectangle<float>& bar) {
            return bar.getX() + bar.getWidth() * (kDynamicDb - juce::jlimit (kCompressedDb, kDynamicDb, db)) / (kDynamicDb - kCompressedDb);
        };
        const auto bar = cell.removeFromTop (12.0f).reduced (0.0f, 3.0f);
        g.setColour (colours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (colours::accent.withAlpha (0.35f)); // typical zone
        g.fillRect (xForCrest (kZoneHiDb, bar), bar.getY(), xForCrest (kZoneLoDb, bar) - xForCrest (kZoneHiDb, bar), bar.getHeight());

        const float outside = lowCrestDb > kZoneHiDb ? lowCrestDb - kZoneHiDb : lowCrestDb < kZoneLoDb ? kZoneLoDb - lowCrestDb : 0.0f;
        const auto c = outside <= 0.0f ? colours::good : outside <= 2.0f ? colours::warn : colours::bad;
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (xForCrest (lowCrestDb, bar) - 2.0f, bar.getY() - 1.0f, 4.0f, bar.getHeight() + 2.0f), 2.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const juce::String note = lowCrestDb > kZoneHiDb ? "  dynamic" : lowCrestDb < kZoneLoDb ? "  compressed" : "";
        g.drawText (juce::String (lowCrestDb, 1) + " dB" + note,
                    juce::Rectangle<int> (juce::roundToInt (cell.getX()), juce::roundToInt (top) + 14, juce::roundToInt (cell.getWidth()), 14),
                    juce::Justification::right);
    }

    // Darkens the frequencies the selected device cannot reproduce (more than kRangeFloorDb below its passband).
    void drawDeviceRange (juce::Graphics& g) const
    {
        if (device == nullptr)
            return;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        const float loX = xFor (deviceLoHz), hiX = xFor (deviceHiHz);
        if (loX > plot.getX() + 1.0f)
            g.fillRect (plot.getX(), plot.getY(), loX - plot.getX(), plot.getHeight());
        if (hiX < plot.getRight() - 1.0f)
            g.fillRect (hiX, plot.getY(), plot.getRight() - hiX, plot.getHeight());
    }

    // The measured curve as the selected device would reproduce it (measured + the device's response).
    void drawMeasuredThroughDevice (juce::Graphics& g) const
    {
        if (device == nullptr)
            return;
        const int last = lastDrawableBand();
        juce::Path p;
        for (int i = 0; i <= last; ++i)
        {
            const auto s = static_cast<size_t> (i);
            const float x = xFor (themis::BandGrid::centreHz (i)), y = yFor (measured[s] + deviceResponse[s] + tiltDb (i));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, p, dashes, 2);
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt()); // a roll-off runs far below the plotted range
        g.setColour (colours::text.withAlpha (0.85f));
        g.fillPath (dashed);
        g.restoreState();
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Via " + juce::String (device->name), juce::Rectangle<int> (static_cast<int> (plot.getX()) + 6, static_cast<int> (plot.getY()) + 18, 320, 16),
                    juce::Justification::left);
    }

    // A thin strip along the bottom of the plot: how much of each band's power would be lost in a mono sum.
    void drawMonoFold (juce::Graphics& g) const
    {
        if (!haveFold)
            return;
        constexpr float kHeight = 5.0f;
        for (int i = 0; i <= lastDrawableBand(); ++i)
        {
            const auto s = static_cast<size_t> (i);
            if (measured[s] < themis::translation::kWorstBandFloorDb) // too little energy for the verdict to matter
                continue;
            const float loss = -fold[s];
            g.setColour (colourFor (themis::translation::bandMonoLevel (i, loss)).withAlpha (0.9f));
            const float x0 = xFor (themis::BandGrid::lowEdgeHz (i)), x1 = xFor (themis::BandGrid::highEdgeHz (i));
            g.fillRect (std::max (x0, plot.getX()), plot.getBottom() - kHeight, std::min (x1, plot.getRight()) - std::max (x0, plot.getX()), kHeight);
        }
    }

    // One-sided meter: 0 on the left, the marker moves right as `value` grows past the warn and bad limits.
    static void drawMeter (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& title, float value, float warnAt, float badAt,
                           themis::Level level, const juce::String& text)
    {
        const float top = cell.getY();
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.setColour (colours::dim);
        g.drawText (title, cell.removeFromTop (14.0f).toNearestInt(), juce::Justification::left);

        const auto bar = cell.removeFromTop (12.0f).reduced (0.0f, 3.0f);
        const float scale = badAt * 1.5f;
        const auto xFor01 = [&] (float v) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, v / scale); };
        g.setColour (colours::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (colours::good.withAlpha (0.25f));
        g.fillRect (bar.getX(), bar.getY(), xFor01 (warnAt) - bar.getX(), bar.getHeight());
        g.setColour (colours::warn.withAlpha (0.25f));
        g.fillRect (xFor01 (warnAt), bar.getY(), xFor01 (badAt) - xFor01 (warnAt), bar.getHeight());
        g.setColour (colours::bad.withAlpha (0.25f));
        g.fillRect (xFor01 (badAt), bar.getY(), bar.getRight() - xFor01 (badAt), bar.getHeight());
        g.setColour (colourFor (level));
        g.fillRoundedRectangle (juce::Rectangle<float> (xFor01 (value) - 2.0f, bar.getY() - 1.0f, 4.0f, bar.getHeight() + 2.0f), 2.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (text, juce::Rectangle<int> (juce::roundToInt (cell.getX()), juce::roundToInt (top) + 14, juce::roundToInt (cell.getWidth()), 14),
                    juce::Justification::right);
    }

    static void drawNote (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& title, const juce::String& text, juce::Colour colour)
    {
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.setColour (colours::dim);
        g.drawText (title, cell.removeFromTop (14.0f).toNearestInt(), juce::Justification::left);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.setColour (colour);
        g.drawText (text, cell.removeFromTop (26.0f).toNearestInt(), juce::Justification::left);
    }

    // Second readout row: how the balance is likely to translate (device loss vs the target, mono compatibility).
    void drawTranslation (juce::Graphics& g, juce::Rectangle<float> strip) const
    {
        namespace tr = themis::translation;
        const float cellW = strip.getWidth() / 5.0f;
        const auto cell = [&] (int i) { return juce::Rectangle<float> (strip.getX() + cellW * static_cast<float> (i), strip.getY(), cellW, strip.getHeight()).reduced (4.0f, 0.0f); };
        if (!haveData)
            return;

        if (report.haveDevice)
            drawMeter (g, cell (0), "DEVICE LOSS", report.extraLossDb, tr::kDeviceWarnDb, tr::kDeviceBadDb, report.deviceLevel,
                       signedDb (report.extraLossDb) + " vs target");
        else
            drawNote (g, cell (0), "DEVICE LOSS", "choose a device", colours::dim);

        if (report.haveFold)
        {
            drawMeter (g, cell (1), "MONO FOLD", report.monoLossDb, tr::kMonoWarnDb, tr::kMonoBadDb, report.monoLevel,
                       "-" + juce::String (report.monoLossDb, 1) + " dB");
            drawMeter (g, cell (2), "LOW-END MONO", report.lowMonoLossDb, tr::kLowMonoWarnDb, tr::kLowMonoBadDb, report.lowMonoLevel,
                       "-" + juce::String (report.lowMonoLossDb, 1) + " dB < " + juce::String (juce::roundToInt (tr::kLowEndHz)) + " Hz");
            if (report.worstBand >= 0)
                drawNote (g, cell (3), "WORST BAND",
                          hzText (themis::BandGrid::centreHz (report.worstBand)) + "  -" + juce::String (report.worstBandLossDb, 1) + " dB",
                          colourFor (tr::bandMonoLevel (report.worstBand, report.worstBandLossDb)));
            else
                drawNote (g, cell (3), "WORST BAND", "-", colours::dim);
        }
        else
        {
            drawNote (g, cell (1), "MONO FOLD", "measuring...", colours::dim);
        }

        if (device != nullptr)
            drawNote (g, cell (4), "DEVICE RANGE", hzText (deviceLoHz) + " - " + hzText (deviceHiHz), colours::dim);
        else
            drawNote (g, cell (4), "DEVICE RANGE", "-", colours::dim);
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
        const float tilt = tiltDb (hoverBand);
        juce::String text = f + "   signal " + juce::String (measured[s] + tilt, 1) + "   target " + juce::String (target.medianDb[s] + tilt, 1)
                            + "   delta " + (d >= 0 ? "+" : "") + juce::String (d, 1) + " dB";
        if (haveFold)
            text += "   mono -" + juce::String (-fold[s], 1) + " dB";
        g.drawText (text, juce::Rectangle<int> (static_cast<int> (plot.getRight()) - 520, static_cast<int> (plot.getY()) + 2, 516, 16),
                    juce::Justification::right);
        if (device != nullptr)
            g.drawText ("via " + juce::String (device->name) + "   " + juce::String (measured[s] + deviceResponse[s] + tilt, 1) + "   (device "
                            + signedDb (deviceResponse[s]) + ")",
                        juce::Rectangle<int> (static_cast<int> (plot.getRight()) - 520, static_cast<int> (plot.getY()) + 18, 516, 16),
                        juce::Justification::right);
    }

    ThemisProcessor& proc;
    themis::BandArray measured {};
    themis::BandArray fold {};           // mono fold per band (dB, <= 0)
    themis::BandArray deviceResponse {};
    themis::TargetCurve target;
    themis::TranslationReport report;
    const themis::DeviceProfile* device = nullptr;
    double deviceLoHz = 20.0, deviceHiHz = 20000.0;
    int deviceIndex = -1;
    bool haveFold = false;
    bool overTranslationStrip = false;
    juce::Rectangle<float> translationStrip;
    bool haveData = false;
    bool haveCrest = false;
    float lowCrestDb = 0.0f;
    float tolerance = 1.0f;
    double fs = 48000.0;
    juce::Rectangle<float> plot;
    int hoverBand = -1;
};
