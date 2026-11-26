// SPDX-License-Identifier: AGPL-3.0-or-later
// Json-level groove codec, shared inside cl_core (preset.cpp embeds a bank in a preset).
#pragma once

#include "cl/groove.h"

#include <hba/core/preset_json.h>

#include <optional>
#include <string>

namespace cl::detail {

using Json = hba::core::Json;

std::optional<Pattern> readPattern(const Json& value, std::string* error);
Json writePattern(const Pattern& pattern);

std::optional<GrooveBank> readGrooveBank(const Json& value, std::string* error);
Json writeGrooveBank(const GrooveBank& bank);

} // namespace cl::detail
