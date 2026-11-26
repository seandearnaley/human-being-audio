// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/params.h"

#include <cstdint>
#include <random>
#include <string_view>

namespace sb {

enum class Archetype : std::uint8_t { snare = 0, kick = 1, clap = 2, metal = 3, tom = 4, perc = 5 };

struct ArchetypeRecipe {
    std::string_view name;
    Params params;
};

// Pure headless sound-design quick-start recipe for each Strike Body archetype
// (pre-auto-align; callers in engine/plugin can run solveAutoAlign on the returned Params).
ArchetypeRecipe makeArchetypeRecipe(Archetype kind) noexcept;

// Musical in-bounds parameter mutation around an existing Strike Body sound
// (pre-auto-align; deterministic for a given RNG state).
Params mutateParams(const Params& base, std::mt19937& rng) noexcept;

} // namespace sb
