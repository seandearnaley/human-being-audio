// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/ui/model.h"

#include <hba/ui/movement_edit.h>

namespace cl::ui {

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);
Curve startingCurve(LaneShape shape, int lane);

namespace edit {
using namespace hba::ui::edit;
} // namespace edit

} // namespace cl::ui
