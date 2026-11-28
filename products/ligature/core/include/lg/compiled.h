// SPDX-License-Identifier: AGPL-3.0-or-later
// The song as the audio thread sees it: fixed-size, trivially copyable, every bridge pre-generated at every length
// it can be squeezed to. Compiled on the message thread; the engine only reads it.
#pragma once

#include "lg/generate.h"
#include "lg/nonblocking.h"
#include "lg/song.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace lg {

inline constexpr int kMaxCompiledNotes = 4096;

struct CompiledNote {
    float start = 0.0f; // steps
    float end = 0.0f;
    std::uint8_t pitch = 60;
    std::uint8_t velocity = 100; // 1..127
    std::uint8_t role = 0;
    std::uint8_t span = 0;
};

struct CompiledPattern {
    int bars = 0;
    int count = 0;
    int spanCount = 0;
    bool slamTail = false; // the route ends in a gate: drums drop for the last beat
    std::array<float, 32> spanStarts{};
    std::array<CompiledNote, kMaxCompiledNotes> notes{}; // sorted by start
};

struct CompiledBridge {
    int from = 0;
    int to = 1;
    int bars = 2;
    std::array<CompiledPattern, kMaxBridgeBars> fits{}; // fits[f - 1]: the bridge squeezed to f bars
};

// The key's own chords for one-key playing: white keys C..B play degrees 1-7.
struct KeyChords {
    std::array<std::array<std::uint8_t, 6>, 7> notes{};
    std::array<std::uint8_t, 7> count{};
};

struct CompiledSong {
    std::uint64_t revision = 0;
    float bpm = 122.0f;
    int sectionCount = 0;
    int bridgeCount = 0;
    std::array<bool, kRoleCount> audible{true, true, true, true}; // mute and solo, applied at output
    std::array<CompiledPattern, kMaxSections> sections{};
    std::array<CompiledBridge, kMaxBridges> bridges{};
    std::array<KeyChords, kMaxSections> keyChords{};
    std::array<std::uint32_t, kMaxSections> sectionUid{}; // Section::uid, for following a section across edits

    // Bridge at `fit` bars (clamped to 1..its length); sections ignore `fit`.
    const CompiledPattern& pattern(ItemRef item, int fit = 0) const noexcept LG_NONBLOCKING;
    int barsOf(ItemRef item) const noexcept LG_NONBLOCKING;
    int bridgeIndex(int from, int to) const noexcept LG_NONBLOCKING; // -1 when the pair has no bridge
};

static_assert(std::is_trivially_copyable_v<CompiledSong>);

void compilePattern(const Pattern& pattern, CompiledPattern& out);
void compileSong(const Song& song, CompiledSong& out, std::uint64_t revision);

} // namespace lg
