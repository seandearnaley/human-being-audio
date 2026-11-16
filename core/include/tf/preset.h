// A preset is one flat JSON object: parameter key to value in real units, plus
// "name" and "category". Choices are stored by label, toggles as booleans.
// Missing keys take the default, so old presets keep loading as parameters are added.
// A lane whose shape is Custom also carries its drawn curve as "lane1Curve" (and
// so on): a list of [x, y, tension, "line" | "smooth" | "hold"] points.
#pragma once

#include "tf/lanes.h"
#include "tf/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace tf {

struct Preset {
    std::string name;
    std::string category;
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

// One curve on its own, in the same form a preset stores it. For session state.
std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

}
