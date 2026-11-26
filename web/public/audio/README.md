# Showcase Audio Clips

All 42 clips (6 composed, automated musical examples per plugin) are rendered offline directly from the seven C++20 DSP engines in this repository at 48 kHz stereo and encoded to stripped VBR MP3 (`libmp3lame -q:a 2`) alongside precomputed 64-bar peak envelopes in `web/app/audioManifest.json`.

- `livecut-redux/` — 6 clips driven through `LivecutEngine` (`CutProc11`, `Rush`, `Bounce`, `Tape` stops, `Filter`/`Suction`, and `Loops`/`Glitch`).
- `tract-form/` — 6 clips rendered from `tf_engine` (`Coupled Reese`, `Acid Snap`, `808 Sub Hammer`, `Talking Vowel Slap`, `West-Coast Buchla`, and one sustained `Cathedral Sub Drone`).
- `pole-field/` — 6 clips processed by `pf_engine` (`Neuro Chew`, `Parallel Choir` cinema chords, `Amen Mangler`, `Razor Comb`, `Envelope Acid Wah`, and `Burg LPC Glass Chamber`).
- `cutline/` — 6 clips where `cl_engine` outputs sample-accurate MIDI/MPE/CC streams directly into `Tract Form`, `Pole Field`, a polyphonic MPE synth, and a velocity-sensitive drum kit (zero audition oscillator).
- `torus-glide/` — 6 clips rendered from `tg_engine` (`Sidewinder Neuro Riser`, `Neo-Riemannian Cinema`, `Phrygian Slide Table`, `Velvet Choir Cathedral`, `Risset Eternal Stutter`, and `Möbius Neuro Bass`).
- `strike-body/` — 6 clips synthesized from `sb_engine` (`Neuro Snare Roller`, `Dispersed Laser Kick`, `Studio Brass Pocket`, `Hyperpop Plastic Slap`, `Kirchhoff Anvil Ritual`, and `Chromatic Glass & Toms`).
- `lineage/` — 6 clips rendered from `ln_engine` (`Velvet Analog Reese`, `Strata Velvet Loop`, `Reverse Riptide Strata`, `Neuro Formant Crawler`, `TZFM Mercury Whip`, and `Halftime Strata Ghost`).

## Regenerating

```bash
python3 factory/showcase/build_showcase.py
```

Requires `cmake`, a C++20 compiler, `ffmpeg` with `libmp3lame`, and `CPM_SOURCE_CACHE` (defaults to `$HOME/.cache/CPM`).
