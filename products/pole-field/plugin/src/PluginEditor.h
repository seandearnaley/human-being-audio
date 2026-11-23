#pragma once

#include "PluginModel.h"
#include "StudioAbout.h"
#include "pf/ui/editor.h"

#include <hba/host/visage_surface.h>

namespace pf {

class PluginEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget {
  public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void showAbout();
    void parentHierarchyChanged() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

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
