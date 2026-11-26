// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <optional>
#include <string>
#include <string_view>

namespace cl {

struct Preset {
    std::string name;
    std::string category;
    Params params;
    LaneCurves curves = defaultLaneCurves();
    PatternBank bank{};
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

std::optional<PatternBank> parsePatternBank(std::string_view json);
std::string serialisePatternBank(const PatternBank& bank);

} // namespace cl
