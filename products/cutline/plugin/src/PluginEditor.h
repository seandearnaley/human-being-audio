// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "StudioAbout.h"
#include "cl/ui/editor.h"

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
#if JUCE_MAC
    struct MacSurface : juce::NSViewComponent {
        MacSurface();
    } nativeHost_;
#elif JUCE_LINUX
    juce::XEmbedComponent nativeHost_{juce::XEmbedComponentOptions {}};
#elif JUCE_WINDOWS
    juce::HWNDComponent nativeHost_;
#endif
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
