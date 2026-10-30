// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The Pantheon authors

#pragma once

#include "../DSP/Band.h"

#include <pantheon/ui/Controls.h>
#include <pantheon/ui/Hardware.h>
#include <pantheon/ui/HardwareLookAndFeel.h>

// What the shared hardware kit (shared/ui) cannot know about Hephaestus: which colour a band mode is lit in. The drawing itself
// (panels, glass, lamps, knobs, buttons) is the kit's (namespace ui); Hephaestus' own view code is in hep::gui.
namespace hep::gui
{
// The two words of the plugin's name. The full name ("Hephaestus Forge") is set once, in CMakeLists.txt (PLUGIN_NAME), and
// reaches the code as JucePlugin_Name; UiCheck checks that the two agree.
inline constexpr const char* kProjectWord = "Hephaestus";
inline constexpr const char* kPluginWord = "Forge";

// Saturate bands are lit amber (the colour of a digital read-out), harmonic fill bands teal (phosphor).
inline juce::Colour forMode (hep::BandMode mode)
{
    return mode == hep::BandMode::fill ? colours::accent : colours::amber;
}
} // namespace hep::gui
