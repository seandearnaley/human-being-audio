# Human Being Audio — Monorepo Guide

## Repository Structure
- `studio.json`: canonical catalog of studio products, bundle IDs, plugin codes, and release metadata.
- `cmake/`: shared build infrastructure (`CPM.cmake`, `StudioDependencies.cmake`, `StudioHelpers.cmake`, `StudioProduct.cmake`, `native-presets.json`, `windows-release.ps1`, `ubuntu-26.04-packages.txt`).
- `modules/`: shared studio C++ libraries consumed by products:
  - `modules/dsp` (`hba_dsp`): header-only real-time DSP primitives (`adaa.h`, `allpass.h`, `biquad.h`, `control.h`, `crossover.h`, `curve.h`, `denormals.h`, `gain.h`, `lane_math.h`, `limiter.h`, `oscillator.h`, `oversampling.h`, `pitch.h`, `random.h`, `svf.h`, plus `third_party/hiir`). All audio-path functions are zero-allocation and `[[clang::nonblocking]]` compatible.
  - `modules/core` (`hba_core`): shared serialization, binary export & math primitives (`preset_json.h`, `wav.h`, `smf.h`, `param_math.h`, `note_name.h`).
  - `modules/visage-ui` (`hba_visage_ui`): single embedded Inter font bundle (`hba::ui::fonts`), studio dark-chrome design tokens & drawing primitives (`hba/ui/theme.h`), and interactive modulation-curve gesture editor (`hba/ui/movement_edit.h`).
  - `modules/plugin-host` (`hba_plugin_host`): JUCE host bridge helpers — `StateMailbox<T>`, `TelemetryChannel<T>`, `VisageSurface`, `StudioPluginEditor`, `MidiExpressionParser`, `parameters.h` (`makeParameterLayout`, `ParameterSnapshotter`), `CurveStore<N>`, `OfflineWavWorker` / `writeTempMidiFile`, `preset_bank.h`, and `StereoAnalyzerBase`.
  - `modules/studio-ui` (`hba_studio_ui`): `StudioAbout.h` and `StudioHistory.h` (coalesced Undo/Redo + A/B compare).
- `products/`:
  - `tract-form` — Sub-locked neuro bass synthesizer (JUCE 9 + Visage 1.0)
  - `pole-field` — 14-pole Z-plane morphing filterbank (JUCE 9 + Visage 1.0)
  - `cutline` — Expressive phrase grammar, microtiming & MPE/CC MIDI articulator (JUCE 9 + Visage 1.0)
  - `torus-glide` — Bar-locked Shepard-Risset & polyphonic chord-bend pitch-manifold synth (JUCE 9 + Visage 1.0)
  - `strike-body` — Phase-locked neuro snare, kick & nonlinear modal percussion synthesizer (JUCE 9 + Visage 1.0)
  - `livecut-redux` — Breakbeat & spectral live slicer (JUCE 9 + Visage 1.0)
- `factory/`: Python release packaging (`factory/release/`), verification (`verify_product.py`, `verify_recall.py`), and CI/quality tooling (`factory/quality/`).
- `web/`: Next.js + React website (`https://humanbeingaudio.com`).

## Real-Time Audio Safety & Compiler Rules
- Every engine `process()` path MUST be zero-allocation, lock-free, syscall-free, and pass Clang `-Wfunction-effects -Werror=function-effects` (`[[clang::nonblocking]]`) plus RealtimeSanitizer (`cmake --workflow --preset rtsan`).
- Studio targets compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- Processor owns Undo/Redo & A/B `hba::studio::History` so history persists across closing/reopening the editor.

## Listening & Audio Spot-Checks
- The `read_file` tool supports native audio understanding on `.wav` (`audio/wav`) and `.mp3` (`audio/mpeg`) files (`.flac` is not supported — convert to a temporary `.wav` in the scratch folder with `ffmpeg` if checking a `.flac` demo).
- When designing or significantly tuning DSP, voicing filters/saturators, crafting presets, or updating web audio demos, feel free to render a short `.wav` via the product's CLI renderer (e.g. `tg-render`) and listen with `read_file` to catch harshness, clicks, mud, or musical balance issues alongside quantitative DSP checks. Use pragmatically where ears help — skip for routine refactors or non-audio work.

## Common Developer Commands
```bash
# Configure, build, and test any CPM/Visage product (fast local dev preset):
cd products/<product>
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset dev
ctest --preset dev

# Engine-only fast loop (no JUCE/Visage):
cmake --preset engine -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset engine
ctest --preset engine

# Format ONLY changed C++ lines (matches CI exactly — never full-file format untouched files):
git-clang-format --binary clang-format origin/main -- products modules research \
  ':(exclude,glob)**/third_party/**' ':(exclude,glob)**/libs/**'

# Standalone shared modules unit tests (hba_dsp + hba_core):
cmake -S modules -B modules/build -G Ninja -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build modules/build
ctest --test-dir modules/build --output-on-failure

# Generate root compile_commands.json for clangd from whichever dev builds are configured:
python3 factory/quality/native.py editor --available

# Factory & quality tests:
python3 -m unittest discover -s factory/quality
python3 -m unittest discover -s factory/release
```
