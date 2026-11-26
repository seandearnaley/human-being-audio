#pragma once

#include "PluginModel.h"
#include "pf/ui/editor.h"

#include <hba/host/plugin_editor.h>

namespace pf {

class PluginEditor final : public hba::host::StudioPluginEditor<PluginProcessor, PluginModel, ui::Editor>,
                           public juce::FileDragAndDropTarget {
  public:
    explicit PluginEditor(PluginProcessor& processor);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
};

} // namespace pf
