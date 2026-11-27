// SPDX-License-Identifier: AGPL-3.0-or-later
// JSON form of the Cutline 3 groove model. One form serves factory pattern files, presets
// and plugin state: step strings per layer, plus an "edits" list for what a character cannot say.
#pragma once

#include "cl/groove.h"

#include <optional>
#include <string>
#include <string_view>

namespace cl {

inline constexpr int kGrooveFormat = 3;
inline constexpr std::string_view kOldFormatError = "Cutline 2 preset: not supported in Cutline 3";

// Step characters: accent, normal, soft.
inline constexpr int kVelocityAccent = 118; // 'X'
inline constexpr int kVelocityNormal = 96;  // 'x'
inline constexpr int kVelocitySoft = 56;    // 'o'

// A standalone pattern file. Requires "format": 3 at the top level.
std::optional<Pattern> parsePattern(std::string_view json, std::string* error = nullptr, std::string* name = nullptr);
std::string serialisePattern(const Pattern& pattern, std::string_view name = {});

// A library fill file: {"format": 3, "name", "category", "beats", "lanes"}, where "beats" and "lanes" are written
// exactly as a pattern file's "fill" object.
std::optional<Fill> parseFill(std::string_view json, std::string* error = nullptr, std::string* name = nullptr);

// The top-level "category" of a library pattern or fill file; empty when absent or unparseable.
std::string libraryCategory(std::string_view json);

// The bank object stored inside presets and plugin state: {"scenes": [...], "slicer": {...}}.
std::optional<GrooveBank> parseGrooveBank(std::string_view json, std::string* error = nullptr);
std::string serialiseGrooveBank(const GrooveBank& bank);

} // namespace cl
