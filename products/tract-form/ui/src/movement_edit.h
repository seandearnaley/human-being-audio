// What the movement panel needs that does not touch a window: reading the lanes
// from the model, the curve a built-in shape starts from, and shared curve-editing geometry.
#pragma once

#include "tf/lanes.h"
#include "tf/ui/model.h"

#include <hba/ui/movement_edit.h>

namespace tf::ui {

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);
Curve startingCurve(LaneShape shape, int lane);

namespace edit {
using namespace hba::ui::edit;
}

}
