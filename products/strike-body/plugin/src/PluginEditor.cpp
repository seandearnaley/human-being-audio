// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/sb_brand.h>

namespace sb {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::strike_body_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::strike_body_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::strike_body_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::strike_body_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Phase-locked neuro snare, kick & modal percussion synthesizer.";
    cfg.headline = "Phase-locked neuro snare, kick & modal percussion synthesizer";
    cfg.body =
        "Three phase-locked synthesis layers — Fundamental Pitch-Drop Body, 24-Mode Nonlinear Tensioned Modal Shell, "
        "and Transient-Ducked Wire & Click — aligned with a live 40 ms Transient Microscope, Auto Phase-Align solver, "
        "16-stage Disperser, HIIR oversampled ADAA2 clipping, and one-click key-tagged WAV bounce.";
    cfg.backgroundArgb = 0xff0a0d12u;
    cfg.wordmarkWidth = 340.0f;
    cfg.wordmarkHeight = 56.0f;
    cfg.iconMaxSize = 210.0f;
    cfg.iconWidthFrac = 0.22f;
    cfg.topPadFrac = 0.18f;
    cfg.headlineFont = 22.0f;
    cfg.bodyFont = 16.0f;
    cfg.bodyMaxWidth = 540.0f;
    cfg.bodyHeight = 96.0f;
    cfg.bodyMaxLines = 5;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0a0d12u,
                         makeAboutConfig()) {}

} // namespace sb
