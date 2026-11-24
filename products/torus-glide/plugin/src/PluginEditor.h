// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "PluginProcessor.h"
#include "StudioAbout.h"
#include "tg/ui/editor.h"

#include <hba/host/visage_surface.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace tg {

class PluginEditor final : public juce::AudioProcessorEditor {
  public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;

  private:
    struct AboutPage final : hba::StudioAbout {
        AboutPage();
        void paint(juce::Graphics& g) override;
        std::unique_ptr<juce::Drawable> wordmark, monogram;
    };

    void attachWhenOnScreen();
    void detachView();
    void showAbout();

    PluginModel model_;
    ui::Editor view_;
    hba::host::VisageSurface nativeHost_;
    AboutPage about_;
    void* attachedTo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace tg
