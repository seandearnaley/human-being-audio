#include "PluginEditor.h"

#include <embedded/tf_brand.h>

namespace tf {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::tract_form_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::tract_form_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::tract_form_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::tract_form_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Monophonic bass synthesizer.";
    cfg.headline = "Monophonic bass synthesizer";
    cfg.body = "One master sine drives two reese voices and a clean sub. Shape the growl with a five-formant tract, "
               "wavefolding and four movement lanes.";
    cfg.backgroundArgb = 0xff0b0e13u;
    cfg.bodyHeight = 70.0f;
    cfg.bodyMaxLines = 3;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0b0e13u,
                         makeAboutConfig()) {}

} // namespace tf
