// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginModel.h"
#include "lg/ui/editor.h"

#include <hba/host/plugin_editor.h>

namespace lg {

class PluginEditor final : public hba::host::StudioPluginEditor<PluginProcessor, PluginModel, ui::Editor> {
  public:
    explicit PluginEditor(PluginProcessor& processor);
};

} // namespace lg
