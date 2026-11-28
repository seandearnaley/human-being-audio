// SPDX-License-Identifier: AGPL-3.0-or-later
// The Ligature song: up to four looping sections in a ring, a bridge between each neighbour, and four parts.
#pragma once

#include "lg/routes.h"
#include "lg/theory.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

inline constexpr int kMaxSections = 4;
inline constexpr int kMaxBridges = kMaxSections;
inline constexpr int kMaxSectionBars = 16;
inline constexpr int kMaxBridgeBars = 4;
inline constexpr int kStepsPerBar = 16;

// Parts in MIDI-channel order: chords on channel 1, bass 2, melody 3, arp 4.
enum class Role : std::uint8_t { chords, bass, melody, arp, count };
inline constexpr int kRoleCount = static_cast<int>(Role::count);
constexpr int midiChannel(Role r) noexcept { return static_cast<int>(r) + 1; }
std::string_view roleName(Role r) noexcept;

// Style names per role; a part stores its style as an index into this list. The last melody and arp styles are Off.
std::span<const std::string_view> styleNames(Role r) noexcept;
bool styleIsOff(Role r, int style) noexcept;
bool rateApplies(Role r,
                 int style) noexcept; // patterned styles only; Hold, Push, Anchor, Counter and melody ignore Rate

struct PartSettings {
    int style = 0;
    float density = 0.6f;
    int reg = 0;             // octave offset -1..1
    float rate = 1.0f;       // 0.5, 1 or 2
    std::uint32_t seedR = 1; // rhythm seed: Twist and Rewrite change it
    std::uint32_t seedP = 1; // pitch seed: Nudge and Rewrite change it
    bool mute = false;
    bool solo = false;
    bool operator==(const PartSettings&) const = default;
};

enum class Stutter : std::uint8_t { none, r32, r64, gate };
std::string_view stutterString(Stutter s) noexcept;
using StutterCells = std::map<int, Stutter>; // step -> cell

struct ItemRef {
    enum class Kind : std::uint8_t { section, bridge };
    Kind kind = Kind::section;
    int index = 0;
    bool isBridge() const noexcept { return kind == Kind::bridge; }
    static ItemRef section(int i) noexcept { return {Kind::section, i}; }
    static ItemRef bridge(int i) noexcept { return {Kind::bridge, i}; }
    auto operator<=>(const ItemRef&) const = default;
};

struct Section {
    std::string name;
    Key key;
    int bars = 4;
    std::vector<Chord> chords; // beats sum to bars * 4
    StutterCells stutter;
    // Who this section is while the plugin runs, so playback stays on it when another section is added or removed.
    // normalise() gives new sections the next free number; it is not saved and not part of equality.
    std::uint32_t uid = 0;
    bool operator==(const Section& o) const {
        return name == o.name && key == o.key && bars == o.bars && chords == o.chords && stutter == o.stutter;
    }
};

struct Bridge {
    int from = 0;
    int to = 1;
    int bars = 2;
    RouteId route = RouteId::backdoor;
    StutterCells stutter;
    bool operator==(const Bridge&) const = default;
};

struct Pin {
    ItemRef item;
    Role role = Role::melody;
    float start = 0.0f; // steps
    float dur = 1.0f;
    int pitch = 60;
    bool operator==(const Pin&) const = default;
};

struct Song {
    std::string name = "Untitled";
    std::string category = "User";
    double bpm = 122.0; // preview tempo; the host's tempo wins while it plays
    bool voiceLead = true;
    std::vector<Section> sections;
    std::vector<Bridge> bridges; // bridges[i] joins sections[i] to sections[(i + 1) % n]
    std::array<PartSettings, kRoleCount> parts{};
    std::vector<Pin> pins;
    bool operator==(const Song&) const = default;

    const PartSettings& part(Role r) const noexcept { return parts[static_cast<std::size_t>(r)]; }
    PartSettings& part(Role r) noexcept { return parts[static_cast<std::size_t>(r)]; }
};

// One bridge per neighbour in the ring (none for a single section), keeping each pair's existing settings.
void normaliseBridges(Song& song);
// Makes every section's chords fill exactly its bars, clamps sizes and settings to their ranges.
void normalise(Song& song);

std::vector<ItemRef> stripOrder(const Song& song);   // A, A->B, B, B->A ...
std::string itemKey(const Song& song, ItemRef item); // "A", "AB": stable seeds and labels
std::string sectionLetter(int index);
int itemBars(const Song& song, ItemRef item);
const StutterCells& itemStutter(const Song& song, ItemRef item);
StutterCells& itemStutter(Song& song, ItemRef item);

std::string serialiseSong(const Song& song);
std::optional<Song> parseSong(std::string_view json);

Song defaultSong(); // the Neon Pivot demo: F minor garage verse, backdoor bridge, C major chorus

} // namespace lg
