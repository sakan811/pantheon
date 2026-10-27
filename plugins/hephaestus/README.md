# Hephaestus — harmonic forge for mastering

A saturation plugin (VST3 / AU / Standalone, macOS universal) that fills in harmonics and
missing fundamentals for tonal balance. Built with JUCE 8 for FL Studio (VST3/AU on macOS).

![Hephaestus editor](docs/screenshot.png)

*The "Missing Fundamental" preset on programme material whose 55 Hz fundamental is absent (only 110/165/220 Hz are present): the grey input trace has a hole in the orange bass range, the orange output fills it.*

It does two jobs that are usually separate plugins:

| Engine | What it does |
|---|---|
| **Saturation** | 3-band (LR4 crossovers) harmonic saturator. Per-band drive, continuous Even ↔ Odd character, three curves, ADAA + linear-phase oversampling. Acts as a warmth/glue saturator in the lows and mids and as an exciter in the highs. |
| **Harmonic Bass** | Virtual bass / *missing-fundamental* effect. Generates H2–H5 of the bass range with exact, individually adjustable weights so the ear reconstructs a bass the speakers can't reproduce. |
| **Fundamental Restore** | Regenerates a fundamental that is *physically* missing while its harmonics are present (difference tone, 3f − 2f = f), and only fills gaps. |

Mastering tools: **Delta** (hear only what the plugin adds), **Gain Match** (loudness-matched
A/B), **Mid/Side** mode, **Mix** (latency-compensated), and a live **input-vs-output spectrum**
with the crossover points and bass range overlaid. The analyzer follows FabFilter Pro-Q's: an
8192-point FFT, **4.5 dB/oct tilt around 1 kHz** by default, a 90 dB range, fast attack and a selectable
release. Right-click it for Range (60/90/120 dB), Speed and Tilt (0/3/4.5/6 dB/oct); the choice is saved with the project.

## Using it in FL Studio

1. FL Studio → *Options → Manage plugins* → *Find plugins* (the VST3 is installed in
   `~/Library/Audio/Plug-Ins/VST3/Hephaestus.vst3`).
2. Put it on the master (or a bus) **before** the limiter. Latency (shown by FL as PDC) is
   reported automatically; it changes with the Oversampling setting.
3. Start from a preset, then:
   - **Thin or small-speaker bass:** *Missing Fundamental* or *Small-Speaker Bass*. Set
     **Range Low/High** around the fundamentals you want to support (the orange band in the display).
     Raise **Amount** until the display shows the dip filled, then confirm with **Delta**.
   - **808 / sub that disappears on laptops:** *808 Sub Fill*.
   - **Glue and warmth:** *Master Glue* / *Warm Tube* / *Tape Body*. Use **Gain Match** so more
     saturation isn't mistaken for "better".
   - **Dull top end:** *Air Exciter* (high-band drive; harmonics land above the band).
