// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/lanes.h"

#include <hba/ui/movement_edit.h>

namespace ln::ui {
namespace edit = hba::ui::edit;
Curve startingCurve(LaneShape shape, int lane);
} // namespace ln::ui
