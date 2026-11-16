// A preset is one flat JSON object: parameter key to value in real units, plus
// "name" and "category". Choices are stored by label, toggles as booleans.
// Missing keys take the default, so old presets keep loading as parameters are added.
#pragma once

#include "tf/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace tf {

struct Preset {
    std::string name;
    std::string category;
    Params params = Params::defaults();
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

}
