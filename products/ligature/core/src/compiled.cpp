// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/compiled.h"

#include <algorithm>
#include <cmath>

namespace lg {

const CompiledPattern& CompiledSong::pattern(ItemRef item, int fit) const noexcept LG_NONBLOCKING {
    if (!item.isBridge())
        return sections[static_cast<std::size_t>(std::clamp(item.index, 0, kMaxSections - 1))];
    const auto& b = bridges[static_cast<std::size_t>(std::clamp(item.index, 0, kMaxBridges - 1))];
    const int f = std::clamp(fit > 0 ? fit : b.bars, 1, std::max(1, b.bars));
    return b.fits[static_cast<std::size_t>(f - 1)];
}

int CompiledSong::barsOf(ItemRef item) const noexcept LG_NONBLOCKING {
    if (!item.isBridge())
        return sections[static_cast<std::size_t>(std::clamp(item.index, 0, kMaxSections - 1))].bars;
    return bridges[static_cast<std::size_t>(std::clamp(item.index, 0, kMaxBridges - 1))].bars;
}

int CompiledSong::bridgeIndex(int from, int to) const noexcept LG_NONBLOCKING {
    for (int i = 0; i < bridgeCount; ++i)
        if (bridges[static_cast<std::size_t>(i)].from == from && bridges[static_cast<std::size_t>(i)].to == to)
            return i;
    return -1;
}

void compilePattern(const Pattern& pattern, CompiledPattern& out) {
    out.bars = pattern.bars;
    out.count = 0;
    out.slamTail = pattern.bridge && pattern.route == RouteId::slam;
    out.spanCount = std::min(static_cast<int>(pattern.spans.size()), static_cast<int>(out.spanStarts.size()));
    for (int i = 0; i < out.spanCount; ++i)
        out.spanStarts[static_cast<std::size_t>(i)] = pattern.spans[static_cast<std::size_t>(i)].start;
    for (const auto& n : pattern.notes) {
        if (out.count >= kMaxCompiledNotes)
            break;
        auto& c = out.notes[static_cast<std::size_t>(out.count++)];
        c.start = n.start;
        c.end = n.start + n.dur;
        c.pitch = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127));
        c.velocity = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(n.velocity * 127.0f)), 1, 127));
        c.role = static_cast<std::uint8_t>(n.role);
        c.span = static_cast<std::uint8_t>(std::clamp(n.span, 0, 255));
    }
}

void compileSong(const Song& song, CompiledSong& out, std::uint64_t revision) {
    out.revision = revision;
    out.bpm = static_cast<float>(song.bpm);
    out.sectionCount = std::min(static_cast<int>(song.sections.size()), kMaxSections);
    for (int s = 0; s < out.sectionCount; ++s)
        out.sectionUid[static_cast<std::size_t>(s)] = song.sections[static_cast<std::size_t>(s)].uid;
    out.bridgeCount = std::min(static_cast<int>(song.bridges.size()), kMaxBridges);
    bool anySolo = false;
    for (const auto& p : song.parts)
        anySolo = anySolo || p.solo;
    for (int r = 0; r < kRoleCount; ++r) {
        const auto& p = song.parts[static_cast<std::size_t>(r)];
        out.audible[static_cast<std::size_t>(r)] = !p.mute && (!anySolo || p.solo);
    }
    for (int s = 0; s < out.sectionCount; ++s) {
        compilePattern(buildPattern(song, ItemRef::section(s)), out.sections[static_cast<std::size_t>(s)]);
        auto& kc = out.keyChords[static_cast<std::size_t>(s)];
        std::vector<int> lead;
        for (int d = 0; d < 7; ++d) {
            const auto v = voiceChord(diatonic(song.sections[static_cast<std::size_t>(s)].key, d, 2),
                                      lead.empty() ? nullptr : &lead, true);
            lead = v.lead;
            const int n = std::min(static_cast<int>(v.notes.size()), 5);
            const int bass = bassPitch(diatonic(song.sections[static_cast<std::size_t>(s)].key, d, 0).root);
            kc.notes[static_cast<std::size_t>(d)][0] = static_cast<std::uint8_t>(bass);
            for (int i = 0; i < n; ++i)
                kc.notes[static_cast<std::size_t>(d)][static_cast<std::size_t>(i + 1)] =
                    static_cast<std::uint8_t>(std::clamp(v.notes[static_cast<std::size_t>(i)], 0, 127));
            kc.count[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(n + 1);
        }
    }
    for (int b = 0; b < out.bridgeCount; ++b) {
        const auto& src = song.bridges[static_cast<std::size_t>(b)];
        auto& dst = out.bridges[static_cast<std::size_t>(b)];
        dst.from = src.from;
        dst.to = src.to;
        dst.bars = src.bars;
        for (int fit = 1; fit <= src.bars; ++fit)
            compilePattern(buildPattern(song, ItemRef::bridge(b), fit), dst.fits[static_cast<std::size_t>(fit - 1)]);
    }
}

} // namespace lg
