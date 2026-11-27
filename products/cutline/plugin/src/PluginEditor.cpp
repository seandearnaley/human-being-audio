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
    cfg.accessibilitySuffix = ". Drum patterns you can read, and a MIDI slicer.";
    cfg.headline = "Drum patterns you can read, and a MIDI slicer";
    cfg.body = "The grid is what plays: literal steps in three layers, one Density knob, fills you draw, and four "
               "scenes. Switch to MIDI Slicer to chop, strum and re-groove your own chords and clips.";
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
