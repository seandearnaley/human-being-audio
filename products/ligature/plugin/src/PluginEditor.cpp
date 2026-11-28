// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/lg_brand.h>

namespace lg {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::ligature_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::ligature_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::ligature_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::ligature_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Chords, parts and the bridges between your sections.";
    cfg.headline = "Chords, parts and the bridges between them";
    cfg.body = "Write each section on a loop, then launch the next: Ligature plays a voice-led bridge, key changes "
               "included, timed so the new section lands on the phrase. Every note goes out as MIDI on channels 1-4.";
    cfg.backgroundArgb = 0xff0a0d12u;
    cfg.bodyHeight = 84.0f;
    cfg.bodyMaxLines = 4;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0a0d12u,
                         makeAboutConfig()) {}

} // namespace lg
