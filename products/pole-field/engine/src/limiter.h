#pragma once
#include "control.h"
#include <hba/dsp/limiter.h>
namespace pf::dsp {
using hba::dsp::Limiter;
using hba::dsp::MovingMinimum;
using hba::dsp::TruePeakDetector;
using hba::dsp::kSoftClipKnee;
using hba::dsp::softClip;
using hba::dsp::softClipLinked;
}
