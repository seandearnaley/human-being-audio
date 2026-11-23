// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/overrides.h"
#include "cl/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace cl {

struct Preset {
    std::string name;
    std::string category;
    Params params;
    LaneCurves curves = defaultLaneCurves();
    PhraseOverrides overrides{};
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

std::optional<PhraseOverrides> parseOverrides(std::string_view json);
std::string serialiseOverrides(const PhraseOverrides& overrides);

} // namespace cl
