# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The Themis authors

cmake_minimum_required(VERSION 3.22)

# These must be set before project(): universal (Apple Silicon + Intel) binaries, macOS 11+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary: Apple Silicon + Intel")

project(Themis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(THEMIS_BUILD_PLUGIN "Build the JUCE plugin (VST3 / AU / Standalone)" ON)
option(THEMIS_BUILD_TESTS "Build the DSP unit tests" ON)
option(THEMIS_BUILD_SMOKE "Build the headless JUCE smoke test (tests/plugin_smoke.cpp)" OFF)
option(THEMIS_INSTALL_AFTER_BUILD "Copy the plugin into the user plugin folders after building (macOS)" ${APPLE})

# ---- DSP core: header-only, no dependencies ---------------------------------------------------
add_library(themis_core INTERFACE)
target_include_directories(themis_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_compile_features(themis_core INTERFACE cxx_std_17)

if(THEMIS_BUILD_TESTS)
    enable_testing()
    add_executable(themis_tests tests/test_main.cpp)
    target_link_libraries(themis_tests PRIVATE themis_core)
    add_test(NAME themis_dsp COMMAND themis_tests)
endif()

# ---- Plugin -------------------------------------------------------------------------------------
if(THEMIS_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)

    set(THEMIS_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND THEMIS_FORMATS AU)
    endif()

    juce_add_plugin(Themis
        COMPANY_NAME                "Themis Audio"
        BUNDLE_ID                   com.themisaudio.themis
        PRODUCT_NAME                "Themis Verdict"
        PLUGIN_NAME                 "Themis Verdict"
        DESCRIPTION                 "Reference-curve tonal balance analyser (audio passes through unchanged)"
        FORMATS                     ${THEMIS_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${THEMIS_INSTALL_AFTER_BUILD}
        PLUGIN_MANUFACTURER_CODE    Thmi
        PLUGIN_CODE                 Thms
        AU_MAIN_TYPE                kAudioUnitType_Effect
        VST3_CATEGORIES             Fx Analyzer
        HARDENED_RUNTIME_ENABLED    TRUE)

    target_sources(Themis PRIVATE
        plugin/Source/Parameters.cpp
        plugin/Source/PluginProcessor.cpp
        plugin/Source/PluginEditor.cpp)

    target_compile_definitions(Themis PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(Themis
        PRIVATE
            themis_core
            juce::juce_audio_utils
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(THEMIS_BUILD_SMOKE)
        juce_add_console_app(ThemisSmoke PRODUCT_NAME "ThemisSmoke")
        target_sources(ThemisSmoke PRIVATE
            tests/plugin_smoke.cpp
            plugin/Source/Parameters.cpp
            plugin/Source/PluginProcessor.cpp
            plugin/Source/PluginEditor.cpp)
        target_compile_definitions(ThemisSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(ThemisSmoke
            PRIVATE themis_core juce::juce_audio_utils juce::juce_audio_formats
            PUBLIC juce::juce_recommended_config_flags)
    endif()
endif()
