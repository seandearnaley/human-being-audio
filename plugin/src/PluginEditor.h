#pragma once

#include "PluginModel.h"
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
    void parentHierarchyChanged() override;

private:
    void attachWhenOnScreen();

    PluginModel model_;
    ui::Editor view_;
    void* attachedTo_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

}
