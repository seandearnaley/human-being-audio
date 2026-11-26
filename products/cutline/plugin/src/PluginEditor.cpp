// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/cl_brand.h>

namespace cl {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::cutline_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::cutline_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::cutline_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::cutline_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Live MIDI breakbeat, polymeter, and rhythmic articulation slicer.";
    cfg.headline = "Live MIDI breakbeat, polymeter & articulation slicer";
    cfg.body = "Drive Drum Racks and Strike Body with 12 multi-bar breakbeat styles, 8 polymetric lanes, 4 instant "
               "scenes, and 8 quantized performance pads — or switch to MIDI Slicer to chop, strum, and re-groove "
               "your own live chords and clips with zero pitch drift.";
    cfg.backgroundArgb = 0xff0b0e13u;
    cfg.bodyHeight = 84.0f;
    cfg.bodyMaxLines = 4;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0b0e13u,
                         makeAboutConfig()) {}

} // namespace cl
