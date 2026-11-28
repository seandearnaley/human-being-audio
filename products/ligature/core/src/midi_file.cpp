// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/midi_file.h"

#include <hba/core/smf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace lg {

namespace {

bool audible(const Song& song, Role r) {
    bool anySolo = false;
    for (const auto& p : song.parts)
        anySolo = anySolo || p.solo;
    const auto& p = song.part(r);
    return !p.mute && (!anySolo || p.solo);
}

std::uint32_t ticks(double beats) {
    return static_cast<std::uint32_t>(std::max(0.0, std::round(beats * static_cast<double>(kMidiFilePpq))));
}

std::vector<std::uint8_t> encode(const std::vector<TimedNote>& notes, std::optional<Role> role, double bpm,
                                 double endBeats, const std::string& name) {
    const auto toEvents = [&](Role r, std::vector<hba::core::SmfEvent>& events) {
        const auto ch = static_cast<std::uint8_t>(role ? 0 : midiChannel(r) - 1);
        for (const auto& n : notes) {
            if (n.role != r)
                continue;
            const auto pitch = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127));
            const auto vel = static_cast<std::uint8_t>(std::clamp(n.velocity, 1, 127));
            // Note-offs sort before note-ons at the same tick so a repeated note retriggers.
            events.push_back(
                hba::core::SmfEvent::msg(ticks(n.startBeats), 1, static_cast<std::uint8_t>(0x90u | ch), pitch, vel));
            events.push_back(hba::core::SmfEvent::msg(ticks(std::max(n.endBeats, n.startBeats + 1.0 / 64.0)), 0,
                                                      static_cast<std::uint8_t>(0x80u | ch), pitch, 0));
        }
    };
    const auto end = ticks(endBeats);
    if (role) {
        std::array<hba::core::SmfTrack, 1> tracks;
        tracks[0].name = name + " " + std::string(roleName(*role));
        toEvents(*role, tracks[0].events);
        return hba::core::encodeSmf(tracks, kMidiFilePpq, bpm, end);
    }
    std::array<hba::core::SmfTrack, kRoleCount> tracks;
    for (int r = 0; r < kRoleCount; ++r) {
        tracks[static_cast<std::size_t>(r)].name = name + " " + std::string(roleName(static_cast<Role>(r)));
        toEvents(static_cast<Role>(r), tracks[static_cast<std::size_t>(r)].events);
    }
    return hba::core::encodeSmf(tracks, kMidiFilePpq, bpm, end);
}

void appendPattern(std::vector<TimedNote>& out, const Song& song, const Pattern& p, double offsetBeats,
                   std::optional<Role> role) {
    for (const auto& n : p.notes) {
        if ((role && n.role != *role) || !audible(song, n.role))
            continue;
        out.push_back({offsetBeats + n.start / 4.0, offsetBeats + (n.start + n.dur) / 4.0, n.pitch,
                       std::clamp(static_cast<int>(std::lround(n.velocity * 127.0f)), 1, 127), n.role});
    }
}

} // namespace

std::vector<std::uint8_t> itemMidiFile(Document& doc, ItemRef item, std::optional<Role> role, double bpm) {
    const auto& p = doc.pattern(item);
    std::vector<TimedNote> notes;
    appendPattern(notes, doc.song(), p, 0.0, role);
    return encode(notes, role, bpm, p.bars * 4.0, doc.song().name + " " + itemKey(doc.song(), item));
}

std::vector<std::uint8_t> songMidiFile(Document& doc, std::optional<Role> role, double bpm, int passes) {
    std::vector<TimedNote> notes;
    double at = 0.0;
    const auto& song = doc.song();
    for (int s = 0; s < static_cast<int>(song.sections.size()); ++s) {
        const auto& sp = doc.pattern(ItemRef::section(s));
        for (int k = 0; k < std::max(1, passes); ++k) {
            appendPattern(notes, song, sp, at, role);
            at += sp.bars * 4.0;
        }
        if (s < static_cast<int>(song.bridges.size()) && s + 1 < static_cast<int>(song.sections.size())) {
            const auto& bp = doc.pattern(ItemRef::bridge(s));
            appendPattern(notes, song, bp, at, role);
            at += bp.bars * 4.0;
        }
    }
    return encode(notes, role, bpm, at, song.name);
}

std::vector<std::uint8_t> notesMidiFile(const std::vector<TimedNote>& notes, std::optional<Role> role, double bpm) {
    double end = 0.0;
    std::vector<TimedNote> kept;
    for (const auto& n : notes)
        if (!role || n.role == *role) {
            kept.push_back(n);
            end = std::max(end, n.endBeats);
        }
    return encode(kept, role, bpm, std::ceil(end / 4.0) * 4.0, "Ligature performance");
}

} // namespace lg
