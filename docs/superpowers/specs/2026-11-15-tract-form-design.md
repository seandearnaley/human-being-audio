# Tract Form — design (beta 0.1)

Tract Form is a monophonic bass synthesizer for neurofunk drum & bass. One master
sine drives everything: it phase-modulates two detuned reese voices, and it also
becomes a clean, mono, phase-locked sub that bypasses all processing.

Approved 2026-11-15. This file is the contract between the engine, the plugin
shell and the interface. Change it in the same commit as the code it describes.

## Decisions

| Area | Choice | Reason |
|---|---|---|
| Shell | JUCE 9.0.5 | Canonical VST3/AU/Standalone, parameters, state. |
| Interface | Visage 1.0.0 (Metal) | GPU vector rendering, headless screenshots. |
| Engine | Plain C++20, no JUCE | Fast tests, portable. |
| Licence | AGPL-3.0-or-later | Matches JUCE's open-source terms. |
| Formats | VST3, AU, Standalone | Ableton Live 12 hosts VST3 and AU, not CLAP. |
| Platform | macOS 12+, universal | Windows follows the beta. |
| Voices | Monophonic, last-note priority | Phase relationships stay deterministic. |
| Build | CMake presets, Ninja, ccache, CPM | One command, pinned tags, no submodules. |
| Tests | Catch2 v3, pluginval, auval | Empirical checks over review rounds. |

Company `Human Being Music`, bundle id `com.humanbeingmusic.tractform`,
manufacturer code `Hbmu`, plugin code `Trfm`.

## Modules

```
core/     tf_core    Parameter table, Params struct, preset JSON. No JUCE, no Visage.
engine/   tf_engine  All DSP. Depends on tf_core and HIIR only.
ui/       tf_ui      Visage frames. Depends on tf_core. Talks to a UiModel interface.
plugin/   TractForm  JUCE shell: formats, APVTS, MIDI, state, presets, analysis.
tools/    tf-render  CLI: preset + note + bars -> WAV. Engine only.
tests/               Catch2: core, engine, ui snapshots, plugin.
presets/             Factory presets, one JSON file each.
```

Dependency direction is one way: `plugin -> ui, engine -> core`. `ui` and
`engine` never include each other or JUCE.

## Signal flow

Oversampled domain (`M` = 2, 4 or 8 live; 16 offline), everything stereo:

1. **Master sine** at `f0`. Phase `pm` in cycles.
2. **Reese A, B**: `sin(2π(p_v + phase_v) + I_v·master + fb)` where
   `f_v = f0 · ratio_v · 2^(detune_v · beat / 1200)` and
   `I_v = pm_v · coupling · 8 rad`.
3. **Feedback**: `(A+B)/2` into a delay line of `fbTime` (tempo-synced), read
   with 3rd-order Lagrange interpolation, DC-blocked at 5 Hz, `tanh`, scaled by
   `fbAmount · 2π`, added to both voices' phase.
4. **Width**: `L = (A + (1−w)·B) / (2−w)`, `R = (B + (1−w)·A) / (2−w)`.
   At `w = 0` both channels carry the sum; at `w = 1` A is left and B is right.
5. **Tract**: per channel, five parallel trapezoidal SVF band-passes with
   alternating sign. Frequency, bandwidth and gain interpolate in the log domain
   between `vowelA` and `vowelB` at `morph`. Output
   `(1−t)·x + t·comp·Σ`, `t = tract`, `comp` a per-vowel loudness constant.
6. **Fold**: `x' = g·x + 0.5·asym`, `g = 10^(drive·30/20)`. Shapers: triangle
   fold, sine fold, `tanh`. First-order antiderivative anti-aliasing, and the
   shaper itself always runs at 16×: at 4× the triangle fold only reached −48 dB.
   DC blocker at 10 Hz. Output `(1−w)·x + w·f(x')`, `w = warp`.
7. **Decimate** by `M` with polyphase IIR half-bands (HIIR). The sub sine is
   generated at the oversampled rate and passes an identical decimator, so the
   two paths are sample-aligned by construction.

Base rate:

8. **Growl path**: LR4 high-pass at `subFreq`, then **Squash** (three bands
   split at 200 Hz and 2.5 kHz, upward and downward compression, auto make-up),
   times `growlLevel`.
