// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/dt_brand.h>

namespace dt {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::datum_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::datum_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::datum_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::datum_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". Phase-locked neurofunk kick synthesizer.";
    cfg.headline = "Phase-locked neurofunk kick drum synthesizer";
    cfg.body = "Four pure-DSP kick cores (Sine, Twin-T, Schroeder Harmonic, Berger Membrane + Helmholtz Cavity), "
               "analytical pitch & amp splines with Zero-X tail snap, phase-native 3-band sculpting, 8x ADAA2 "
               "shaping, and Sub-Link live sidechain bass alignment.";
    cfg.backgroundArgb = 0xff090c12u;
    cfg.wordmarkWidth = 250.0f;
    cfg.wordmarkHeight = 66.0f;
    cfg.iconMaxSize = 200.0f;
    cfg.iconWidthFrac = 0.22f;
    cfg.topPadFrac = 0.17f;
    cfg.headlineFont = 21.0f;
    cfg.bodyFont = 15.5f;
    cfg.bodyMaxWidth = 540.0f;
    cfg.bodyHeight = 88.0f;
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
    const auto bundle = processor_.stateStore.all();
    for (const auto& path : files) {
        const juce::File f(path);
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac") {
            if (processor_.clickStore.loadAudioFile(f, processor_.currentParams(), bundle.pitchCurve,
                                                    bundle.ampCurve)) {
                if (auto* p = processor_.state.getParameter("clickWavMix")) {
                    if (p->getValue() < 0.05f) {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(0.65f);
                        p->endChangeGesture();
                    }
                }
                break;
            }
        }
    }
}

} // namespace dt
