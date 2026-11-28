// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/song.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace lg {

namespace {

using json = nlohmann::json;

constexpr std::array<std::string_view, 5> kChordStyles{"Hold", "Stabs", "2-Step", "Push", "Pulse"};
constexpr std::array<std::string_view, 4> kBassStyles{"Anchor", "Garage", "Offbeat", "Rolling"};
constexpr std::array<std::string_view, 4> kMelodyStyles{"Motif", "Hook", "Answer", "Off"};
constexpr std::array<std::string_view, 4> kArpStyles{"Up 16ths", "Pluck 8ths", "Counter", "Off"};
constexpr std::array<std::string_view, kRoleCount> kRoleNames{"Chords", "Bass", "Melody", "Arp"};
constexpr std::array<std::string_view, 4> kStutterNames{"none", "r32", "r64", "gate"};
constexpr std::array<std::string_view, 3> kVoicingNames{"close", "drop2", "spread"};
constexpr std::array<std::string_view, 7> kFamilyNames{"maj", "min", "dom", "sus", "hdim", "dim", "aug"};

int parsePc(const json& j, int fallback) {
    if (j.is_number_integer())
        return mod12(j.get<int>());
    if (!j.is_string())
        return fallback;
    const auto s = j.get<std::string>();
    if (s.empty())
        return fallback;
    static constexpr std::array<int, 7> kLetters{9, 11, 0, 2, 4, 5, 7}; // A..G
    const char letter = static_cast<char>(s[0] & ~0x20);
    if (letter < 'A' || letter > 'G')
        return fallback;
    int pc = kLetters[static_cast<std::size_t>(letter - 'A')];
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] == 'b')
            --pc;
        else if (s[i] == '#')
            ++pc;
    }
    return mod12(pc);
}

template <std::size_t N> int indexIn(const std::array<std::string_view, N>& names, std::string_view s, int fallback) {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == s)
            return static_cast<int>(i);
    return fallback;
}

json stutterJson(const StutterCells& cells) {
    json out = json::object();
    for (const auto& [step, cell] : cells)
        if (cell != Stutter::none)
            out[std::to_string(step)] = std::string(stutterString(cell));
    return out;
}

StutterCells parseStutter(const json& j) {
    StutterCells cells;
    if (!j.is_object())
        return cells;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_string())
            continue;
        const int cell = indexIn(kStutterNames, it.value().get<std::string>(), 0);
        if (cell > 0)
            cells[std::atoi(it.key().c_str())] = static_cast<Stutter>(cell);
    }
    return cells;
}

json keyJson(Key k) { return {{"tonic", pcName(k.tonic)}, {"mode", k.mode == Mode::major ? "major" : "minor"}}; }

Key parseKey(const json& j) {
    Key k;
    if (!j.is_object())
        return k;
    k.tonic = parsePc(j.value("tonic", json("C")), 0);
    k.mode = j.value("mode", std::string("major")) == "minor" ? Mode::minor : Mode::major;
    return k;
}

json chordJson(const Chord& c) {
    json j = {{"root", pcName(c.root)}, {"quality", std::string(qualityInfo(c.quality).id)}, {"beats", c.beats}};
    if (c.bass >= 0 && c.bass != c.root)
        j["bass"] = pcName(c.bass);
    if (c.style != VoicingStyle::close)
        j["voicing"] = std::string(kVoicingNames[static_cast<std::size_t>(c.style)]);
    if (c.inversion != 0)
        j["inversion"] = c.inversion;
    if (c.family != qualityInfo(c.quality).family)
        j["family"] = std::string(kFamilyNames[static_cast<std::size_t>(c.family)]);
    return j;
}

std::optional<Chord> parseChord(const json& j) {
    if (!j.is_object())
        return std::nullopt;
    const auto q = qualityFromId(j.value("quality", std::string("maj")));
    if (!q)
        return std::nullopt;
    Chord c = makeChord(parsePc(j.value("root", json("C")), 0), *q, j.value("beats", 4.0f),
                        j.contains("bass") ? parsePc(j["bass"], -1) : -1);
    c.style = static_cast<VoicingStyle>(indexIn(kVoicingNames, j.value("voicing", std::string("close")), 0));
    c.inversion = std::clamp(j.value("inversion", 0), -2, 2);
    if (j.contains("family") && j["family"].is_string())
        c.family =
            static_cast<Family>(indexIn(kFamilyNames, j["family"].get<std::string>(), static_cast<int>(c.family)));
    return c;
}

json itemJson(ItemRef r) { return {{"kind", r.isBridge() ? "bridge" : "section"}, {"index", r.index}}; }

ItemRef parseItem(const json& j) {
    ItemRef r;
    if (j.is_object()) {
        r.kind = j.value("kind", std::string("section")) == "bridge" ? ItemRef::Kind::bridge : ItemRef::Kind::section;
        r.index = j.value("index", 0);
    }
    return r;
}

