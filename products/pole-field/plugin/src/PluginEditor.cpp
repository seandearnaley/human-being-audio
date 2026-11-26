#include "PluginEditor.h"

#include <embedded/pf_brand.h>

namespace pf {

namespace {
hba::host::StudioAboutConfig makeAboutConfig() {
    hba::host::StudioAboutConfig cfg;
    cfg.wordmarkData = ui::brand::pole_field_logotype_on_dark_svg.data;
    cfg.wordmarkSize = static_cast<std::size_t>(ui::brand::pole_field_logotype_on_dark_svg.size);
    cfg.monogramData = ui::brand::pole_field_mark_on_dark_svg.data;
    cfg.monogramSize = static_cast<std::size_t>(ui::brand::pole_field_mark_on_dark_svg.size);
    cfg.accessibilitySuffix = ". 14-pole Z-plane morphing filterbank.";
    cfg.headline = "14-pole Z-plane morphing filterbank";
    cfg.body =
        "Seven cascaded pole-zero sections interpolated in polar coordinates across 3D morph cubes, with inter-stage "
        "ADAA drive, Burg LPC audio imprinting and mono sub lock.";
    cfg.backgroundArgb = 0xff0b0e13u;
    cfg.bodyHeight = 70.0f;
    cfg.bodyMaxLines = 3;
    return cfg;
}
} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : StudioPluginEditor(processor, ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth, ui::kDefaultHeight, 0xff0b0e13u,
                         makeAboutConfig()) {}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        const auto ext = juce::File(path).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac")
            return true;
    }
    return false;
}

void PluginEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(ui::kDefaultWidth);
    const float padX = 24.0f * scale;
    const float padY = 100.0f * scale;
    const float padW = 344.0f * scale;
    const float padH = 408.0f * scale;

    int slot = 0;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    if (fx >= padX && fx <= padX + padW && fy >= padY && fy <= padY + padH) {
        const int col = (fx >= padX + 0.5f * padW) ? 1 : 0;
        const int row = (fy >= padY + 0.5f * padH) ? 1 : 0;
        slot = row * 2 + col;
    }

    for (const auto& path : files) {
        const juce::File file(path);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac") {
            model_.imprintFileToSlot(file, slot);
            return;
        }
    }
}

} // namespace pf
