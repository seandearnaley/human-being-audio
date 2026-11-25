// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace sb {

struct PresetData {
    std::string name = "Neuro Crack 185Hz";
    std::string category = "Snare";
    Params params = Params::defaults();
};

std::optional<PresetData> parsePresetJson(std::string_view json);
std::string serialisePresetJson(const PresetData& preset);

} // namespace sb
