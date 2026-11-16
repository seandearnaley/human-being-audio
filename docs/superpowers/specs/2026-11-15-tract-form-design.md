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
   split at 200 Hz and 2.5 kHz, upward and downward compression, make-up that
   never lowers the level), times `growlLevel`.
9. **Mid/side**: side high-passed with LR4 at `xover`; `midGain`, `sideGain`.
10. **Growl ceiling**: the sub is a sine, so its peak is known exactly. The growl
    gets what is left, `ceiling − subPeak`, as a static ceiling: a linked soft
    clipper, a high-pass at `subFreq` to remove the low frequencies clipping
    regenerates, then a look-ahead true-peak limiter whose release is slower than
    a cycle of the note. Nothing here follows the sub's waveform: a gain that
    moves at the note's rate would put growl energy on the fundamental.
11. **Sub lock**: master sine, LR4 low-pass at `subFreq`, mono, times `subLevel`,
    delayed to match and added after the growl limiter. It is never processed.
12. Amp gate (2 ms attack, 30 ms release), then a linked soft clip at 0 dBFS as
    a safety net. With `limit` off only that clip bounds the output.

How loud the growl can be therefore depends on the room the sub leaves. The two
levels in the Out node are that trade.

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

Stable string ids, 79 in all. In 0.1 there were 54, 53 automatable (`quality` is not); Live shows
an empty device panel above 64.

| Id | Name | Range | Default |
|---|---|---|---|
| `coupling` | Coupling | 0–1 | 0.72 |
| `beat` | Beat | 0–1 | 0.38 |
| `reach` | Reach | 2–256 harmonics, log | 96 |
| `tract` | Tract | 0–1 | 0.61 |
| `warp` | Warp | 0–1 | 0.46 |
| `motion` | Motion | 0–1 | 0.78 |
| `width` | Width | 0–1 | 0.64 |
| `weight` | Weight | 0–1 | 0.3 |
| `fit` | Fit | 0–360° | 0 |
| `beatLock` | Beat lock | off/on | off |
| `glide` | Glide | 0–500 ms, skewed | 12.5 |
| `phaseReset` | Phase reset | off/on | on |
| `aDetune`, `bDetune` | Detune | −50–50 ct | +19, −19 |
| `aRatio`, `bRatio` | Ratio | 0.25–8, skewed | 1.000 |
| `aPhase`, `bPhase` | Phase | 0–360° | 0, 180 |
| `aPm`, `bPm` | PM | −1–1 | +0.62, −0.48 |
| `fbTime` | Feedback time | 1/4, 1/2, 1, 2, 4 cycles | 1 cyc |
| `fbAmount` | Feedback | 0–1, squared | 0.49 |
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
  Sculpt, Mid/side, Out. The sub path runs underneath from Master to Out. Each node draws
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
  varies by less than 0.15 dB while all lanes run at full depth. (A rectangular
  50 ms window over a pure 44 Hz sine already varies by 0.58 dB.)
- **Mono low end**: side energy one octave below `xover` is under −60 dB relative
  to mid. At the crossover itself an LR4 is only 6 dB down.
- **Block independence**: block sizes 1, 7, 64 and 512 give bit-identical output.
- **Ceiling**: 4× true peak never exceeds `ceiling` by more than 0.1 dB.
- **Aliasing**: at 4×, inharmonic energy between 20 Hz and 20 kHz is at least
  70 dB below the harmonics for a 1 kHz note with `drive` and `coupling` at 1 and
  detune at 0. Half-band filters pass some aliases just under Nyquist.
- **Sub independence**: sweeping `growlLevel` from 0 to 1 on a hot patch moves
  the sub band by under 0.05 dB, and the sub band rendered with and without the
  growl agrees to −36 dB (the remainder is linear crossover overlap).
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

## Revision 0.2

Listening to 0.1 showed the Neuro presets were close to white noise: with the
lanes off, Chrome Rasp had −17 dB of its mid-range energy off the note's
harmonics and a flat spectrum, against −53 dB and −3.8 dB per octave with
feedback off. Three causes: the feedback delay was timed to the beat rather than
the pitch; detuned voices shared one folder; and nothing limited how many
harmonics distortion made.

The rule for 0.2: **everything that adds spectrum is locked to the master, and
the growl's harmonics are bounded and weighted to the bottom.**

### Harmonic core

- **Per-voice distortion.** Reese A and Reese B each run through their own tract
  and fold. `width` mixes them afterwards (`L = (A + (1−w)·B) / (2−w)`, and the
  mirror for R), so detuned voices never intermodulate in a shared nonlinearity.
- **Reach** (`reach`, 2–256, a harmonic number, default 96): a 24 dB per octave
  low-pass at `reach · f0` after the fold. It follows pitch, glide and bend. It is
  a ceiling to pull down, not a wall: at the default it sits near 4 kHz on F1.
- **Weight** (`weight`, default 0.3): extra slope, from 0 to −12 dB per octave
  (`feel::weightSlopeDb`), by blending in one and then two one-pole low-passes.
  The pivot is `max(3 · f0, 1.5 · subFreq)` and the octave above it keeps its
  level, so Weight leans the spectrum without lowering it. The fold is already
  bottom-weighted (about −14 dB per octave at default drive), so a little goes a
  long way.
