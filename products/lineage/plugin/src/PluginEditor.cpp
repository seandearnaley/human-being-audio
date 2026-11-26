// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/ln_brand.h>

namespace ln {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::lineage_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::lineage_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::lineage_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::lineage_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Non-destructive resampling Reese and bass sculpting synthesizer.";
    cfg.headline = "Non-destructive resampling Reese & bass sculpting synthesizer";
    cfg.body =
        "Seven-voice zero-aliasing Reese, Soft/Hard Sync, and TZFM core feeding a serial ADAA2 sculpting bench, "
        "phase-locked clean mono sub re-injection, and live multi-generation Strata resampling.";
    cfg.backgroundArgb = 0xff090c12u;
    cfg.wordmarkWidth = 280.0f;
    cfg.wordmarkHeight = 62.0f;
    cfg.iconMaxSize = 210.0f;
    cfg.iconWidthFrac = 0.22f;
    cfg.topPadFrac = 0.17f;
    cfg.headlineFont = 21.0f;
    cfg.bodyFont = 15.5f;
    cfg.bodyMaxWidth = 540.0f;
    cfg.bodyHeight = 84.0f;
    cfg.bodyMaxLines = 4;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff090c12u,
                         makeAboutConfig()),
      processor_(processor) {}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        const auto ext = juce::File(path).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac")
            return true;
    }
    return false;
}

void PluginEditor::filesDropped(const juce::StringArray& files, int, int) {
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    for (const auto& path : files) {
        const juce::File f(path);
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac") {
            if (processor_.printStore.loadAudioFile(f, sr)) {
                if (auto* p = processor_.state.getParameter("genMix")) {
                    if (p->getValue() < 0.05f) {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(0.85f);
                        p->endChangeGesture();
                    }
                }
                break;
            }
        }
    }
}

} // namespace ln