void fitChords(Section& s) {
    const float total = static_cast<float>(s.bars * 4);
    if (s.chords.empty())
        s.chords.push_back(makeChord(s.key.tonic, s.key.mode == Mode::major ? Quality::maj9 : Quality::m9, total));
    float at = 0.0f;
    std::vector<Chord> out;
    for (auto c : s.chords) {
        if (at >= total - 1e-4f)
            break;
        c.beats = std::max(0.25f, std::round(c.beats * 4.0f) / 4.0f);
        c.beats = std::min(c.beats, total - at);
        at += c.beats;
        out.push_back(c);
    }
    if (at < total - 1e-4f)
        out.back().beats += total - at;
    s.chords = std::move(out);
}

} // namespace

std::string_view roleName(Role r) noexcept { return kRoleNames[static_cast<std::size_t>(r)]; }

std::span<const std::string_view> styleNames(Role r) noexcept {
    switch (r) {
    case Role::chords:
        return kChordStyles;
    case Role::bass:
        return kBassStyles;
    case Role::melody:
        return kMelodyStyles;
    case Role::arp:
        return kArpStyles;
    default:
        return {};
    }
}

bool styleIsOff(Role r, int style) noexcept {
    const auto names = styleNames(r);
    return style >= 0 && static_cast<std::size_t>(style) < names.size() &&
           names[static_cast<std::size_t>(style)] == "Off";
}

bool rateApplies(Role r, int style) noexcept {
    switch (r) {
    case Role::chords:
        return style == 1 || style == 2 || style == 4;
    case Role::bass:
        return style >= 1;
    case Role::arp:
        return style == 0 || style == 1;
    default:
        return false;
    }
}

std::string_view stutterString(Stutter s) noexcept { return kStutterNames[static_cast<std::size_t>(s)]; }

std::string sectionLetter(int index) { return std::string(1, static_cast<char>('A' + std::clamp(index, 0, 25))); }

void normaliseBridges(Song& song) {
    const int n = static_cast<int>(song.sections.size());
    std::vector<Bridge> out;
    std::vector<int> newIndex(song.bridges.size(), -1); // old bridge index -> its index in the rebuilt ring
    if (n >= 2) {
        for (int i = 0; i < n; ++i) {
            const int to = (i + 1) % n;
            auto it = std::find_if(song.bridges.begin(), song.bridges.end(),
                                   [&](const Bridge& b) { return b.from == i && b.to == to; });
            if (it != song.bridges.end()) {
                newIndex[static_cast<std::size_t>(it - song.bridges.begin())] = i;
                out.push_back(*it);
            } else {
                Bridge b;
                b.from = i;
                b.to = to;
                const bool closing = to == 0;
                b.bars = closing ? 1 : 2;
                b.route = closing ? RouteId::dominant : RouteId::backdoor;
                out.push_back(b);
            }
        }
    }
    song.bridges = std::move(out);
    // A pin belongs to the bridge between two sections, not to a slot in the ring: it moves with its bridge, and goes
    // when its bridge does (adding a section splits the closing bridge).
    std::erase_if(song.pins, [&](const Pin& p) {
        return p.item.isBridge() && (p.item.index < 0 || static_cast<std::size_t>(p.item.index) >= newIndex.size() ||
                                     newIndex[static_cast<std::size_t>(p.item.index)] < 0);
    });
    for (auto& p : song.pins)
        if (p.item.isBridge())
            p.item.index = newIndex[static_cast<std::size_t>(p.item.index)];
}

