#pragma once

#include "PluginModel.h"
#include "StudioAbout.h"
#include "tf/ui/editor.h"

#include <hba/host/visage_surface.h>

namespace tf {

// The native instrument and studio About page occupy the same editor bounds.
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

}
