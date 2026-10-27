#pragma once

#include "../DSP/Engine.h"
#include "../DSP/SpectrumAnalyzer.h"
#include "../Parameters.h"
#include "Look.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace hep::ui
{
// Logarithmic frequency axis shared by everything the view draws.
struct FreqAxis
{
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;

    static float toNormalised (float hz) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, std::log (hz / minHz) / std::log (maxHz / minHz));
    }

    static float toHz (float normalised) noexcept { return minHz * std::pow (maxHz / minHz, normalised); }
};

// Input (grey) vs output (ember) spectrum, plus the crossover points and the bass range being
// worked on. Analysis and defaults follow FabFilter Pro-Q's analyzer: 8192-point FFT, fast attack,
// selectable release, a 90 dB range and a 4.5 dB/oct tilt around 1 kHz. Range, speed and tilt are
// chosen from a right-click menu and saved with the project.
class SpectrumView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    SpectrumView (Engine& e, juce::AudioProcessorValueTreeState& s, const double& sampleRateRef)
        : engine (e), apvts (s), sampleRate (sampleRateRef)
    {
        range.load (apvts.state);
        speed.load (apvts.state);
        tilt.load (apvts.state);
        applyAnalyzerSettings();

        setTooltip ("Right-click for analyzer range, speed and tilt.");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colour;
        g.setColour (juce::Colour (0xff120f0d));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        const auto plot = plotArea();
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        drawGrid (g, plot);
        drawBassRange (g, plot);
        drawCrossovers (g, plot);
        drawTrace (g, plot, inTrace, trace.withAlpha (0.55f), trace.withAlpha (0.12f));
        drawTrace (g, plot, outTrace, glow, ember.withAlpha (0.28f));
        g.restoreState();

        drawAxisLabels (g, plot);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showSettingsMenu();
    }

