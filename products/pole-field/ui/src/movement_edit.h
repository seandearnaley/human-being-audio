#pragma once

#include "pf/lanes.h"
#include "pf/ui/model.h"

#include <hba/ui/movement_edit.h>

namespace pf::ui {

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);
Curve startingCurve(LaneShape shape, int lane);

namespace edit {
using namespace hba::ui::edit;
}

}
