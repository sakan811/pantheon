# Harmonia

Natural-sounding pitch correction for solo vocals. VST3 + AudioUnit, universal (Apple Silicon + Intel).

## Using it in FL Studio

1. Build/install (already done once): `scripts/build.sh`. This copies the plug-ins to
   `~/Library/Audio/Plug-Ins/VST3/Harmonia.vst3` and `~/Library/Audio/Plug-Ins/Components/Harmonia.component`.
2. In FL Studio: **Options → Manage plugins → Find installed plugins** (rescan), then
   add **Harmonia** (the VST3) to the mixer insert of your vocal.
3. Put it **first** in the insert chain, before reverb/delay/compression.
4. Set the **key and scale** (or click **Detect key from my vocal**, sing/play a phrase, click again).
5. Pick a **style**: *Natural* for transparent correction, *Pop* for a polished studio sound, *Hard-tune* for the robotic effect.

FL compensates the plug-in's fixed 46 ms latency automatically when mixing. If you monitor through
Harmonia while recording, you will hear that delay: record dry and add Harmonia afterwards.

## Controls

| Control | What it does |
| --- | --- |
| **Key / Scale** | Notes the voice may land on. Click piano keys to allow/block individual notes (switches to *Custom*). |
| **Detect key** | Listens to your vocal and sets key + major/minor for you. |
| **Speed** | How fast the voice is pulled onto the note. *Instant* = robotic. 20–60 ms is transparent. |
| **Strength** | Amount of correction. 100 % = fully on the note; lower keeps more of your own pitch. |
| **Natural** | Keeps your own vibrato and expression. 100 % = all of it, 0 % = flattened onto the note. |
| **Formant** | Shifts vocal character without changing pitch. Leave at 0 to keep your own voice. |
| **Tuning (A4)** | Reference pitch, for songs not tuned to 440 Hz. |
| **Voice** | Bass/baritone, tenor/alto, soprano or Auto. Narrows the pitch search: faster, never jumps an octave. |
| **Bypass** | Original vocal, delayed by the latency so it stays aligned. |

The graph shows your pitch (blue) and the corrected pitch (green) over the last few seconds against the
allowed notes.

### Tips for the most natural result

* Use **Natural** style first; raise Speed only if notes still sound off.
* Choose the **correct key**. Wrong-key notes are the most audible "autotune" sign.
* Block notes your melody never uses (click the piano keys): fewer wrong-note snaps.
* Pick your **Voice** type rather than Auto if the vocal is clean and solo.
* Works on one voice at a time. Not for chords, backing-vocal stacks or full mixes.

## How it works

`detect (YIN) → pick target note + smooth the correction → PSOLA re-synthesis`

* **Pitch detection** ([PitchDetector.h](Source/dsp/PitchDetector.h)): YIN at ~24 kHz, refined at full rate; octave guard for
  strong harmonics; sub-cent precision.
* **Correction** ([PitchCorrector.h](Source/dsp/PitchCorrector.h)): the target note comes from a slow-moving "centre" of the sung
  note (so vibrato never makes it flip notes) that speeds up when the voice really moves. The correction is smoothed
  by *Speed*; *Natural* decides how much of the singer's own deviation is kept.
* **Re-synthesis** ([AutoTuneEngine.h](Source/dsp/AutoTuneEngine.h)): pitch-synchronous overlap-add (PSOLA) with sub-sample
  analysis marks. Grains are re-spaced, not resampled, so formants (the voice colour) stay where they were.
  Consonants and breaths (unvoiced) pass through untouched.
* A fixed 46 ms look-ahead means the pitch track is known before the audio is re-synthesised.

## Building

Requires CMake and Xcode (clang + macOS SDK). JUCE 8.0.12 is fetched automatically.

```sh
scripts/build.sh            # configure, build VST3 + AU + Standalone, install, ad-hoc sign
scripts/build_tests.sh      # offline DSP accuracy tests (no JUCE) + builds build-tools/tune_wav
```

> `scripts/build.sh` drives Xcode's clang/SDK through Unix Makefiles on purpose: on this machine `xcodebuild`
> cannot start until `sudo xcodebuild -runFirstLaunch` is run, which also breaks CMake's Xcode generator.
> If you run that once, the stock `cmake -G Xcode` flow works too.

Extra tools:

```sh
# tune a WAV file offline
build-tools/tune_wav in.wav out.wav --key A --scale minor --speed 40 --natural 0.7

# headless processor self-test and editor screenshot
cmake --build build --target HarmoniaDevTool
build/HarmoniaDevTool_artefacts/Release/HarmoniaDevTool selftest
build/HarmoniaDevTool_artefacts/Release/HarmoniaDevTool snapshot ui.png
```

## Test results (synthetic voices, 22–192 kHz)

* Steady notes land within ~0.05 cent of the target; wobble < 1 cent (E2 to C6, two vowels).
* Vibrato depth kept ≥ 100 % at *Natural 100*, ≤ 15 % at *Natural 0*.
* Formants unchanged when shifting 3 semitones (spectral centroid ratio 1.04; naive resampling gives 1.19).
* ~0.8 % of one core (stereo, 48 kHz; ~1.4 % at 96 kHz). Passes `auval` and `pluginval` at strictness 10.

## Layout

```
Source/dsp/        JUCE-free engine: Scales, PitchDetector, PitchCorrector, AutoTuneEngine,
                   Kernels (SIMD-friendly sums), Rings (history buffers)
Source/            PluginProcessor (parameters, host glue), PluginEditor
Source/ui/         Theme/look-and-feel, PitchGraph, NoteKeyboard
Tests/             dsp_test, tune_wav, DevTool, synthetic voice generator
scripts/           build.sh, build_tests.sh, toolchain.sh (shared Xcode clang/SDK lookup)
```
