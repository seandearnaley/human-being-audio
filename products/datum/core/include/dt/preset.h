// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/params.h"
#include "dt/pattern.h"

#include <hba/dsp/envelope_curve.h>

#include <optional>
#include <string>
#include <string_view>

namespace dt {

struct Preset {
    std::string name = "Datum Neuro 52Hz";
    std::string category = "Neurofunk";
    Params params = Params::defaults();
    hba::dsp::EnvelopeCurve pitchCurve = hba::dsp::defaultKickPitchCurve();
    hba::dsp::EnvelopeCurve ampCurve = hba::dsp::defaultKickAmpCurve();
    ComposePattern pattern = defaultComposePattern();
};

std::string serializePreset(const Preset& preset);
std::optional<Preset> parsePreset(std::string_view jsonText, std::string* errorOut = nullptr);

} // namespace dt
