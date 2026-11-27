// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/lanes.h"
#include "ln/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace ln {

struct Preset {
    std::string name = "Init";
    std::string category = "Reese";
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    bool autoPrint = false; // If true, auto-bakes a Gen 2 strata pass on load
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

} // namespace ln