- **Fit** (`fit`, 0–360°): rotates the sub against the growl. At 0 the sub at
  the output is in phase with the master, with the sub low-pass's phase at `f0`
  compensated; `fit` adds to that. It changes how the two waveforms interleave,
  which shows as the crest factor of the sum.
- **Feedback in cycles.** `fbTime` is `1/4 cyc` … `4 cyc`: a delay of that many
  cycles of the note, so the loop is a comb on the harmonics. The beat-timed
  delays of 0.1 are gone: a delay unrelated to the pitch is what turned the loop
  chaotic. The loop is fed from undetuned copies of the voices, averages the taps
  at n and 2n cycles, and is low-passed at `min(reach, 8) · f0`, so it thickens
  the low harmonics and cannot bring noise back as Reach opens.
- **Beat lock** (`beatLock`): the Beat macro then selects a division
  (`choices::beatLock`, via `feel::beatLockIndex`) and the two voices are offset
  by a constant frequency so their fundamentals beat exactly once per division.
  Each voice keeps its share of the offset in proportion to its Detune setting,
  with its sign.

There are eight macros, in signal order: Coupling, Beat, Reach, Tract, Warp,
Sculpt, Motion, Width. They are the first eight parameters the plugin exposes,
in that order, so a controller's first bank is the macro row.

### Measuring noise

`tf-render --metrics` adds `tiltDbPerOct` (octave bands from 500 Hz up to
`0.8 · reach · f0`, so the Reach cut-off is not counted as tilt) and
`offHarmonicDb` (the share of 300 Hz–6 kHz energy further than a tolerance
from any harmonic of the note; white noise reads about −1 dB). Presets are held
to targets with the lanes off:

| Category | Tilt, dB per octave | Off-harmonic |
|---|---|---|
| Reese | −14 to −7 | under −30 dB |
| Neuro | −9 to −4 | under −25 dB |
| Talk, Motion | −10 to −5 | under −25 dB |

### Sculpt

Movement by subtraction: a bank of `sculptCount` (1–6) bell filters after Reach
and Weight, per channel, at the oversampled rate. Band `i` sits at harmonic
`sculpt · sculptSpread^i` of the note, so the bank tracks pitch, glide and bend;
bands above 0.45 of the sample rate are skipped. `sculptDepth` sets every band's
gain (`feel::sculptGainDb`: negative cuts notches down to −30 dB, positive lifts
peaks up to +15 dB, zero bypasses the stage) and `sculptRes` their width
(`feel::sculptQ`). Sweeping `sculpt`, the macro, moves the whole bank.

| Id | Name | Range | Default |
|---|---|---|---|
| `sculpt` | Sculpt | ×1.5–×48, log | ×6 |
| `sculptDepth` | Sculpt depth | −1–1 | −0.5 |
| `sculptCount` | Sculpt bands | 1–6 | 3 |
| `sculptSpread` | Sculpt spread | 1.2–4, log | 2 |
| `sculptRes` | Sculpt resonance | 0–1 | 0.5 |

### Movement

The four lanes keep their timing rule (phase from the host position) and gain
three things.

**Targets.** A lane moves one of ten parameters, chosen by `lane{n}Target`:

| Target | Full depth moves it by |
|---|---|
| Phase | ±90° on the two voices, opposite ways; wraps |
| Morph, Drive, Beat, Coupling, Weight, Width | ±0.5 |
| Reach, Sculpt | ±2 octaves (they are harmonic numbers, so they move by ratio) |
| Fit | ±180°; wraps |

Lanes aimed at the same target add. The defaults are the four fixed lanes of 0.1.

**Following.** With `lane{n}Follow` set to another lane, this lane plays that
lane's shape or curve at `lane{n}Ratio` (÷4 … ×4) times its rate, with its own
phase, depth and target. One drawn gesture can then drive several parameters in
step, out of step or inverted. Chains resolve to their end; a loop falls back to
the lane's own shape.

**Drawn curves.** With `lane{n}Shape` set to Custom the lane plays its `Curve`:
up to 32 breakpoints over one cycle, each leaving by a line (bent by a tension),
a smooth half-cosine or a hold. The built-in shapes are also the starting curves:
editing a Sine lane converts it to the two-point curve that is exactly a sine.
Curves are not host parameters. They live in the session state, travel in a
preset as `lane{n}Curve` when the lane plays them, and reach the audio thread as
a plain copy.

Only a lane's on switch, phase and depth are automatable; the rest shapes the
lane rather than performs it. That leaves 54 automatable parameters.

`core/include/tf/lanes.h` holds all of this once: the engine plays a lane with
`laneSend` and the interface draws it with the same call.

**Editing.** One lane is selected and expanded; the others stay compact. In the
selected lane: click to add a point, drag to move it (snapping to sixteenths,
Alt to free it), double-click to remove it, drag a segment to bend it, and
right-click a point for line, smooth or hold.