4. Judge with **Delta** and with the analyzer. The display is tilted +4.5 dB/oct (as Pro-Q's default) so typical programme material looks flat.

### Parameter guide

- **Even** — 0 % = symmetric curve → odd harmonics only (3rd, 5th…). Higher = asymmetric → adds 2nd, 4th… (tube-like).
- **Curve** — Tanh (smooth), Atan (open, tape-like), Soft (algebraic, gritty, strongest upper odd harmonics).
- **Harmonic Bass → H2–H5** — level of each harmonic *relative to the fundamental as the range filter passes it*, in dB (a fundamental sitting right on the Range Low/High edge is attenuated by that filter, so its harmonics are correspondingly lower). The 2nd and 3rd carry most of the missing-fundamental cue; keep H4/H5 lower for definition without bite.
- **Ceiling** — low-pass on the generated harmonics.
- **Restore → Fill gaps** — 100 % = add only what is missing (backs off once a real fundamental exists at that frequency); 0 % = always add.
- **Oversampling** — Off / 2× / 4× / 8×, linear-phase. 4× is the default.
- **Stereo mode** — Mid/Side saturates M and S; the bass engine then works on Mid only so bass stays centred.

## Research this is based on

- **Missing fundamental / virtual bass.** Larsen & Aarts' book *Audio Bandwidth Extension* (Wiley, 2004) frames the nonlinear-device (NLD) approach; Bai & Lin (JAES 2006) introduced phase-vocoder virtual bass; Mu, Gan & Tan (ICASSP 2012) and Hill & Hawksford (2010) built hybrids. The trade-off reported in that literature: **NLDs keep transients but give no control over individual harmonics; phase vocoders give precise harmonic weighting but smear transients.** Overview via [Eloi et al., DAFx 2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf).
  → *Hephaestus' answer:* divide the bass band by its own envelope, apply Chebyshev polynomials T2–T5, multiply the envelope back. For a unit sine, T*k* gives exactly the *k*-th harmonic (the weights are exact and level-independent), yet the processing stays in the time domain (no smearing). See [Csound `chebyshevpoly`](https://csound.com/manual/opcodes/chebyshevpoly/) and the [UW lecture notes](https://courses.cs.washington.edu/courses/cse490s/11au/lectures/E-Non-linearSynthesis.pdf).
- **Typical harmonic weighting.** Hobbyist virtual-bass designs use roughly −6/−12/−18/−24 dB for H2–H5 ([DIYAudio thread](https://www.diyaudio.com/community/goto/post?id=3684217)); the defaults here are in that region. Vendor claims for how far below the speaker's limit MaxxBass works vary (1.5–2 octaves), and its algorithm is proprietary, so this is not a MaxxBass clone.
- **Aliasing.** First-order antiderivative anti-aliasing (Parker, Zavalishin & Le Bivic, DAFx 2016; extended in [Werner & Azelborn, DAFx 2023](https://www.dafx.de/paper-archive/2023/DAFx23_paper_61.pdf)) suppresses but does not eliminate aliasing, and misbehaves in single precision at extreme drive ([JUCE forum](https://forum.juce.com/t/anti-derivative-anti-aliasing-adaa-issues/46993)). → ADAA runs in `double`, with a midpoint fallback, *inside* linear-phase oversampling.
- **Host behaviour.** Latency changes must be reported from the message thread ([JUCE forum](https://forum.juce.com/t/bug-setlatencysamples-crashes-ableton-live-10-vst3-fixed/40216)), and the legacy parameter API breaks VST3 automation ([JUCE forum](https://forum.juce.com/t/vst3-automation-with-legacy-parameter-handling-broken/44854)). → latency goes through an `AsyncUpdater`; all parameters use the versioned `ParameterID` API.
- **Even vs odd harmonics.** Sources disagree on which sounds "warmer" ([iZotope](https://www.izotope.com/en/learn/what-is-audio-saturation) vs. a [CreativeLive/Ozone lesson](https://phoenix.creativelive.com/class/mastering-music-with-izotope-ozone-11-tomas-george/lessons/saturation-vintage-tape-module-and-exciter-module)), so Even is a continuous control rather than fixed "tube/tape" types. Trust your ears and Delta.

## Signal flow

```
in → gain ─┬─ dry (delayed by latency) ─────────────────────────────────────────┐
           ├─ [M/S enc] ─┬─ Fundamental engine (base rate) ─ delay ──┐          │
           │             └─ LR4 split → per band: ↑OS → ADAA shaper → ↓OS → sum ┴─ + ─ [M/S dec] ─ [gain match] ─ mix/delta ─ out
```

Details that matter:

- **ADAA is applied to the nonlinear residual only** (`f(u)` minus its tangent line at the bias point) and added to the untouched signal. Plain first-order ADAA is a `[1 1]/2` low-pass in the linear regime (≈ −5 dB at 15 kHz at 48 kHz); this form keeps small-signal gain exactly 1 while still anti-aliasing the distortion. The DC blocker acts on the residual only, so bass in the clean path is never phase-shifted.
- **Small-signal gain is 1 for every drive, bias and curve**, so Drive changes tone, not just level.
- **The fundamental engine runs at the host rate**: generated components stay below ~1.5 kHz, so there is nothing to alias.
- **The Chebyshev envelope releases slowly (`6 / lo`).** The k-th harmonic scales as `a^k` where `a` is the normalised amplitude, so envelope droop between waveform peaks costs accuracy mostly in H4/H5. Doubling the release from `3/lo` cut the worst-case error from 1.26 dB to 0.38 dB at 60 Hz, with harmonics still 48 dB down 50 ms after a note ends.
- **Restore's gap detector** compares input vs restored energy per frequency in a bank of narrow resonators. Comparing against the whole bass band mistakes a 2nd harmonic for an existing fundamental, and correlating with the restored signal fails because a difference tone is in phase quadrature with the true fundamental of saw-like waveforms. Both were tried first and failed the test suite.

## Verification (all run on the final binaries)

`HephaestusTests` (headless DSP, `Tests/Tests.cpp`) — measured, not assumed:

| Check | Result |
|---|---|
| Small-signal frequency response, 40 Hz–20 kHz, every oversampling mode | flat within **0.01 dB** |
| Dry path vs reported latency | dry impulse lands **exactly** on the reported latency (49 / 61 / 65 samples at 2× / 4× / 8×) |
| Alias energy, 5.3 kHz sine, +18 dB drive (re fundamental) | naive tanh **−18 dB** → ADAA only **−31 dB** → with oversampling **≈ −75 dB** |
| Even control | bias 0: H2 < −160 dB; bias 0.5: H2 −15 dB |
| Harmonic Bass weights vs targets (−6/−9/−15/−22), 45 / 60 / 90 Hz, two input levels | within **0.6 dB** (0.3 dB mid-range), identical at −6 and −26 dBFS |
| Harmonic Bass tails | 48 dB down 50 ms after note-off, 95 dB down at 100 ms |
| Restore, 120+180 Hz present, 60 Hz missing | restores 60 Hz at 0.299 (target 0.3); tracks level linearly (−6.0 dB for −6 dB); backs off to **0.000** when the real fundamental exists (0.230 when missing) |
| Mid/Side keeps bass centred | 2nd harmonic of a side-only tone: −5 dB in L/R mode, **−93 dB** in M/S mode |
| Mix / Delta | mix = 0 is the input delayed by exactly the latency; delta = wet − dry and mix = 0.37 is the linear blend, all **sample-exact** |
| Gain Match | a −4.8 dB level drop becomes **0.0 dB** |
| Spectrum analyzer | a bin-centred −6.02 dBFS sine reads **−6.02 dB**; 4 kHz with 4.5 dB/oct tilt reads +2.98 dB (expected +2.98); off-bin worst case −7.44 dB (Hann scalloping, < 1.5 dB); left-only signal reads −3 dB (L/R power is averaged) |
| 600 randomised blocks (all params, block sizes 1–1500, mono/stereo, mode switches) | always finite |
| CPU, 10 s stereo, everything on | 1 % (no OS) → 11 % of one core (8×), Apple M4 |

`HephaestusHostCheck` loads the installed **VST3** through JUCE's host wrapper: 5 sample rates (22.05–192 kHz) × 5 block sizes (1–4096) with every parameter randomised, latency reporting follows the oversampling choice (0 / 49 / 61 / 65 samples), state and all 8 presets round-trip. Apple's `auval` passes for the **AU**.

## Known limitations (honest list)

- **Not yet tested inside FL Studio itself.** It was validated through JUCE's VST3 host and `auval`; *pluginval* was not run (the sandbox blocked executing the downloaded binary). Load it in FL and report anything odd.
- **Restore is experimental.** A squarer produces difference tones between *all* component pairs in its input band, so dense low-mid material can yield inharmonic bass content. It suits sparse bass material; leave it off for full mixes unless Delta says it helps.
- **Mix below 100 %** blends a multiband (allpass-shifted) wet signal with the dry one, so expect slight phase interaction near the two crossover frequencies. Magnitude through the plugin is flat; use 100 % where you can.
- **Gain Match** is a slow (~0.6 s) RMS match, clamped to ±12 dB. It will partly undo a deliberate bass boost, since low-end energy dominates RMS; that's why it's off by default.
- **The analyzer's absolute level** uses a Hann window, so a sine falling between FFT bins reads up to 1.4 dB low. Its tilt and range options mirror Pro-Q's, but it is not a Pro-Q clone: Pro-Q's smoothing and peak-hold details are not published.
- **Ad-hoc signed, not notarised.** Fine on this Mac. Distributing it needs a Developer ID signature and notarisation.
- Windows/Linux builds should work (JUCE is cross-platform) but were not built or tested.

## Build

```bash
# macOS. SDKROOT works around Command Line Tools shipping an SDK this Xcode's linker can't read.
export SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release    # fetches JUCE 8.0.16-security; a copy in third_party/JUCE is used if present
cmake --build build --target Hephaestus_VST3 Hephaestus_AU Hephaestus_Standalone   # installs to ~/Library/Audio/Plug-Ins
cmake --build build --target HephaestusTests && ./build/HephaestusTests_artefacts/Release/HephaestusTests
cmake --build build --target HephaestusHostCheck && ./build/HephaestusHostCheck_artefacts/Release/HephaestusHostCheck ~/Library/Audio/Plug-Ins/VST3/Hephaestus.vst3
cmake --build build --target HephaestusSnapshot   # renders the editor to PNG
```

See `CLAUDE.md` for the design invariants and the single-source-of-truth map before changing anything.

## Layout

```
Source/DSP/Biquad.h              double-precision biquads, Butterworth cascades, Bandpass pair
Source/DSP/Waveshaper.h          tanh / atan / algebraic curves + residual ADAA
Source/DSP/Saturator.h           LR4 3-band split, per-band oversampling + shaping
Source/DSP/FundamentalEngine.h   Chebyshev virtual bass + difference-tone restore + gap detector
Source/DSP/Engine.h              latency alignment, M/S, gain match, delta, meters
Source/DSP/AnalyzerTap.h         lock-free FIFOs feeding the display
Source/DSP/SpectrumAnalyzer.h    Pro-Q-style FFT analyzer (GUI-free, unit-tested)
Source/Parameters.h              parameter layout, presets
Source/PluginProcessor.*         JUCE wrapper
Source/PluginEditor.*, UI/       table-driven editor, look-and-feel, spectrum view
Tests/                           DSP tests, VST3 host check, editor snapshot, shared helpers
docs/screenshot.png              editor render (generated by HephaestusSnapshot)
```
