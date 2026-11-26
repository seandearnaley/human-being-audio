// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/dsp/curve.h>

namespace ln {

using hba::dsp::addPoint;
using hba::dsp::Curve;
using hba::dsp::CurvePoint;
using hba::dsp::evaluate;
using hba::dsp::kMaxCurvePoints;
using hba::dsp::movePoint;
using hba::dsp::removePoint;
using hba::dsp::Segment;
namespace curves = hba::dsp::curves;

} // namespace ln
