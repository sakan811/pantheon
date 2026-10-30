// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "../PluginProcessor.h"
#include "HarmoniaUi.h"

// Scrolling pitch display: piano-roll lanes for the notes the tuner may use, the singer's
// own pitch (amber) and the corrected pitch (teal), on a window of glass.
class PitchGraph : public juce::Component, private juce::Timer
{
public:
    explicit PitchGraph (HarmoniaProcessor& p) : proc (p), lastMask (p.currentNoteMask()) { startTimerHz (30); }

    int singingPitchClass() const { return singingPc; }

    void paint (juce::Graphics& g) override
    {
        const auto full = getLocalBounds().toFloat();
        ui::hw::drawGlass (g, full, 6.0f, colours::accent);

        const float labelW = 40.0f;
        const auto plot = full.withTrimmedLeft (labelW).reduced (0.0f, 8.0f).withTrimmedRight (10.0f);
        const float laneH = plot.getHeight() / kSemitonesShown;
        auto yOf = [&] (float midi) { return plot.getCentreY() - (midi - viewCenter) * laneH; };

        const uint16_t mask = proc.currentNoteMask();

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (full.reduced (3.0f).toNearestInt());

            const int lo = (int) std::floor (viewCenter - kSemitonesShown * 0.5f) - 1;
            const int hi = (int) std::ceil (viewCenter + kSemitonesShown * 0.5f) + 1;
            for (int n = lo; n <= hi; ++n)
            {
                const int pc = harmonia::pitchClass (n);
                const bool allowed = (mask & (1u << pc)) != 0;
                const float yTop = yOf ((float) n + 0.5f);
                const juce::Rectangle<float> lane (plot.getX(), yTop, plot.getWidth(), laneH);

                if (allowed)
                {
                    const bool isTarget = voicedNow && std::abs ((float) n - targetNow) < 0.01f;
                    g.setColour (colours::accent.withAlpha (isTarget ? 0.20f : 0.07f));
                    g.fillRect (lane);
                }
                g.setColour (colours::glassGrid.withAlpha (pc == 0 ? 1.0f : 0.6f));
                g.fillRect (plot.getX(), yTop + laneH - 0.5f, plot.getWidth(), 1.0f);

                if (allowed || pc == 0)
                {
                    g.setColour (allowed ? (pc == 0 ? colours::text : colours::dim) : colours::dim.withAlpha (0.4f));
                    g.setFont (ui::lcdFont (11.0f, pc == 0 ? juce::Font::bold : juce::Font::plain));
                    g.drawText (noteName (n), juce::Rectangle<float> (full.getX() + 4.0f, yTop, labelW - 8.0f, laneH),
                                juce::Justification::centredRight);
                }
            }

            drawTraces (g, plot, yOf);
        }

        if (! anyVoiced)
        {
            g.setColour (colours::dim);
            g.setFont (ui::font (14.0f));
            g.drawText ("Play or record a vocal - your pitch will appear here", plot, juce::Justification::centred);
        }
        else if (readout.isNotEmpty())
        {
            const auto chip = juce::Rectangle<float> (plot.getX() + 10.0f, plot.getY() + 8.0f, 176.0f, 26.0f);
            g.setColour (colours::glass.withAlpha (0.85f));
            g.fillRoundedRectangle (chip, 5.0f);
            g.setFont (ui::lcdFont (13.0f, juce::Font::bold));
            g.setColour (pitchcolours::sung);
            g.drawText (readoutIn, chip.withTrimmedLeft (10.0f).withWidth (80.0f), juce::Justification::centredLeft);
            g.setColour (colours::dim);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), chip.withTrimmedLeft (86.0f).withWidth (22.0f), juce::Justification::centred);
            g.setColour (pitchcolours::corrected);
            g.drawText (readoutOut, chip.withTrimmedLeft (112.0f), juce::Justification::centredLeft);
        }

        ui::hw::drawGlassOverlay (g, full, 6.0f, true);
    }

