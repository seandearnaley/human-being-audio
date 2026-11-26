// SPDX-License-Identifier: GPL-3.0-or-later
// Shared Standard MIDI File (SMF Type 0) binary encoder.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hba::core {

struct SmfEvent {
    std::uint32_t tick = 0;
    int order = 0;
    std::array<std::uint8_t, 6> data{};
    std::uint8_t size = 0;

    static SmfEvent msg(std::uint32_t t, int ord, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2 = 0,
                        std::uint8_t sz = 3) {
        SmfEvent e;
        e.tick = t;
        e.order = ord;
        e.data[0] = b0;
        e.data[1] = b1;
        e.data[2] = b2;
        e.size = sz;
        return e;
    }

    static SmfEvent msg2(std::uint32_t t, int ord, std::uint8_t b0, std::uint8_t b1) {
        return msg(t, ord, b0, b1, 0, 2);
    }
};

inline void pushBe16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

inline void pushBe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

inline void pushVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
    std::uint8_t buf[5]{};
    int n = 0;
    buf[n++] = static_cast<std::uint8_t>(value & 0x7Fu);
    value >>= 7u;
    while (value > 0u) {
        buf[n++] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
        value >>= 7u;
    }
    for (int i = n - 1; i >= 0; --i)
        out.push_back(buf[i]);
}

// Encodes sorted MIDI events with a leading Tempo meta-event and trailing End-of-Track at `endTick`.
inline std::vector<std::uint8_t> encodeSmfType0(std::vector<SmfEvent> events, std::uint16_t ppq, double bpm,
                                                std::uint32_t endTick) {
    const double safeBpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0);
    const auto usPerBeat = static_cast<std::uint32_t>(std::llround(60'000'000.0 / safeBpm));

    std::stable_sort(events.begin(), events.end(), [](const SmfEvent& a, const SmfEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    std::vector<std::uint8_t> track;
    track.reserve(events.size() * 5u + 16u);

    // Tempo meta-event at tick 0
    pushVlq(track, 0u);
    track.insert(track.end(),
                 {0xFFu, 0x51u, 0x03u, static_cast<std::uint8_t>((usPerBeat >> 16u) & 0xFFu),
                  static_cast<std::uint8_t>((usPerBeat >> 8u) & 0xFFu), static_cast<std::uint8_t>(usPerBeat & 0xFFu)});

    std::uint32_t prevTick = 0u;
    for (const auto& ev : events) {
        const std::uint32_t delta = ev.tick >= prevTick ? (ev.tick - prevTick) : 0u;
        prevTick = std::max(prevTick, ev.tick);
        pushVlq(track, delta);
        for (std::uint8_t i = 0; i < ev.size && i < ev.data.size(); ++i)
            track.push_back(ev.data[i]);
    }

    // End of track at exact endTick so DAW clip loop length matches the phrase/bar length.
    pushVlq(track, endTick >= prevTick ? (endTick - prevTick) : 0u);
    track.insert(track.end(), {0xFFu, 0x2Fu, 0x00u});

    std::vector<std::uint8_t> smf;
    smf.reserve(22u + track.size());
    smf.insert(smf.end(), {'M', 'T', 'h', 'd'});
    pushBe32(smf, 6u);
    pushBe16(smf, 0u); // Format 0
    pushBe16(smf, 1u); // 1 track
    pushBe16(smf, ppq);
    smf.insert(smf.end(), {'M', 'T', 'r', 'k'});
    pushBe32(smf, static_cast<std::uint32_t>(track.size()));
    smf.insert(smf.end(), track.begin(), track.end());
    return smf;
}

} // namespace hba::core
