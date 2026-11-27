// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "PluginProcessor.h"
#include "dt/ui/editor.h"

#include <hba/host/plugin_editor.h>

namespace dt {

class PluginEditor final : public hba::host::StudioPluginEditor<PluginProcessor, PluginModel, ui::Editor>,
                           public juce::FileDragAndDropTarget {
  public:
    explicit PluginEditor(PluginProcessor& processor);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

  private:
    PluginProcessor& processor_;
};

} // namespace dt
