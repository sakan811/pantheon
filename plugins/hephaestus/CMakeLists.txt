cmake_minimum_required(VERSION 3.22)

# Universal binary so the plugin loads whether FL Studio runs natively or under Rosetta.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Hephaestus VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is not vendored: cmake/Juce.cmake fetches it (and owns the version).
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Juce.cmake) # JUCE, one version for the repository

# ---------------------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------------------
juce_add_plugin(Hephaestus
    COMPANY_NAME "Hephaestus Audio"
    BUNDLE_ID com.hephaestusaudio.hephaestus
    PLUGIN_MANUFACTURER_CODE Hpha
    PLUGIN_CODE Hphs
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Hephaestus"
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Hephaestus PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Hephaestus PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Hephaestus
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------
# Headless DSP tests (no plugin, no GUI):  cmake --build build --target HephaestusTests
# ---------------------------------------------------------------------------------------
juce_add_console_app(HephaestusTests PRODUCT_NAME "HephaestusTests")
target_sources(HephaestusTests PRIVATE Tests/Tests.cpp)
target_include_directories(HephaestusTests PRIVATE Source Tests)
target_link_libraries(HephaestusTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
set_target_properties(HephaestusTests PROPERTIES EXCLUDE_FROM_ALL TRUE OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")

# ---------------------------------------------------------------------------------------
# Host check: loads the built VST3 like a DAW would, fuzzes it, and renders the editor to PNG.
#   cmake --build build --target HephaestusHostCheck
# ---------------------------------------------------------------------------------------
juce_add_console_app(HephaestusHostCheck PRODUCT_NAME "HephaestusHostCheck")
target_sources(HephaestusHostCheck PRIVATE Tests/HostCheck.cpp)
target_include_directories(HephaestusHostCheck PRIVATE Tests)
target_compile_definitions(HephaestusHostCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(HephaestusHostCheck
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags)
set_target_properties(HephaestusHostCheck PROPERTIES EXCLUDE_FROM_ALL TRUE OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")

# ---------------------------------------------------------------------------------------
# Snapshot: renders the real editor component to a PNG (for design review / documentation).
#   cmake --build build --target HephaestusSnapshot
# ---------------------------------------------------------------------------------------
juce_add_console_app(HephaestusSnapshot PRODUCT_NAME "HephaestusSnapshot")
target_sources(HephaestusSnapshot PRIVATE Tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(HephaestusSnapshot PRIVATE Source Tests)
target_compile_definitions(HephaestusSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HephaestusSnapshot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
set_target_properties(HephaestusSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")

# ---------------------------------------------------------------------------------------
# UI check: drives the spectrum view with synthetic mouse events (band drags, limits, slots).
#   cmake --build build --target HephaestusUiCheck
# ---------------------------------------------------------------------------------------
juce_add_console_app(HephaestusUiCheck PRODUCT_NAME "HephaestusUiCheck")
target_sources(HephaestusUiCheck PRIVATE Tests/UiCheck.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(HephaestusUiCheck PRIVATE Source Tests)
target_compile_definitions(HephaestusUiCheck PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HephaestusUiCheck
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
set_target_properties(HephaestusUiCheck PROPERTIES EXCLUDE_FROM_ALL TRUE OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
