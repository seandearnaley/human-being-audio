// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "StudioAbout.h"
#include "cl/ui/editor.h"

#include <hba/host/visage_surface.h>

namespace cl {

class PluginEditor final : public juce::AudioProcessorEditor {
  public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void showAbout();
    void parentHierarchyChanged() override;

  private:
    void attachWhenOnScreen();
    void detachView();

    PluginModel model_;
    hba::host::VisageSurface nativeHost_;
    ui::Editor view_;
    struct AboutPage : hba::StudioAbout {
        AboutPage();
        void paint(juce::Graphics& g) override;
        std::unique_ptr<juce::Drawable> wordmark, monogram;
    } about_;
    void* attachedTo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace cl
