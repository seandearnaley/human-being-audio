// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "PluginProcessor.h"
#include "StudioAbout.h"
#include "lc/ui/editor.h"

#include <hba/host/visage_surface.h>

class LivecutAudioProcessorEditor final : public juce::AudioProcessorEditor {
  public:
    static constexpr int baseWidth = lc::ui::kDefaultWidth;
    static constexpr int baseHeight = lc::ui::kDefaultHeight;

    explicit LivecutAudioProcessorEditor(LivecutAudioProcessor& processor);
    ~LivecutAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void showAbout();
    void parentHierarchyChanged() override;

  private:
    void attachWhenOnScreen();
    void detachView();

    livecut::PluginModel model_;
    hba::host::VisageSurface nativeHost_;
    lc::ui::Editor view_;

    struct AboutPage final : hba::StudioAbout {
        AboutPage();
        void paint(juce::Graphics& g) override;
    } about_;

    void* attachedTo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivecutAudioProcessorEditor)
};
