// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/pv_brand.h>

namespace pv {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::prism_vein_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::prism_vein_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::prism_vein_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::prism_vein_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Phase-locked spectral colorizer and harmonic resonator.";
    cfg.headline = "Phase-locked spectral colorizer, chord vocoder & Bark auto-tame resonator";
    cfg.body = "Four spectral and waveguide color engines (Chord Lattice, Carrier Morph, Harmonic Comb, Glass Freeze), "
               "phase-locked LR4 mono sub bypass, exact unity Transient Shield, zero-extra-latency Bark Auto-Tame, "
               "ADAA2 post-grit, and 4 drawable tempo-synced modulation lanes.";
    cfg.backgroundArgb = 0xff090c12u;
    cfg.wordmarkWidth = 280.0f;
    cfg.wordmarkHeight = 56.0f;
    cfg.iconMaxSize = 200.0f;
    cfg.iconWidthFrac = 0.22f;
    cfg.topPadFrac = 0.17f;
    cfg.headlineFont = 20.0f;
    cfg.bodyFont = 15.5f;
    cfg.bodyMaxWidth = 560.0f;
    cfg.bodyHeight = 88.0f;
    cfg.bodyMaxLines = 4;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff090c12u,
                         makeAboutConfig()) {}

} // namespace pv
