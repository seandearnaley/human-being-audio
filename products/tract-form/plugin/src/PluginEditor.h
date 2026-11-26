#pragma once

#include "PluginModel.h"
#include "tf/ui/editor.h"

#include <hba/host/plugin_editor.h>

namespace tf {

// The native instrument and studio About page occupy the same editor bounds.
class PluginEditor final : public hba::host::StudioPluginEditor<PluginProcessor, PluginModel, ui::Editor> {
  public:
    explicit PluginEditor(PluginProcessor& processor);
};

} // namespace tf
