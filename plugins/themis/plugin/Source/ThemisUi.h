// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Themis authors

#pragma once

#include <pantheon/ui/Hardware.h>
#include <pantheon/ui/HardwareLookAndFeel.h>
#include <pantheon/ui/MeterCell.h>

#include <themis/Common.h>
#include <themis/Translation.h>

// What the shared hardware kit (shared/ui) cannot know about: Themis' own words, number formats and verdict type. The
// drawing itself (panels, lamps, knobs, glass, meters) lives in shared/ui and is used from here, not redrawn.
namespace colours
{
// The one mapping from a verdict to its lamp colour.
inline juce::Colour forLevel (themis::Level l)
{
    return l == themis::Level::good ? good : l == themis::Level::warn ? warn : bad;
}
} // namespace colours

namespace ui
{
// The two words of the plugin's name. The full name ("Themis Verdict") is set once, in CMakeLists.txt (PLUGIN_NAME), and
// reaches the code as JucePlugin_Name; the smoke test checks that the two agree.
inline constexpr const char* kProjectWord = "Themis";
inline constexpr const char* kPluginWord = "Verdict";

inline juce::String signedDb (float v) { return juce::String (themis::signedDbText (v)); }

inline juce::String hzText (double hz) { return juce::String (themis::hzText (hz)); }
} // namespace ui
