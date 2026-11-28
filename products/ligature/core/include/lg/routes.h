// SPDX-License-Identifier: AGPL-3.0-or-later
// Modulation route solver: the chord paths a bridge can take from one key to another.
#pragma once

#include "lg/theory.h"

#include <optional>
#include <string_view>
#include <vector>

namespace lg {

enum class RouteId : std::uint8_t { pedal, pivot, backdoor, dominant, neapolitan, dim7, hexatonic, slam, count };

struct Route {
    RouteId id = RouteId::backdoor;
    std::string_view name;
    std::string_view why;
    int boldness = 1;          // 1 smooth .. 5 wild
    std::vector<Chord> chords; // written in 8 beats; a bridge rescales them to its own length
};

std::string_view routeIdString(RouteId id) noexcept;
std::optional<RouteId> routeFromString(std::string_view s) noexcept;

// Every family that applies to the pair, smooth to bold. The pivot family appears only when a chord is diatonic to
// both keys; the common-tone family only when the old key already holds the new tonic or its dominant.
std::vector<Route> routesFor(Key from, Key to);

// The requested route, or the first available one when the pair has no such family.
Route routeOrFirst(Key from, Key to, RouteId wanted);

} // namespace lg
