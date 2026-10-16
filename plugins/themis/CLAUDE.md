# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Themis: a reference-curve (tonal balance) plugin (VST3 + AU, Standalone) built on JUCE 8.0.6, in the style of iZotope Tonal Balance Control. It is analysis-only: audio passes through unchanged with zero latency. Targets FL Studio on macOS (universal arm64 + x86_64, macOS 11+). See [README.md](README.md) for the feature list and the "verified / not verified" table.

## Commands

```bash
make test                                   # build + run all DSP tests (g++/clang++ only, no JUCE, no network)
CXX=clang++ make test                       # override the compiler

# Full plugin (fetches JUCE via FetchContent, needs git + network on first configure)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Themis_VST3 Themis_AU -j8   # Themis_AU is macOS only

# Headless JUCE smoke test (real processor + editor, no host/display); writes a UI screenshot
cmake -S . -B build -DTHEMIS_BUILD_SMOKE=ON && cmake --build build --target ThemisSmoke
./build/ThemisSmoke_artefacts/ThemisSmoke snapshot.png
```

- On macOS the plugin build **copies the plugins into `~/Library/Audio/Plug-Ins/{VST3,Components}`** by default (`THEMIS_INSTALL_AFTER_BUILD`, default `${APPLE}`). Pass `-DTHEMIS_INSTALL_AFTER_BUILD=OFF` to avoid touching those folders. Apple Silicon needs an ad-hoc `codesign --force --deep --sign -` on the result (see README).
- Other CMake options: `THEMIS_BUILD_PLUGIN`, `THEMIS_BUILD_TESTS` (both default ON).
- There is no linter or formatter config in the repo. The Makefile compiles tests with `-Wall -Wextra -Wpedantic -Wconversion`; the plugin target uses JUCE's recommended warning flags and is kept warning-free in Themis' own sources.
- **Running a single test:** the test runner has no CLI filtering ([tests/test_main.cpp](tests/test_main.cpp) `main()` runs every `TEST(name)` registered). To iterate on one, temporarily comment out others or add an early filter; don't assume `--gtest_filter`-style flags exist. The tests use a tiny custom framework (`TEST`, `CHECK`, `CHECK_NEAR`), not Catch/GoogleTest.
- CI ([.github/workflows/build.yml](.github/workflows/build.yml)): `make test` on Linux, a universal macOS VST3 + AU build with `lipo`/`codesign`/`auval`, and a Linux VST3 compile check. Tagging `v*` publishes a release.

## Architecture

Two layers with a hard dependency direction: `plugin/` → `core/`, never the reverse.

**`core/include/themis/` — header-only, dependency-free C++17 DSP** (no JUCE types). The plugin only uses [TonalAnalyzer.h](core/include/themis/TonalAnalyzer.h) and [TargetCurve.h](core/include/themis/TargetCurve.h). The rest ([Engine.h](core/include/themis/Engine.h), EQ, compressor, limiter, loudness, EqFit…) is a full mastering chain that the plugin **no longer uses** but that is kept, with its tests, in `core/`.

- **Shared analysis grid:** `BandGrid` in TonalAnalyzer.h (61 bands, 1/6-octave from 20 Hz) is the common representation for the analyzer and `TargetCurve` (built-ins, `fromTracks`, JSON `.themiscurve`). Curves are normalised to 0 dB mean over the 200 Hz–5 kHz anchor region (`normaliseAnchor`), so they express shape, not level.
- Cross-thread: the audio thread only calls `TonalAnalyzer::push` (lock-free ring, no FFT). A plugin-owned worker thread (`ThemisProcessor::AnalysisThread`, started in `prepareToPlay`, stopped in `releaseResources`/destructor) calls `pump()` every ~15 ms to run the FFT and publish through atomics. `requestReset` is applied by the next `pump`. `process()` = `push` + `pump` for offline/tests; `prepare()` must never overlap with `push`/`pump`. `FFT.h` has a complex `FFT` and a `RealFFT` (half-size complex FFT) used by the analyzer.

**`plugin/Source/` — JUCE layer**
- [Parameters.cpp/.h](plugin/Source/Parameters.cpp): three APVTS parameters (`target`, `tolerance`, `avgTime`); `params::Reader` caches the raw atomics.
- [PluginProcessor.cpp](plugin/Source/PluginProcessor.cpp): `processBlock` only *reads* the buffer into one `TonalAnalyzer` (via `push`) (never writes it; latency stays 0). State = APVTS XML plus a `customCurve` property holding the target curve as JSON. "Learn from tracks" decodes files on a lazily-created `ThreadPool` job with an `alive` shared flag guarding the async callback against processor destruction. Keep the constructor and `prepareToPlay` light: FL Studio instantiates the plugin while scanning.
- Editor (`PluginEditor`, `TonalBalanceView`, `ThemisLookAndFeel`) is timer-driven and reads from the processor/atomics; it holds no DSP state.
- Built-in target names populate the `target` choice parameter, with "Custom" appended as the last index; reordering/adding built-ins in `TargetCurve::builtIns()` shifts that index and affects saved sessions.

**Tests**
- [tests/test_main.cpp](tests/test_main.cpp): DSP-only, no JUCE; covers the core library (loudness, limiter/compressor, EQ fit, curve JSON round-trip, engine…), including DSP no longer used by the plugin.
- [tests/plugin_smoke.cpp](tests/plugin_smoke.cpp): includes the plugin sources directly (the `ThemisSmoke` target recompiles `Parameters/PluginProcessor/PluginEditor.cpp`), so new plugin `.cpp` files must be added to **both** `Themis` and `ThemisSmoke` in [CMakeLists.txt](CMakeLists.txt).

## Gotchas

- JUCE is AGPLv3/commercial; distributing builds has licensing implications (README, "Known limitations").
- The README documents known limiter overshoot on full-band white noise (up to ~1 dB); don't "fix" core tests by loosening ceilings without checking that note.
- Stereo in/stereo out only; `isBusesLayoutSupported` rejects everything else.