private:
    static constexpr float topDb = -10.0f;

    // One selectable analyzer setting, persisted as a property on the plugin state.
    struct Setting
    {
        struct Option { const char* label; float value; };

        const char* title;
        const char* property;
        std::vector<Option> options;
        int selected;

        void load (const juce::ValueTree& state)
        {
            const float stored = state.getProperty (property, options[(size_t) selected].value);
            for (size_t i = 0; i < options.size(); ++i)
                if (std::fabs (options[i].value - stored) < 1.0e-4f)
                    selected = (int) i;
        }

        float value() const { return options[(size_t) selected].value; }
    };

    float rangeDb() const { return range.value(); }
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (6.0f, 6.0f); }

    float xOf (float hz, juce::Rectangle<float> r) const { return r.getX() + FreqAxis::toNormalised (hz) * r.getWidth(); }

    float yOf (float db, juce::Rectangle<float> r) const
    {
        const float t = (juce::jlimit (topDb - rangeDb(), topDb, db) - (topDb - rangeDb())) / rangeDb();
        return r.getY() + (1.0f - t) * r.getHeight();
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        g.setColour (colour::panelEdge.withAlpha (0.8f));
        for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
            g.drawVerticalLine ((int) xOf (f, plot), plot.getY(), plot.getBottom());

        const float step = rangeDb() > 100.0f ? 20.0f : 10.0f;
        g.setColour (colour::panelEdge.withAlpha (0.6f));
        for (float db = topDb - step; db > topDb - rangeDb(); db -= step)
            g.drawHorizontalLine ((int) yOf (db, plot), plot.getX(), plot.getRight());
    }

    void drawBassRange (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace colour;
        const float lo = apvts.getRawParameterValue (id::fundLo)->load();
        const float hi = (float) effectiveRangeHigh (lo, apvts.getRawParameterValue (id::fundHi)->load());
        const bool active = ! isOff (apvts.getRawParameterValue (id::extAmount)->load())
                            || ! isOff (apvts.getRawParameterValue (id::resAmount)->load());

        const auto band = juce::Rectangle<float> (xOf (lo, plot), plot.getY(), xOf (hi, plot) - xOf (lo, plot), plot.getHeight());
        g.setColour (ember.withAlpha (active ? 0.14f : 0.05f));
        g.fillRect (band);
        g.setColour (ember.withAlpha (active ? 0.55f : 0.2f));
        g.drawVerticalLine ((int) band.getX(), plot.getY(), plot.getBottom());
        g.drawVerticalLine ((int) band.getRight(), plot.getY(), plot.getBottom());
    }

    void drawCrossovers (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        g.setColour (colour::copper.withAlpha (0.8f));
        for (auto pid : { id::xoverLow, id::xoverHigh })
        {
            const float x = xOf (apvts.getRawParameterValue (pid)->load(), plot);
            for (float y = plot.getY(); y < plot.getBottom(); y += 8.0f)
                g.drawVerticalLine ((int) x, y, y + 3.0f);
        }
    }

    void drawAxisLabels (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (colour::dim);

        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const auto text = f >= 1000.f ? juce::String (f / 1000.f, 0) + "k" : juce::String ((int) f);
            const int x = (int) xOf (f, plot) + 3;
            g.drawText (text, std::min (x, (int) plot.getRight() - 28), (int) plot.getBottom() - 14, 28, 12, juce::Justification::left);
        }

        const float step = rangeDb() > 100.0f ? 20.0f : 10.0f;
        for (float db = topDb - step; db > topDb - rangeDb(); db -= step)
            g.drawText (juce::String ((int) db), (int) plot.getRight() - 30, (int) yOf (db, plot) - 12, 26, 12, juce::Justification::right);

        g.setColour (colour::trace);
        g.drawText ("INPUT", (int) plot.getRight() - 170, (int) plot.getY() + 4, 60, 12, juce::Justification::right);
        g.setColour (colour::glow);
        g.drawText ("OUTPUT", (int) plot.getRight() - 100, (int) plot.getY() + 4, 70, 12, juce::Justification::right);
    }

    void drawTrace (juce::Graphics& g, juce::Rectangle<float> plot, const SpectrumAnalyzer& analyzer,
                    juce::Colour line, juce::Colour fill) const
    {
        juce::Path path;
        const int width = (int) plot.getWidth();
        const float binHz = (float) analyzer.binFrequency (1);

        for (int px = 0; px <= width; ++px)
        {
            const float f0 = FreqAxis::toHz ((float) px / (float) width);
            const float f1 = FreqAxis::toHz ((float) (px + 1) / (float) width);

            // Narrower than a bin: interpolate. Wider: show the strongest bin the pixel covers.
            const float db = (f1 - f0) < binHz ? analyzer.levelAtFrequency (0.5 * (f0 + f1))
                                                : analyzer.peakInRange (f0, f1);
            const float x = plot.getX() + (float) px, y = yOf (db, plot);
            if (px == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        juce::Path filled (path);
        filled.lineTo (plot.getRight(), plot.getBottom());
        filled.lineTo (plot.getX(), plot.getBottom());
        filled.closeSubPath();
        g.setColour (fill);
        g.fillPath (filled);
        g.setColour (line);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

    void showSettingsMenu()
    {
        juce::PopupMenu menu;
        int nextId = 1;
        std::vector<std::function<void()>> actions;

        for (Setting* setting : { &range, &speed, &tilt })
        {
            menu.addSectionHeader (setting->title);
            for (size_t i = 0; i < setting->options.size(); ++i)
            {
                menu.addItem (nextId++, setting->options[i].label, true, (int) i == setting->selected);
                actions.push_back ([this, setting, i] { setting->selected = (int) i; applyAnalyzerSettings(); });
            }
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this, actions] (int result)
                            {
                                if (result > 0)
                                {
                                    actions[(size_t) result - 1]();
                                    repaint();
                                }
                            });
    }

    void applyAnalyzerSettings()
    {
        for (auto* a : { &inTrace, &outTrace })
        {
            a->setTilt (tilt.value());
            a->setReleaseSeconds (speed.value());
        }
        for (Setting* setting : { &range, &speed, &tilt })
            apvts.state.setProperty (setting->property, setting->value(), nullptr);
    }

    void timerCallback() override
    {
        for (auto* a : { &inTrace, &outTrace })
            a->setSampleRate (sampleRate);

        inTrace.update (engine.tapIn);
        outTrace.update (engine.tapOut);
        repaint();
    }

    Engine& engine;
    juce::AudioProcessorValueTreeState& apvts;
    const double& sampleRate;

    // Pro-Q defaults: 90 dB range and 4.5 dB/oct tilt; medium release.
    Setting range { "Range", "spectrumRange", { { "60 dB", 60.0f }, { "90 dB", 90.0f }, { "120 dB", 120.0f } }, 1 };
    Setting speed { "Speed", "spectrumSpeed", { { "Slow", 0.8f }, { "Medium", 0.4f }, { "Fast", 0.15f } }, 1 };
    Setting tilt  { "Tilt",  "spectrumTilt",  { { "0 dB/oct", 0.0f }, { "3 dB/oct", 3.0f }, { "4.5 dB/oct", SpectrumAnalyzer::defaultTilt }, { "6 dB/oct", 6.0f } }, 2 };

    SpectrumAnalyzer inTrace, outTrace;
};
}
