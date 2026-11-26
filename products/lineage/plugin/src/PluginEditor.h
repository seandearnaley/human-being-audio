// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "PluginProcessor.h"
#include "ln/ui/editor.h"

#include <hba/host/plugin_editor.h>

namespace ln {

class PluginEditor final : public hba::host::StudioPluginEditor<PluginProcessor, PluginModel, ui::Editor> {
  public:
    explicit PluginEditor(PluginProcessor& processor);
};

} // namespace ln
