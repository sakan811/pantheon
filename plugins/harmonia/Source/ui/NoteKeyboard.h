// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "../dsp/Scales.h"
#include "Theme.h"

#include <functional>

// One octave of piano keys. Lit keys are the notes the tuner may land on; click a key to
// allow or block that note. The note currently being sung gets a ring.
class NoteKeyboard : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int pitchClass)> onToggle;

    void setState (uint16_t allowedMask, int rootNote, int singingPitchClass)
    {
        if (allowedMask != mask || rootNote != root || singingPitchClass != singing)
        {
            mask = allowedMask;
            root = rootNote;
            singing = singingPitchClass;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const float wk = whiteKeyWidth();

        for (int i = 0; i < 7; ++i)
            drawKey (g, kWhiteNotes[i], { area.getX() + wk * (float) i + 1.0f, area.getY(), wk - 2.0f, area.getHeight() }, false);

        for (int pc = 0; pc < 12; ++pc)
            if (blackIndex (pc) >= 0)
                drawKey (g, pc, blackKeyBounds (pc), true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int pc = noteAt (e.position);
        if (pc >= 0 && onToggle)
            onToggle (pc);
    }

private:
    static constexpr int kWhiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };

    static int blackIndex (int pc)
    {
        switch (pc)
        {
            case 1: return 0;
            case 3: return 1;
            case 6: return 3;
            case 8: return 4;
            case 10: return 5;
            default: return -1;
        }
    }

    float whiteKeyWidth() const { return (float) getWidth() / 7.0f; }

    juce::Rectangle<float> blackKeyBounds (int pc) const
    {
        const float wk = whiteKeyWidth();
        const float w = wk * 0.58f;
        return { wk * (float) (blackIndex (pc) + 1) - w * 0.5f, 0.0f, w, (float) getHeight() * 0.60f };
    }

    int noteAt (juce::Point<float> p) const
    {
        for (int pc = 0; pc < 12; ++pc)
            if (blackIndex (pc) >= 0 && blackKeyBounds (pc).contains (p))
                return pc;
        return kWhiteNotes[juce::jlimit (0, 6, (int) (p.x / whiteKeyWidth()))];
    }

    void drawKey (juce::Graphics& g, int pc, juce::Rectangle<float> r, bool black)
    {
        const bool on = (mask & (1u << pc)) != 0;
        const juce::Colour fill = black ? (on ? theme::accent.darker (0.35f) : theme::blackKeyOff)
                                        : (on ? theme::accent : theme::whiteKeyOff);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (black ? theme::bg : theme::panelEdge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        if (pc == singing)
        {
            g.setColour (theme::sung);
            g.drawRoundedRectangle (r.reduced (1.5f), 4.0f, 2.5f);
        }

        const bool isRoot = pc == root;
        g.setColour (on ? (black ? theme::text : theme::bg) : theme::dim);
        g.setFont (theme::font (black ? 10.0f : 12.0f, isRoot));
        const auto labelArea = black ? r.removeFromBottom (18.0f) : r.removeFromBottom (24.0f);
        g.drawText (harmonia::kNoteNames[pc], labelArea, juce::Justification::centred);

        if (isRoot && on)
        {
            g.setColour (black ? theme::text : theme::bg);
            g.fillEllipse (r.getCentreX() - 2.5f, r.getY() + (black ? r.getHeight() - 30.0f : r.getHeight() - 40.0f), 5.0f, 5.0f);
        }
    }

    uint16_t mask = 0x0FFF;
    int root = 0, singing = -1;
};