9. **Mid/side**: side high-passed with LR4 at `xover`; `midGain`, `sideGain`.
10. **Sub lock**: master sine, LR4 low-pass at `subFreq`, mono, times `subLevel`.
11. Sum, amp gate (2 ms attack, 30 ms release), soft clip, look-ahead limiter
    (1.5 ms, true-peak detection at 4×) to `ceiling`.

Latency reported to the host is the decimator group delay, the look-ahead and
the true-peak detector: 106 samples at 48 kHz.

## Notes and timing

- Last-note priority with a note stack. Legato notes glide; a new note from
  silence starts at pitch.
- `phaseReset` on: a non-legato note-on resets master, A and B phases to zero. If
  the gate is still open, fade out over 1 ms first.
- Pitch bend ±2 semitones. Velocity is ignored.
- Movement lanes read the host position: lane phase is
  `frac(ppq / lengthInQuarters + phase/360)`. When the host is stopped, the clock
  runs from note-on at the host tempo. The shapes are defined once, in
  `core/include/tf/lanes.h`; the engine plays them and the interface draws them.

## Parameters

Stable string ids. 54 parameters, 53 automatable (`quality` is not); Live shows
an empty device panel above 64.

| Id | Name | Range | Default |
|---|---|---|---|
| `coupling` | Coupling | 0–1 | 0.72 |
| `beat` | Beat | 0–1 | 0.38 |
| `tract` | Tract | 0–1 | 0.61 |
| `warp` | Warp | 0–1 | 0.46 |
| `motion` | Motion | 0–1 | 0.78 |
| `width` | Width | 0–1 | 0.64 |
| `glide` | Glide | 0–500 ms, skewed | 12.5 |
| `phaseReset` | Phase reset | off/on | on |
| `aDetune`, `bDetune` | Detune | −50–50 ct | +19, −19 |
| `aRatio`, `bRatio` | Ratio | 0.25–8, skewed | 1.000 |
| `aPhase`, `bPhase` | Phase | 0–360° | 0, 180 |
| `aPm`, `bPm` | PM | −1–1 | +0.62, −0.48 |
| `fbTime` | Feedback time | 1/64, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4 | 1/16 |
| `fbAmount` | Feedback | 0–1 | 0.24 |
| `vowelA`, `vowelB` | Vowel | a, æ, e, i, o, u | æ, u |
| `morph` | Morph | 0–1 | 0.63 |
| `foldType` | Type | Fold, Sine, Tube | Fold |
| `drive` | Drive | 0–1 | 0.42 |
| `asym` | Asym | 0–1 | 0.18 |
| `midGain` | Mid | −12–12 dB | 0 |
| `sideGain` | Side | −12–12 dB | +4.2 |
| `xover` | Crossover | 60–300 Hz | 100 |
| `subFreq` | Sub low-pass | 40–200 Hz | 120 |
| `subLevel` | Sub | 0–1 | 0.5 |
| `growlLevel` | Growl | 0–1 | 0.76 |
| `squash` | Squash | 0–1 | 0.3 |
| `limit` | Limit | off/on | on |
| `ceiling` | Ceiling | −12–0 dB | −0.3 |
| `quality` | Quality | 2×, 4×, 8× | 4× |
| `lane{1..4}On` | On | off/on | on |
| `lane{1..4}Rate` | Rate | 8 bars … 1/32, see below | 1/8, 1/4, 1/8, 1/2 |
| `lane{1..4}Phase` | Phase | 0–360° | 0, 90, 180, 270 |
| `lane{1..4}Depth` | Depth | −1–1 | 0.62, 0.79, 0.46, 0.38 |
| `lane{1..4}Shape` | Shape | Sine, Triangle, Ramp up, Ramp down, Square, Steps | Sine |

Lane rates: 8 bars, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16,
1/16T, 1/32. `Steps` holds eight values per cycle from a fixed hash of the lane
and step index, so each lane has its own pattern and it repeats identically on
every pass.

Lane targets, each scaled by `motion`:

| Lane | Target | Full depth |
|---|---|---|
| 1 Phase | A and B phase offsets, opposite directions; wraps | ±90° each |
| 2 Tract | `morph` | ±0.5 |
| 3 Fold | `drive` | ±0.5 |
| 4 Detune | `beat` | ±0.5 |

Modulated values are clamped to the parameter range.

## Vowels

