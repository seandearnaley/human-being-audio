// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "PluginProcessor.h"
#include "lc/ui/editor.h"

#include <hba/host/plugin_editor.h>

struct LivecutAboutPage final : hba::StudioAbout {
    LivecutAboutPage();
    void paint(juce::Graphics& g) override;
};

class LivecutAudioProcessorEditor final
    : public hba::host::StudioPluginEditor<LivecutAudioProcessor, livecut::PluginModel, lc::ui::Editor,
                                           LivecutAboutPage> {
  public:
    static constexpr int baseWidth = lc::ui::kDefaultWidth;
    static constexpr int baseHeight = lc::ui::kDefaultHeight;

    explicit LivecutAudioProcessorEditor(LivecutAudioProcessor& processor);
};
