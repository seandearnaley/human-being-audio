// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/document.h"

#include <algorithm>
#include <cmath>

namespace lg {

namespace {

bool validSection(const Song& s, int i) { return i >= 0 && i < static_cast<int>(s.sections.size()); }
bool validBridge(const Song& s, int i) { return i >= 0 && i < static_cast<int>(s.bridges.size()); }
bool validChord(const Song& s, int section, int index) {
    return validSection(s, section) && index >= 0 &&
           index < static_cast<int>(s.sections[static_cast<std::size_t>(section)].chords.size());
}
bool validItem(const Song& s, ItemRef r) { return r.isBridge() ? validBridge(s, r.index) : validSection(s, r.index); }

constexpr std::array<std::string_view, 4> kDefaultNames{"Verse", "Chorus", "Bridge", "Drop"};

} // namespace

std::string_view mutationName(Mutation m) noexcept {
    switch (m) {
    case Mutation::nudge:
        return "Nudge";
    case Mutation::twist:
        return "Twist";
    case Mutation::rewrite:
        return "Rewrite";
    }
    return "";
}

Document::Document() : Document(defaultSong()) {}

Document::Document(Song song) : song_(std::move(song)) { normalise(song_); }

template <typename Fn> void Document::change(Fn&& fn, bool undoable) {
    if (undoable)
        history_.push(song_);
    fn(song_);
    normalise(song_);
    changed();
}

void Document::changed() {
    ++revision_;
    cache_.clear();
}

const Pattern& Document::pattern(ItemRef item, int fit) {
    if (!validItem(song_, item))
        item = ItemRef::section(0);
    const int full = itemBars(song_, item);
    const int key = item.isBridge() && fit > 0 && fit < full ? fit : 0;
    auto it = cache_.find({item, key});
    if (it == cache_.end())
        it = cache_.emplace(std::make_pair(item, key), buildPattern(song_, item, key)).first;
    return it->second;
}

Pattern Document::previewTake(ItemRef item, Role role, int take) const {
    auto parts = song_.parts;
    for (int r = 0; r < kRoleCount; ++r) {
        auto& p = parts[static_cast<std::size_t>(r)];
        if (static_cast<Role>(r) == role) {
            p.seedR += static_cast<std::uint32_t>(take * 13);
            p.seedP += static_cast<std::uint32_t>(take * 7);
        } else {
            p.style = static_cast<int>(styleNames(static_cast<Role>(r)).size()) - 1;
            if (!styleIsOff(static_cast<Role>(r), p.style))
                p.mute = true;
        }
    }
    auto pat = buildPattern(song_, validItem(song_, item) ? item : ItemRef::section(0), 0, &parts);
    std::erase_if(pat.notes, [role](const Note& n) { return n.role != role; });
    return pat;
}

std::vector<PaletteEntry> Document::palette(int section, int index) const {
    if (!validSection(song_, section))
        return {};
    return neighbourhood(song_.sections[static_cast<std::size_t>(section)], index);
}

std::vector<Route> Document::routes(int bridge) const {
    if (!validBridge(song_, bridge))
        return {};
    const auto& b = song_.bridges[static_cast<std::size_t>(bridge)];
    return routesFor(song_.sections[static_cast<std::size_t>(b.from)].key,
                     song_.sections[static_cast<std::size_t>(b.to)].key);
}

void Document::load(Song song, bool undoable) {
    ++loads_;
    change([&](Song& s) { s = std::move(song); }, undoable);
    if (!undoable)
        history_.clearHistory();
}

void Document::mirror(Song song) {
    change([&](Song& s) { s = std::move(song); }, false);
    history_.clearHistory();
}

void Document::undo() {
    if (auto s = history_.undo(song_)) {
        song_ = std::move(*s);
        changed();
    }
}

void Document::redo() {
    if (auto s = history_.redo(song_)) {
        song_ = std::move(*s);
        changed();
    }
}

void Document::toggleAB() {
    song_ = history_.toggleAB(song_);
    changed();
}

void Document::copyAB() { history_.copyActiveToOther(song_); }

void Document::replaceChord(int section, int index, const Chord& chord) {
    if (!validChord(song_, section, index))
        return;
    change([&](Song& s) {
        auto& c = s.sections[static_cast<std::size_t>(section)].chords[static_cast<std::size_t>(index)];
        c.root = chord.root;
        c.quality = chord.quality;
        c.bass = chord.bass;
        c.family = chord.family;
    });
}

void Document::setVoicingStyle(int section, int index, VoicingStyle style) {
    if (!validChord(song_, section, index))
        return;
    change([&](Song& s) {
        s.sections[static_cast<std::size_t>(section)].chords[static_cast<std::size_t>(index)].style = style;
    });
}

bool Document::stepInversion(int section, int index, int delta) {
    if (!validChord(song_, section, index))
        return false;
    const auto& c = song_.sections[static_cast<std::size_t>(section)].chords[static_cast<std::size_t>(index)];
    const int next = std::clamp(c.inversion + delta, -2, 2);
    if (next == c.inversion)
        return false;
    change([&](Song& s) {
        s.sections[static_cast<std::size_t>(section)].chords[static_cast<std::size_t>(index)].inversion = next;
    });
    return true;
}

void Document::setLevel(int section, int index, int level) {
    if (!validChord(song_, section, index))
        return;
    change([&](Song& s) {
        auto& sec = s.sections[static_cast<std::size_t>(section)];
        auto& c = sec.chords[static_cast<std::size_t>(index)];
        const auto a = analyze(c, sec.key);
        const bool harmonicV =
            sec.key.mode == Mode::minor && a.degree == 4 && (c.family == Family::dom || c.family == Family::maj);
        if (a.diatonic && a.degree >= 0 && !harmonicV) {
            const auto d = diatonic(sec.key, a.degree, level);
            c.quality = d.quality;
            c.family = d.family;
        } else if (hasLadder(c.family)) {
            c.quality = ladder(c.family, level);
        }
    });
}

void Document::setVoiceLead(bool on) {
    if (song_.voiceLead != on)
        change([&](Song& s) { s.voiceLead = on; });
}

void Document::setStyle(Role role, int style) {
    change([&](Song& s) { s.part(role).style = style; });
}

void Document::setDensity(Role role, float density, bool gestureStart) {
    if (gestureStart)
        history_.push(song_);
    song_.part(role).density = std::clamp(density, 0.0f, 1.0f);
    changed();
}

void Document::setRegister(Role role, int reg) {
    change([&](Song& s) { s.part(role).reg = reg; });
}

void Document::setRate(Role role, float rate) {
    change([&](Song& s) { s.part(role).rate = rate; });
}

void Document::mutate(Role role, Mutation m) {
    change([&](Song& s) {
        auto& p = s.part(role);
        if (m != Mutation::twist)
            ++p.seedP;
        if (m != Mutation::nudge)
            ++p.seedR;
    });
}

void Document::applyTake(Role role, int take) {
    change([&](Song& s) {
        auto& p = s.part(role);
        p.seedR += static_cast<std::uint32_t>(take * 13);
        p.seedP += static_cast<std::uint32_t>(take * 7);
    });
}

void Document::toggleMute(Role role) {
    change([&](Song& s) { s.part(role).mute = !s.part(role).mute; });
}

void Document::toggleSolo(Role role) {
    change([&](Song& s) { s.part(role).solo = !s.part(role).solo; });
}

void Document::togglePin(ItemRef item, const Note& note) {
    if (!validItem(song_, item) || note.role == Role::chords)
        return;
    change([&](Song& s) {
        auto it = std::find_if(s.pins.begin(), s.pins.end(), [&](const Pin& p) {
            return p.item == item && p.role == note.role && std::abs(p.start - note.start) < 0.01f &&
                   p.pitch == note.pitch;
        });
        if (it != s.pins.end())
            s.pins.erase(it);
        else
            s.pins.push_back({item, note.role, note.start, note.dur, note.pitch});
    });
}

void Document::clearPins(ItemRef item, Role role) {
    change([&](Song& s) { std::erase_if(s.pins, [&](const Pin& p) { return p.item == item && p.role == role; }); });
}

int Document::pinCount(ItemRef item, Role role) const {
    return static_cast<int>(std::count_if(song_.pins.begin(), song_.pins.end(),
                                          [&](const Pin& p) { return p.item == item && p.role == role; }));
}

void Document::cycleStutter(ItemRef item, int step) {
    if (!validItem(song_, item) || step < 0 || step >= itemBars(song_, item) * kStepsPerBar)
        return;
    change([&](Song& s) {
        auto& cells = itemStutter(s, item);
        const auto it = cells.find(step);
        const Stutter cur = it == cells.end() ? Stutter::none : it->second;
        const Stutter next = cur == Stutter::none  ? Stutter::r32
                             : cur == Stutter::r32 ? Stutter::r64
                             : cur == Stutter::r64 ? Stutter::gate
                                                   : Stutter::none;
        if (next == Stutter::none)
            cells.erase(step);
        else
            cells[step] = next;
    });
}

void Document::setRoute(int bridge, RouteId route) {
    if (validBridge(song_, bridge))
        change([&](Song& s) { s.bridges[static_cast<std::size_t>(bridge)].route = route; });
}

void Document::setBridgeBars(int bridge, int bars) {
    if (validBridge(song_, bridge))
        change([&](Song& s) {
            auto& b = s.bridges[static_cast<std::size_t>(bridge)];
            const int nb = std::clamp(bars, 1, kMaxBridgeBars);
            const float k = static_cast<float>(nb) / static_cast<float>(b.bars);
            for (auto& p : s.pins)
                if (p.item == ItemRef::bridge(bridge)) {
                    p.start *= k;
                    p.dur *= k;
                }
            StutterCells cells;
            for (const auto& [step, cell] : b.stutter) {
                const int ns = static_cast<int>(std::lround(static_cast<float>(step) * k));
                if (ns >= 0 && ns < nb * kStepsPerBar)
                    cells[ns] = cell;
            }
            b.stutter = std::move(cells);
            b.bars = nb;
        });
}

void Document::setSectionKey(int section, Key key) {
    if (!validSection(song_, section))
        return;
    change([&](Song& s) {
        auto& sec = s.sections[static_cast<std::size_t>(section)];
        const Key old = sec.key;
        if (key.mode != old.mode)
            for (auto& c : sec.chords) {
                const auto a = analyze(c, old);
                if (a.diatonic && a.degree >= 0) {
                    const auto d = diatonic({old.tonic, key.mode}, a.degree, levelOf(c), c.beats);
                    c.root = d.root;
                    c.quality = d.quality;
                    c.family = d.family;
                    c.bass = -1; // a slash bass from the old mode (B under Gm7) is not in the new one
                }
            }
        const int d = key.tonic - old.tonic;
        const int shortWay = mod12(d + 6) - 6; // pins move the short way round
        for (auto& c : sec.chords) {
            c.root = mod12(c.root + d);
            if (c.bass >= 0)
                c.bass = mod12(c.bass + d);
        }
        for (auto& p : s.pins)
            if (p.item == ItemRef::section(section))
                p.pitch += shortWay;
        sec.key = {mod12(key.tonic), key.mode};
    });
}

void Document::setSectionBars(int section, int bars) {
    if (!validSection(song_, section))
        return;
    change([&](Song& s) {
        auto& sec = s.sections[static_cast<std::size_t>(section)];
        const int nb = std::clamp(bars, 1, kMaxSectionBars);
        const float k = static_cast<float>(nb) / static_cast<float>(sec.bars);
        std::vector<Chord> src(sec.chords.begin(),
                               sec.chords.begin() + std::min<std::ptrdiff_t>(4, std::ssize(sec.chords)));
        for (auto& c : src)
            c.beats = static_cast<float>(nb * 4) / static_cast<float>(src.size());
        sec.chords = src;
        for (auto& p : s.pins)
            if (p.item == ItemRef::section(section)) {
                p.start *= k;
                p.dur *= k;
            }
        StutterCells cells;
        for (const auto& [step, cell] : sec.stutter) {
            const int ns = static_cast<int>(std::lround(static_cast<float>(step) * k));
            if (ns < nb * kStepsPerBar)
                cells[ns] = cell;
        }
        sec.stutter = cells;
        sec.bars = nb;
    });
}

void Document::loadProgression(int section, const Progression& progression) {
    if (!validSection(song_, section))
        return;
    change([&](Song& s) {
        auto& sec = s.sections[static_cast<std::size_t>(section)];
        sec.chords = realise(progression, sec.key, sec.bars);
    });
}

void Document::setSectionName(int section, std::string name) {
    if (validSection(song_, section))
        change([&](Song& s) { s.sections[static_cast<std::size_t>(section)].name = std::move(name); });
}

bool Document::addSection() {
    if (song_.sections.size() >= static_cast<std::size_t>(kMaxSections))
        return false;
    change([&](Song& s) {
        const auto& first = s.sections.front();
        Section sec;
        const std::size_t n = s.sections.size();
        sec.name = std::string(kDefaultNames[std::min<std::size_t>(n, kDefaultNames.size() - 1)]);
        // A contrasting key: the first section's relative, or its subdominant when that is taken.
        Key key = first.key.mode == Mode::minor ? Key{mod12(first.key.tonic + 3), Mode::major}
                                                : Key{mod12(first.key.tonic + 9), Mode::minor};
        if (std::any_of(s.sections.begin(), s.sections.end(), [&](const Section& x) { return x.key == key; }))
            key = {mod12(first.key.tonic + 5), first.key.mode};
        sec.key = key;
        sec.bars = 4;
        const auto list = progressionsFor(key.mode);
        sec.chords = realise(*list[n % list.size()], key, sec.bars);
        s.sections.push_back(sec);
    });
    return true;
}

bool Document::removeSection(int section) {
    if (!validSection(song_, section) || song_.sections.size() <= 1)
        return false;
    change([&](Song& s) {
        s.sections.erase(s.sections.begin() + section);
        const auto remap = [section](int x) { return x > section ? x - 1 : x; };
        std::vector<Bridge> kept;
        for (auto b : s.bridges)
            if (b.from != section && b.to != section) {
                b.from = remap(b.from);
                b.to = remap(b.to);
                kept.push_back(b);
            }
        s.bridges = kept;
        std::erase_if(s.pins, [&](const Pin& p) { return p.item.isBridge() || p.item.index == section; });
        for (auto& p : s.pins)
            p.item.index = remap(p.item.index);
    });
    return true;
}

void Document::setBpm(double bpm) {
    change([&](Song& s) { s.bpm = bpm; });
}

void Document::setName(std::string name) {
    change([&](Song& s) { s.name = std::move(name); });
}

} // namespace lg
