#pragma once

#include "PluginModel.h"
#include "StudioAbout.h"
#include "tf/ui/editor.h"

namespace tf {

// A JUCE editor that owns no pixels: it hands its native view to the Visage
// interface and forwards size changes.
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
#if JUCE_LINUX
    juce::XEmbedComponent nativeHost_ { juce::XEmbedComponentOptions{} };
#elif JUCE_WINDOWS
    juce::HWNDComponent nativeHost_;
#endif
    ui::Editor view_;
    hba::StudioAbout about_;
    juce::Label aboutText_;
    void* attachedTo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

}
