# Human Being Audio — Monorepo Guide

## Repository Structure
- `studio.json`: canonical catalog of studio products, bundle IDs, plugin codes, and release metadata.
- `cmake/`: shared build infrastructure (`CPM.cmake`, `StudioHelpers.cmake`, `StudioProduct.cmake`, `native-presets.json`, `windows-release.ps1`, `ubuntu-26.04-packages.txt`).
- `modules/`: shared studio C++ libraries consumed by products:
  - `modules/dsp` (`hba_dsp`): header-only real-time DSP primitives (`adaa.h`, `control.h`, `crossover.h`, `curve.h`, `denormals.h`, `gain.h`, `lane_math.h`, `limiter.h`, `oversampling.h`, `svf.h`, plus `third_party/hiir`). All audio-path functions are zero-allocation and `[[clang::nonblocking]]` compatible.
  - `modules/core` (`hba_core`): shared `nlohmann::ordered_json` preset & curve serialization helpers (`hba/core/preset_json.h`).
  - `modules/visage-ui` (`hba_visage_ui`): single embedded Inter font bundle (`hba::ui::fonts`), studio dark-chrome design tokens & drawing primitives (`hba/ui/theme.h`), and interactive modulation-curve gesture editor (`hba/ui/movement_edit.h`).
  - `modules/plugin-host` (`hba_plugin_host`): JUCE host bridge helpers — lock-free try-lock `StateMailbox<T>`, cross-platform `VisageSurface` (macOS/Linux/Windows), and `MidiExpressionParser` (14-bit RPN 00 00, symmetric pitch wheel, CC1/64/74/121, Channel Pressure, Poly AT).
  - `modules/studio-ui` (`hba_studio_ui`): `StudioAbout.h` and `StudioHistory.h` (coalesced Undo/Redo + A/B compare).
- `products/`:
  - `tract-form` — Sub-locked neuro bass synthesizer (JUCE 9 + Visage 1.0)
  - `pole-field` — 14-pole Z-plane morphing filterbank (JUCE 9 + Visage 1.0)
  - `cutline` — Expressive phrase grammar, microtiming & MPE/CC MIDI articulator (JUCE 9 + Visage 1.0)
  - `livecut-redux` — Breakbeat & spectral live slicer (JUCE)
- `factory/`: Python release packaging (`factory/release/`), verification (`verify_product.py`, `verify_recall.py`), and CI/quality tooling (`factory/quality/`).
- `web/`: Astro + React website (`https://humanbeingaudio.com`).

## Real-Time Audio Safety & Compiler Rules
- Every engine `process()` path MUST be zero-allocation, lock-free, syscall-free, and pass Clang `-Wfunction-effects -Werror=function-effects` (`[[clang::nonblocking]]`) plus RealtimeSanitizer (`cmake --workflow --preset rtsan`).
- Studio targets compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- Processor owns Undo/Redo & A/B `hba::studio::History` so history persists across closing/reopening the editor.

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

# Factory & quality tests:
python3 -m unittest discover -s factory/quality
python3 -m unittest discover -s factory/release
```
