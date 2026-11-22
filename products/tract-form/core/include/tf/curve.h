// A drawn movement curve: delegates to the shared studio definition in modules/dsp.
#pragma once

#include <hba/dsp/curve.h>

namespace tf {

using hba::dsp::addPoint;
using hba::dsp::Curve;
using hba::dsp::CurvePoint;
using hba::dsp::evaluate;
using hba::dsp::kMaxCurvePoints;
using hba::dsp::movePoint;
using hba::dsp::removePoint;
using hba::dsp::Segment;

namespace curves {
using hba::dsp::curves::make;
using hba::dsp::curves::rampDown;
using hba::dsp::curves::rampUp;
using hba::dsp::curves::sine;
using hba::dsp::curves::square;
using hba::dsp::curves::triangle;
} // namespace curves

} // namespace tf
