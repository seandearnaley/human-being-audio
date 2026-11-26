# Pole Field

A 14-pole Z-plane morphing filterbank for neurofunk, bass sound design and break sculpting, by Human Being Audio.

Seven cascaded second-order pole-zero TPT SVF sections interpolate across 3D morph cubes in log-frequency polar coordinates so audio-rate sweeps remain strictly stable. Inter-stage ADAA saturation drives resonant peaks from inside the filterbank, an LR4 Sub Lock path preserves mono sub-bass untouched and sample-aligned, and offline 14th-order Burg LPC lets you imprint any WAV file directly onto a Z-plane corner.

## What it does

- **14-pole Z-plane morphing** across 8 curated 3D cubes (Serial, Hybrid or Parallel topology; 4P to 14P).
- **Inter-stage ADAA drive** (Tube, Fold, Sine with asymmetry) selectable Pre, Inter or Post lattice inside HIIR oversampling (2×, 4× or 8×).
- **Sub lock** splits and phase-aligns a clean mono sub below `Sub split` with continuous `Fit` phase rotation.
- **MIDI pitch tracking & expression** shifts all 14 poles and zeros smoothly with glide relative to F1, responds to `CC1`, `CC74`, aftertouch, `CC121`, and RPN `00 00` pitch bend, and displays live modulation ghost arcs across the top-bar macros.
- **Burg LPC WAV Imprint** extracts 7 conjugate pole pairs from any audio file into the Imprint Cube.
- **Movement & A/B workflow** provides four drawable and follower lanes with five timing modes (`Sync`, `Retrig`, `1-Shot`, `Free` Hz, and `Free R` MIDI-retriggered Hz from `0.05`–`20 Hz` with exact phase-locked `Follow` + `Ratio` scaling), an Envelope Follower shape, plus 64-step Undo/Redo (`⌘Z` / `⌘⇧Z`) and A/B Compare across parameters, curves, and imprints.

## Building

```sh
export PATH=/opt/homebrew/bin:$PATH
cd products/pole-field
cmake --workflow --preset check
```

To install into `~/Library/Audio/Plug-Ins` on macOS:

```sh
tools/scripts/install.sh build/dev
```

## Licence

AGPL-3.0-or-later. See [LICENSE](LICENSE).
