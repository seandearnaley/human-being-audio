// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/tg_brand.h>

namespace tg {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::torus_glide_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::torus_glide_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::torus_glide_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::torus_glide_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Continuous pitch-manifold synthesizer.";
    cfg.headline = "Continuous pitch-manifold synthesizer";
    cfg.body =
        "Bar-locked Shepard-Risset bass spirals with a mono sub-anchor floor, programmable polyphonic chord bends with "
        "Tymoczko voice-leading and Scale/Just-Intonation gravity wells, and Mobius infinite chord glides.";
    cfg.backgroundArgb = 0xff0a0d12u;
    cfg.wordmarkWidth = 320.0f;
    cfg.wordmarkHeight = 56.0f;
    cfg.iconMaxSize = 210.0f;
    cfg.iconWidthFrac = 0.22f;
    cfg.topPadFrac = 0.18f;
    cfg.headlineFont = 22.0f;
    cfg.bodyFont = 16.0f;
    cfg.bodyMaxWidth = 520.0f;
    cfg.bodyHeight = 84.0f;
    cfg.bodyMaxLines = 4;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0a0d12u,
                         makeAboutConfig()) {}

} // namespace tg
