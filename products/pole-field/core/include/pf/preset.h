// Preset loading and saving as JSON, including custom movement curves and
// optional Burg LPC imprinted corner data.
#pragma once

#include "pf/cubes.h"
#include "pf/lanes.h"
#include "pf/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

struct Preset {
    std::string name;
    std::string category;
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    ImprintData imprint {};
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

std::optional<ImprintData> parseImprint(std::string_view json);
std::string serialiseImprint(const ImprintData& imprint);

}