void normalise(Song& song) {
    if (song.sections.empty()) {
        Section s;
        s.name = "Loop";
        s.key = {9, Mode::minor};
        song.sections.push_back(s);
    }
    if (song.sections.size() > static_cast<std::size_t>(kMaxSections))
        song.sections.resize(static_cast<std::size_t>(kMaxSections));
    for (auto& s : song.sections) {
        s.bars = std::clamp(s.bars, 1, kMaxSectionBars);
        s.key.tonic = mod12(s.key.tonic);
        fitChords(s);
        std::erase_if(s.stutter, [&](const auto& kv) { return kv.first < 0 || kv.first >= s.bars * kStepsPerBar; });
    }
    std::uint32_t nextUid = 1;
    for (const auto& sec : song.sections)
        nextUid = std::max(nextUid, sec.uid + 1);
    for (auto& sec : song.sections)
        if (sec.uid == 0 || std::count_if(song.sections.begin(), song.sections.end(),
                                          [&](const Section& o) { return o.uid == sec.uid; }) > 1)
            sec.uid = nextUid++;
    normaliseBridges(song);
    for (auto& b : song.bridges) {
        b.bars = std::clamp(b.bars, 1, kMaxBridgeBars);
        std::erase_if(b.stutter, [&](const auto& kv) { return kv.first < 0 || kv.first >= b.bars * kStepsPerBar; });
    }
    for (int r = 0; r < kRoleCount; ++r) {
        auto& p = song.parts[static_cast<std::size_t>(r)];
        p.style = std::clamp(p.style, 0, static_cast<int>(styleNames(static_cast<Role>(r)).size()) - 1);
        p.density = std::clamp(p.density, 0.0f, 1.0f);
        p.reg = std::clamp(p.reg, -1, 1);
        p.rate = p.rate < 0.75f ? 0.5f : (p.rate > 1.5f ? 2.0f : 1.0f);
    }
    song.bpm = std::clamp(std::isfinite(song.bpm) ? song.bpm : 122.0, 40.0, 220.0);
    const auto validItem = [&](ItemRef r) {
        const auto count = r.isBridge() ? song.bridges.size() : song.sections.size();
        return r.index >= 0 && static_cast<std::size_t>(r.index) < count;
    };
    std::erase_if(song.pins, [&](const Pin& p) { return !validItem(p.item) || p.pitch < 0 || p.pitch > 127; });
}

std::vector<ItemRef> stripOrder(const Song& song) {
    std::vector<ItemRef> out;
    for (int i = 0; i < static_cast<int>(song.sections.size()); ++i) {
        out.push_back(ItemRef::section(i));
        if (i < static_cast<int>(song.bridges.size()))
            out.push_back(ItemRef::bridge(i));
    }
    return out;
}

std::string itemKey(const Song& song, ItemRef item) {
    if (!item.isBridge())
        return sectionLetter(item.index);
    const auto& b = song.bridges[static_cast<std::size_t>(item.index)];
    return sectionLetter(b.from) + sectionLetter(b.to);
}

int itemBars(const Song& song, ItemRef item) {
    return item.isBridge() ? song.bridges[static_cast<std::size_t>(item.index)].bars
                           : song.sections[static_cast<std::size_t>(item.index)].bars;
}

const StutterCells& itemStutter(const Song& song, ItemRef item) {
    return item.isBridge() ? song.bridges[static_cast<std::size_t>(item.index)].stutter
                           : song.sections[static_cast<std::size_t>(item.index)].stutter;
}

StutterCells& itemStutter(Song& song, ItemRef item) {
    return item.isBridge() ? song.bridges[static_cast<std::size_t>(item.index)].stutter
                           : song.sections[static_cast<std::size_t>(item.index)].stutter;
}

std::string serialiseSong(const Song& song) {
    json j;
    j["format"] = 2;
    j["name"] = song.name;
    j["category"] = song.category;
    j["bpm"] = song.bpm;
    j["voiceLead"] = song.voiceLead;
    json sections = json::array();
    for (const auto& s : song.sections) {
        json chords = json::array();
        for (const auto& c : s.chords)
            chords.push_back(chordJson(c));
        sections.push_back({{"name", s.name},
                            {"key", keyJson(s.key)},
                            {"bars", s.bars},
                            {"chords", chords},
                            {"stutter", stutterJson(s.stutter)}});
    }
    j["sections"] = sections;
    json bridges = json::array();
    for (const auto& b : song.bridges)
        bridges.push_back({{"from", b.from},
                           {"to", b.to},
                           {"bars", b.bars},
                           {"route", std::string(routeIdString(b.route))},
                           {"stutter", stutterJson(b.stutter)}});
    j["bridges"] = bridges;
    json parts = json::object();
    for (int r = 0; r < kRoleCount; ++r) {
        const auto& p = song.parts[static_cast<std::size_t>(r)];
        const auto role = static_cast<Role>(r);
        parts[std::string(roleName(role))] = {
            {"style", std::string(styleNames(role)[static_cast<std::size_t>(p.style)])},
            {"density", p.density},
            {"register", p.reg},
            {"rate", p.rate},
            {"seedR", p.seedR},
            {"seedP", p.seedP},
            {"mute", p.mute},
            {"solo", p.solo}};
    }
    j["parts"] = parts;
    json pins = json::array();
    for (const auto& p : song.pins)
        pins.push_back({{"item", itemJson(p.item)},
                        {"role", std::string(roleName(p.role))},
                        {"start", p.start},
                        {"dur", p.dur},
                        {"pitch", p.pitch}});
    j["pins"] = pins;
    return j.dump(2);
}

namespace {
std::optional<Song> parseSongJson(std::string_view text);
} // namespace

