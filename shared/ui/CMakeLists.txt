# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The Pantheon authors

# The hardware-style interface kit shared by the plugins: brushed-metal modules, glass displays, lamps, knobs, the look and
# feel and the meter cell. Header-only; it needs JUCE's GUI and audio-processor modules (for the parameter-bound controls) and nothing from any plugin.
add_library(pantheon_ui INTERFACE)
target_include_directories(pantheon_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pantheon_ui INTERFACE cxx_std_17)
target_link_libraries(pantheon_ui INTERFACE juce::juce_gui_basics juce::juce_audio_processors)
