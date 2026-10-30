// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <pantheon/ui/Controls.h>
#include <pantheon/ui/Hardware.h>
#include <pantheon/ui/HardwareLookAndFeel.h>

// What the shared hardware kit (shared/ui) cannot know about Harmonia: the two pitches it draws. The singer's own pitch is an
// amber read-out, the corrected pitch teal phosphor. The drawing itself (panels, glass, knobs, buttons) is the kit's.
namespace pitchcolours
{
inline const juce::Colour sung = colours::amber;
inline const juce::Colour corrected = colours::accent;
} // namespace pitchcolours
