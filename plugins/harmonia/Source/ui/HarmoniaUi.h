// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include <pantheon/ui/Controls.h>
#include <pantheon/ui/Hardware.h>
#include <pantheon/ui/HardwareLookAndFeel.h>

// What the shared hardware kit (shared/ui) cannot know about Harmonia: the two pitches it draws. The singer's own pitch is an
// amber read-out, the corrected pitch teal phosphor. The drawing itself (panels, glass, knobs, buttons) is the kit's.
namespace words
{
// The two words of the plugin's name. The full name ("Harmonia Pitch") is set once, in CMakeLists.txt (PLUGIN_NAME), and
// reaches the code as JucePlugin_Name; the DevTool self-test checks that the two agree.
inline constexpr const char* kProjectWord = "Harmonia";
inline constexpr const char* kPluginWord = "Pitch";
} // namespace words

namespace pitchcolours
{
inline const juce::Colour sung = colours::amber;
inline const juce::Colour corrected = colours::accent;
} // namespace pitchcolours