Frequencies in Hz, gains in dB, bandwidths in Hz. a/e/i/o/u are the bass voice
from the Csound manual's formant appendix. æ uses Peterson & Barney male means
for F1–F3; its upper formants, gains and bandwidths are estimates to be tuned by
ear.

| Vowel | F1–F5 | Gain | Bandwidth |
|---|---|---|---|
| a | 600 1040 2250 2450 2750 | 0 −7 −9 −9 −20 | 60 70 110 120 130 |
| æ | 660 1720 2410 3300 3750 | 0 −8 −14 −20 −28 | 70 90 110 130 140 |
| e | 400 1620 2400 2800 3100 | 0 −12 −9 −12 −18 | 40 80 100 120 120 |
| i | 250 1750 2600 3050 3340 | 0 −30 −16 −22 −28 | 60 90 100 120 120 |
| o | 400 750 2400 2600 2900 | 0 −11 −21 −20 −40 | 40 80 100 120 120 |
| u | 350 600 2400 2675 2950 | 0 −20 −32 −28 −36 | 40 80 100 120 120 |

## Interface

One screen, no tabs, 16:9, resizable with a fixed aspect ratio. Default
1280×720 logical pixels, minimum 960×540.

- **Top bar**: wordmark, preset name with previous/next and save, six macro
  knobs.
- **Signal graph**: Master, Reese A, Reese B with the feedback loop, Tract, Fold,
  Mid/side, Out. The sub path runs underneath from Master to Out. Each node draws
  its own live shape and holds its own values, edited by dragging.
- **Analysis**: scope (master, growl, output), phase correlation, spectrum.
- **Movement**: four lanes with on, rate, phase, depth and shape, drawn with a
  moving playhead. Beside them: meters, Squash, Limit, Ceiling, Quality,
  Resample.

No taglines, legends or helper text. Everything is drawn as vectors; no bitmaps.

`ui` reads and writes through `ui::Model`: real-valued get/set with gesture
begin and end, preset operations, Resample, and a `Visuals` snapshot (a scope
phase-locked to the master, stereo field, spectrum, levels, gain reduction, lane
state, current note). `FakeModel` drives headless rendering.

## Resample

Renders the current patch offline at 16× for exactly four bars at the host tempo
and the last played note, writes 32-bit float WAV to
`~/Music/Tract Form/Resamples/`, and turns the button into a drag source for
that file.

## Presets

One flat JSON object per file: parameter id to value in real units, plus `name`
and `category`. Missing ids take the default. Factory presets are embedded in the
binary; user presets live in `~/Music/Tract Form/Presets/`. `quality` is a
machine setting and is not restored by a preset. Categories, in menu order:
Reese, Neuro, Talk, Sub, Motion, then User.

## Verification

Engine properties, each a Catch2 test:

- **Sub stability**: output low-passed at `subFreq`, 50 ms Hann-weighted RMS
  varies by less than 0.5 dB while all lanes run at full depth. (A rectangular
  50 ms window over a pure 44 Hz sine already varies by 0.58 dB.)
- **Mono low end**: side energy one octave below `xover` is under −60 dB relative
  to mid. At the crossover itself an LR4 is only 6 dB down.
- **Block independence**: block sizes 1, 7, 64 and 512 give bit-identical output.
- **Ceiling**: 4× true peak never exceeds `ceiling` by more than 0.1 dB.
- **Aliasing**: at 4×, inharmonic energy between 20 Hz and 20 kHz is at least
  70 dB below the harmonics for a 1 kHz note with `drive` and `coupling` at 1 and
  detune at 0. Half-band filters pass some aliases just under Nyquist.
- **Path alignment**: sub and growl paths have equal latency to the sample.
- **Fuzz**: random parameters and note streams produce only finite samples.
- **Determinism**: the same input twice gives identical output.
- **Factory presets**: every preset parses, renders finite, and keeps the sub
  stable.

Plugin: a note sounds and decays to silence; the interface sees the right note,
a phase-locked scope and the sub in the spectrum; state round-trips; every
factory preset loads; host parameters map exactly like the core table.

Compile time: the audio path is `[[clang::nonblocking]]` and built with
`-Wfunction-effects -Werror=function-effects`.

Hosts: pluginval at strictness 10, `auval -v aumu Trfm Hbmu`.

Interface: `tf-ui-snapshot` renders the editor headlessly against `FakeModel`;
`tf-plugin-snapshot` plays a note through the real processor and renders the
interface from the real analysis, with no host or audio device.