private:
    static constexpr float kSemitonesShown = 16.0f;

    static juce::PathStrokeType stroke (float width) { return { width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded }; }

    static juce::String noteName (int midi)
    {
        return juce::String (harmonia::kNoteNames[harmonia::pitchClass (midi)]) + juce::String ((int) std::floor ((double) midi / 12.0) - 1);
    }

    // How many of the most recent analysis hops fit on screen.
    int visibleHops() const
    {
        return juce::jmin (harmonia::AutoTuneEngine::kHistory - 4, (int) (proc.hopRateHz() * kSecondsShown));
    }

    template <typename YFn>
    void drawTraces (juce::Graphics& g, juce::Rectangle<float> plot, YFn yOf)
    {
        const int write = proc.historyWriteIndex();
        const int visible = visibleHops();
        const int first = juce::jmax (0, write - visible);
        const float pxPerHop = plot.getWidth() / (float) visible;

        juce::Path sungPath, outPath;
        bool prevVoiced = false;
        float prevIn = 0.0f;

        for (int i = first; i < write; ++i)
        {
            const auto h = proc.historyAt (i);
            const float x = plot.getRight() - (float) (write - 1 - i) * pxPerHop;
            if (h.voiced <= 0.0f)
            {
                prevVoiced = false;
                continue;
            }
            const bool brk = ! prevVoiced || std::abs (h.inMidi - prevIn) > 6.0f;
            if (brk)
            {
                sungPath.startNewSubPath (x, yOf (h.inMidi));
                outPath.startNewSubPath (x, yOf (h.outMidi));
            }
            else
            {
                sungPath.lineTo (x, yOf (h.inMidi));
                outPath.lineTo (x, yOf (h.outMidi));
            }
            prevVoiced = true;
            prevIn = h.inMidi;
        }

        g.setColour (pitchcolours::sung.withAlpha (0.85f));
        g.strokePath (sungPath, stroke (1.6f));
        g.setColour (pitchcolours::corrected.withAlpha (0.18f));
        g.strokePath (outPath, stroke (7.0f));
        g.setColour (pitchcolours::corrected);
        g.strokePath (outPath, stroke (2.4f));
    }

    void timerCallback() override
    {
        const int write = proc.historyWriteIndex();
        const bool hadContent = anyVoiced;
        anyVoiced = false;
        voicedNow = false;
        singingPc = -1;
        readout.clear();

        const int visible = visibleHops();
        for (int i = juce::jmax (0, write - visible); i < write; ++i)
            if (proc.historyAt (i).voiced > 0.0f)
            {
                anyVoiced = true;
                break;
            }

        // newest voiced hop within the last ~120 ms
        for (int i = write - 1; i >= juce::jmax (0, write - 30); --i)
        {
            const auto h = proc.historyAt (i);
            if (h.voiced <= 0.0f)
                continue;

            voicedNow = true;
            targetNow = h.targetMidi;
            const int nearest = (int) std::lround (h.inMidi);
            const int cents = (int) std::lround ((h.inMidi - (float) nearest) * 100.0f);
            singingPc = harmonia::pitchClass (nearest);
            readoutIn = noteName (nearest) + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + juce::String (juce::CharPointer_UTF8 ("\xc2\xa2"));
            readoutOut = noteName ((int) std::lround (h.outMidi));
            readout = readoutIn;

            viewCenter += 0.10f * (juce::jlimit (30.0f, 90.0f, 0.5f * (h.inMidi + h.targetMidi)) - viewCenter);
            break;
        }

        // While nothing is being sung the picture is static: only redraw when the lanes change.
        const uint16_t mask = proc.currentNoteMask();
        if (anyVoiced || hadContent || mask != lastMask)
        {
            lastMask = mask;
            repaint();
        }
    }

    static constexpr float kSecondsShown = 3.5f;

    HarmoniaProcessor& proc;
    float viewCenter = 60.0f;
    float targetNow = 0.0f;
    bool anyVoiced = false, voicedNow = false;
    int singingPc = -1;
    uint16_t lastMask; // note mask as last drawn, to notice changes while idle
    juce::String readout, readoutIn, readoutOut;
};
