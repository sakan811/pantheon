// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include "PluginProcessor.h"
#include "ThemisLookAndFeel.h"

#include <themis/MasterAdvisor.h>

// The master plan: what to do to the master, worst first, from everything Themis measures (see themis::advise). Each row
// says what to change and by how much; the tooltip says why; rows that can be listened to have a HEAR chip that sets the
// Solo / Mute audition (click again to release). Display only: it reads the measurements and never touches the audio.
class AdvicePanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit AdvicePanel (ThemisProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::NormalCursor); }

    // Refresh from the measurements (message thread, a couple of times a second).
    void refresh()
    {
        inputs = proc.masterInputs();
        advice = themis::advise (inputs);
        firstRow = juce::jlimit (0, std::max (0, static_cast<int> (advice.items.size()) - 1), firstRow);
        repaint();
    }

    // The audition state changed elsewhere: only the HEAR chips care.
    void auditionChanged()
    {
        const auto now = proc.auditionSwitches();
        if (now != audition)
        {
            audition = now;
            repaint();
        }
    }

    // The most important item that can be listened to (for demos and tests), or an empty Switches.
    themis::Switches firstHearable() const
    {
        for (const auto& item : advice.items)
            if (item.hear.active())
                return item.hear;
        return {};
    }
    const themis::MasterAdvice& currentAdvice() const { return advice; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        area.reduce (10.0f, 8.0f);

        auto titleRow = area.removeFromTop (16.0f);
        g.setFont (ui::font (11.5f, juce::Font::bold));
        g.setColour (colours::dim);
        g.drawText ("MASTER PLAN", titleRow.toNearestInt(), juce::Justification::left);
        const auto [verdict, verdictColour] = verdictText();
        g.setColour (verdictColour);
        g.drawText (verdict, titleRow.toNearestInt(), juce::Justification::right);

        g.setFont (ui::font (11.0f));
        g.setColour (colours::dim);
        g.drawText (readoutText(), area.removeFromTop (16.0f).toNearestInt(), juce::Justification::left);
        area.removeFromTop (4.0f);

        listArea = area.toNearestInt();
        if (advice.items.empty())
        {
            g.setFont (ui::font (12.0f));
            g.setColour (advice.measuring ? colours::dim : colours::good);
            g.drawFittedText (emptyText(), listArea, juce::Justification::topLeft, 6, 1.0f);
            return;
        }

        const int rows = visibleRows();
        for (int r = 0; r < rows && firstRow + r < static_cast<int> (advice.items.size()); ++r)
            drawRow (g, advice.items[static_cast<size_t> (firstRow + r)], rowBounds (r));

        const int hidden = static_cast<int> (advice.items.size()) - (firstRow + rows);
        if (hidden > 0 || firstRow > 0)
        {
            g.setFont (ui::font (10.5f));
            g.setColour (colours::dim);
            g.drawText (juce::String (hidden > 0 ? hidden : 0) + " more below - scroll", juce::Rectangle<int> (listArea.getX(), listArea.getBottom() - kMoreHeight, listArea.getWidth(), kMoreHeight),
                        juce::Justification::centredRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverRow = rowAt (e.position.toInt());
        setMouseCursor (hoverRow >= 0 && advice.items[static_cast<size_t> (hoverRow)].hear.active() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int row = rowAt (e.position.toInt());
        if (row < 0 || !advice.items[static_cast<size_t> (row)].hear.active())
            return;
        const auto hear = advice.items[static_cast<size_t> (row)].hear;
        proc.setAudition (proc.auditionSwitches() == hear ? themis::Switches {} : hear); // a second click releases it
        auditionChanged();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int maxFirst = std::max (0, static_cast<int> (advice.items.size()) - visibleRows());
        const int next = juce::jlimit (0, maxFirst, firstRow + (w.deltaY < 0.0f ? 1 : w.deltaY > 0.0f ? -1 : 0));
        if (next != firstRow)
        {
            firstRow = next;
            repaint();
        }
    }

    juce::String getTooltip() override
    {
        if (hoverRow < 0)
            return "The master plan turns every measurement into what to do next, worst first. The limits behind it are rules of thumb, not measured data, "
                   "and a plan is only as good as the audio that has played: press Reset, then play the whole track. Themis never changes the audio; "
                   "only the HEAR chips and the AUDITION buttons change what you hear.";
        const auto& item = advice.items[static_cast<size_t> (hoverRow)];
        juce::String text = juce::String (item.headline) + "\n\n" + juce::String (item.why);
        if (item.hear.active())
            text += "\n\nClick to hear it: solo " + juce::String (themis::soloText (item.hear)) + ". Click again to release.";
        return text;
    }

private:
    static constexpr int kRowHeight = 58, kMoreHeight = 14, kChipWidth = 40;

    int visibleRows() const
    {
        const bool needsMore = static_cast<int> (advice.items.size()) * kRowHeight > listArea.getHeight();
        return std::max (1, (listArea.getHeight() - (needsMore ? kMoreHeight : 0)) / kRowHeight);
    }
    juce::Rectangle<int> rowBounds (int visibleIndex) const
    {
        return { listArea.getX(), listArea.getY() + visibleIndex * kRowHeight, listArea.getWidth(), kRowHeight };
    }
    int rowAt (juce::Point<int> p) const
    {
        for (int r = 0; r < visibleRows(); ++r)
            if (firstRow + r < static_cast<int> (advice.items.size()) && rowBounds (r).contains (p))
                return firstRow + r;
        return -1;
    }

    void drawRow (juce::Graphics& g, const themis::Advice& item, juce::Rectangle<int> row) const
    {
        row.removeFromBottom (4);
        const auto colour = colours::forLevel (item.level);
        g.setColour (colours::background.withAlpha (0.55f));
        g.fillRoundedRectangle (row.toFloat(), 5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (row.removeFromLeft (4).toFloat(), 2.0f);
        row.reduce (8, 4);

        if (item.hear.active())
        {
            const auto chip = row.removeFromRight (kChipWidth).removeFromTop (16);
            const bool on = audition == item.hear;
            g.setColour (on ? colours::warn : colours::panelEdge);
            g.fillRoundedRectangle (chip.toFloat(), 4.0f);
            g.setColour (on ? juce::Colours::black : colours::text);
            g.setFont (ui::font (10.0f, juce::Font::bold));
            g.drawText (on ? "STOP" : "HEAR", chip, juce::Justification::centred);
            row.removeFromRight (4);
        }

        const auto headlineFont = ui::font (12.0f, juce::Font::bold);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (headlineFont, item.headline, 0.0f, 0.0f);
        const int headlineLines = glyphs.getBoundingBox (0, -1, false).getWidth() > static_cast<float> (row.getWidth()) ? 2 : 1;
        g.setColour (colours::text);
        g.setFont (headlineFont);
        g.drawFittedText (item.headline, row.removeFromTop (headlineLines * 15), juce::Justification::topLeft, headlineLines, 0.9f);
        g.setColour (colours::dim);
        g.setFont (ui::font (10.5f));
        g.drawFittedText (item.why, row.removeFromTop (14 * (3 - headlineLines)), juce::Justification::topLeft, 3 - headlineLines, 1.0f);
    }

    std::pair<juce::String, juce::Colour> verdictText() const
    {
        if (advice.measuring)
            return { inputs.analysedSeconds < 0.5 ? juce::String ("PLAY YOUR TRACK")
                                                  : "MEASURING  " + juce::String (juce::roundToInt (inputs.analysedSeconds)) + " / " + juce::String (juce::roundToInt (themis::master::kMinSeconds)) + " s",
                     colours::dim };
        if (advice.items.empty())
            return { "READY", colours::good };
        int bad = 0, warn = 0;
        for (const auto& i : advice.items)
            (i.level == themis::Level::bad ? bad : warn)++;
        juce::StringArray parts;
        if (bad > 0)
            parts.add (juce::String (bad) + " TO FIX");
        if (warn > 0)
            parts.add (juce::String (warn) + " TO CHECK");
        return { parts.joinIntoString ("  -  "), colours::forLevel (advice.level()) };
    }

    juce::String readoutText() const
    {
        juce::StringArray parts;
        if (inputs.haveLoudness)
        {
            const auto level = themis::translation::classify (std::abs (inputs.integratedLufs - inputs.targetLufs), themis::master::kLoudnessWarnLu, themis::master::kLoudnessBadLu);
            parts.add (juce::String (inputs.integratedLufs, 1) + " LUFS (" + juce::String (inputs.targetLufs, 0) + ")" + (level == themis::Level::good ? "" : level == themis::Level::warn ? " !" : " !!"));
        }
        if (inputs.clip.have)
            parts.add ("TP " + juce::String (inputs.clip.truePeakDb, 1) + " dBTP");
        if (inputs.haveLoudness && inputs.clip.have)
            parts.add ("PLR " + juce::String (inputs.clip.truePeakDb - inputs.integratedLufs, 1) + " dB");
        const int secs = static_cast<int> (inputs.analysedSeconds);
        parts.add (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + " analysed");
        return parts.joinIntoString ("   ");
    }

    juce::String emptyText() const
    {
        if (advice.measuring)
            return "Press Reset, then play the whole track from the start. The plan appears after " + juce::String (juce::roundToInt (themis::master::kMinSeconds))
                   + " s of audio and gets more reliable the more of the song has played (set Averaging to Infinite for a whole-track measurement). Clipping is judged at once.";
        return "Nothing to fix: all " + juce::String (advice.checks) + " checks that have data are inside their limits. The limits are rules of thumb, so trust your ears for the last call.";
    }

    ThemisProcessor& proc;
    themis::MasterInputs inputs;
    themis::MasterAdvice advice;
    themis::Switches audition;
    juce::Rectangle<int> listArea;
    int firstRow = 0, hoverRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvicePanel)
};