// A field of the wrong type (a hand-edited preset, say) makes nlohmann throw: that song does not load, the host lives.
std::optional<Song> parseSong(std::string_view text) {
    try {
        return parseSongJson(text);
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

namespace {
std::optional<Song> parseSongJson(std::string_view text) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("sections") || !j["sections"].is_array())
        return std::nullopt;
    Song song;
    song.name = j.value("name", std::string("Untitled"));
    song.category = j.value("category", std::string("User"));
    song.bpm = j.value("bpm", 122.0);
    song.voiceLead = j.value("voiceLead", true);
    for (const auto& sj : j["sections"]) {
        if (!sj.is_object())
            continue;
        Section s;
        s.name = sj.value("name", std::string("Section"));
        s.key = parseKey(sj.value("key", json::object()));
        s.bars = sj.value("bars", 4);
        if (sj.contains("chords") && sj["chords"].is_array())
            for (const auto& cj : sj["chords"])
                if (auto c = parseChord(cj))
                    s.chords.push_back(*c);
        s.stutter = parseStutter(sj.value("stutter", json::object()));
        song.sections.push_back(std::move(s));
    }
    if (j.contains("bridges") && j["bridges"].is_array())
        for (const auto& bj : j["bridges"]) {
            if (!bj.is_object())
                continue;
            Bridge b;
            b.from = bj.value("from", 0);
            b.to = bj.value("to", 1);
            b.bars = bj.value("bars", 2);
            b.route = routeFromString(bj.value("route", std::string("backdoor"))).value_or(RouteId::backdoor);
            b.stutter = parseStutter(bj.value("stutter", json::object()));
            song.bridges.push_back(std::move(b));
        }
    if (j.contains("parts") && j["parts"].is_object())
        for (int r = 0; r < kRoleCount; ++r) {
            const auto role = static_cast<Role>(r);
            const auto name = std::string(roleName(role));
            if (!j["parts"].contains(name))
                continue;
            const auto& pj = j["parts"][name];
            auto& p = song.parts[static_cast<std::size_t>(r)];
            const auto names = styleNames(role);
            const auto style = pj.value("style", std::string(names[0]));
            p.style = 0;
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i] == style)
                    p.style = static_cast<int>(i);
            p.density = pj.value("density", 0.6f);
            p.reg = pj.value("register", 0);
            p.rate = pj.value("rate", 1.0f);
            p.seedR = pj.value("seedR", 1u);
            p.seedP = pj.value("seedP", 1u);
            p.mute = pj.value("mute", false);
            p.solo = pj.value("solo", false);
        }
    if (j.contains("pins") && j["pins"].is_array())
        for (const auto& pj : j["pins"]) {
            if (!pj.is_object())
                continue;
            Pin p;
            p.item = parseItem(pj.value("item", json::object()));
            const auto role = pj.value("role", std::string("Melody"));
            for (int r = 0; r < kRoleCount; ++r)
                if (roleName(static_cast<Role>(r)) == role)
                    p.role = static_cast<Role>(r);
            p.start = pj.value("start", 0.0f);
            p.dur = pj.value("dur", 1.0f);
            p.pitch = pj.value("pitch", 60);
            song.pins.push_back(p);
        }
    normalise(song);
    return song;
}
} // namespace

Song defaultSong() {
    Song song;
    song.name = "Neon Pivot";
    song.category = "Garage";
    song.bpm = 122.0;
    Section a;
    a.name = "Verse";
    a.key = {5, Mode::minor};
    a.bars = 4;
    a.chords = {makeChord(5, Quality::m9), makeChord(1, Quality::maj9), makeChord(10, Quality::m9),
                makeChord(0, Quality::d7b9)};
    a.stutter = {{60, Stutter::r32}, {61, Stutter::r32}, {62, Stutter::r64}, {63, Stutter::r64}};
    Section b;
    b.name = "Chorus";
    b.key = {0, Mode::major};
    b.bars = 4;
    b.chords = {makeChord(0, Quality::maj9), makeChord(7, Quality::add9, 4.0f, 11), makeChord(9, Quality::m9),
                makeChord(5, Quality::maj9)};
    b.stutter = {{44, Stutter::r32}, {62, Stutter::r32}, {63, Stutter::r64}};
    song.sections = {a, b};
    Bridge ab;
    ab.from = 0;
    ab.to = 1;
    ab.bars = 2;
    ab.route = RouteId::backdoor;
    Bridge ba;
    ba.from = 1;
    ba.to = 0;
    ba.bars = 1;
    ba.route = RouteId::dominant;
    song.bridges = {ab, ba};
    song.part(Role::melody) = {0, 0.62f, 0, 1.0f, 3, 2, false, false};
    song.part(Role::chords) = {1, 0.55f, 0, 1.0f, 1, 1, false, false};
    song.part(Role::arp) = {2, 0.7f, 0, 1.0f, 1, 1, false, false};
    song.part(Role::bass) = {1, 0.6f, 0, 1.0f, 1, 1, false, false};
    normalise(song);
    return song;
}

} // namespace lg
