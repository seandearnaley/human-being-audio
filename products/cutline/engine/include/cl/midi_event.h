// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed-capacity, allocation-free MIDI event container for the real-time callback.
#pragma once

#include "cl/nonblocking.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace cl {

struct MidiEvent {
    int sampleOffset = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3]{0, 0, 0};
    // Stable sub-sample tie-breaker: 0 = RPN/setup, 1 = PB/CC pre-note, 2 = NoteOff,
    // 3 = NoteOn, 4 = continuous CC/PB/AT.
    std::uint8_t order = 4;

    static constexpr MidiEvent noteOn(int offset, int channel1To16, int note, int velocity) noexcept CL_NONBLOCKING {
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel1To16, 1, 16) - 1);
        return {offset,
                3,
                {static_cast<std::uint8_t>(0x90u | ch), static_cast<std::uint8_t>(std::clamp(note, 0, 127)),
                 static_cast<std::uint8_t>(std::clamp(velocity, 1, 127))},
                3};
    }

    static constexpr MidiEvent noteOff(int offset, int channel1To16, int note,
                                       int velocity = 0) noexcept CL_NONBLOCKING {
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel1To16, 1, 16) - 1);
        return {offset,
                3,
                {static_cast<std::uint8_t>(0x80u | ch), static_cast<std::uint8_t>(std::clamp(note, 0, 127)),
                 static_cast<std::uint8_t>(std::clamp(velocity, 0, 127))},
                2};
    }

    static constexpr MidiEvent controlChange(int offset, int channel1To16, int cc, int value,
                                             std::uint8_t ord = 4) noexcept CL_NONBLOCKING {
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel1To16, 1, 16) - 1);
        return {offset,
                3,
                {static_cast<std::uint8_t>(0xB0u | ch), static_cast<std::uint8_t>(std::clamp(cc, 0, 127)),
                 static_cast<std::uint8_t>(std::clamp(value, 0, 127))},
                ord};
    }

    static constexpr MidiEvent pitchBend(int offset, int channel1To16, int value14Bit,
                                         std::uint8_t ord = 4) noexcept CL_NONBLOCKING {
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel1To16, 1, 16) - 1);
        const int v = std::clamp(value14Bit, 0, 16383);
        return {offset,
                3,
                {static_cast<std::uint8_t>(0xE0u | ch), static_cast<std::uint8_t>(v & 0x7F),
                 static_cast<std::uint8_t>((v >> 7) & 0x7F)},
                ord};
    }

    static constexpr MidiEvent channelPressure(int offset, int channel1To16, int pressure,
                                               std::uint8_t ord = 4) noexcept CL_NONBLOCKING {
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel1To16, 1, 16) - 1);
        return {offset,
                2,
                {static_cast<std::uint8_t>(0xD0u | ch), static_cast<std::uint8_t>(std::clamp(pressure, 0, 127)), 0},
                ord};
    }

    constexpr bool isNoteOn() const noexcept CL_NONBLOCKING {
        return size == 3 && (data[0] & 0xF0u) == 0x90u && data[2] > 0;
    }
    constexpr bool isNoteOff() const noexcept CL_NONBLOCKING {
        return size == 3 && ((data[0] & 0xF0u) == 0x80u || ((data[0] & 0xF0u) == 0x90u && data[2] == 0));
    }
    constexpr int channel() const noexcept CL_NONBLOCKING { return static_cast<int>(data[0] & 0x0Fu) + 1; }
    constexpr int noteNumber() const noexcept CL_NONBLOCKING { return static_cast<int>(data[1]); }
};

// Hard safety cap well below Steinberg VST3 Host Checker's 2048 event/block limit.
inline constexpr int kMaxMidiEventsPerBlock = 256;
inline constexpr int kReservedNoteOffHeadroom = 32;

struct MidiEventBuffer {
    std::array<MidiEvent, kMaxMidiEventsPerBlock> events{};
    int count = 0;

    void clear() noexcept CL_NONBLOCKING { count = 0; }

    bool pushCritical(const MidiEvent& ev) noexcept CL_NONBLOCKING {
        if (count >= kMaxMidiEventsPerBlock)
            return false;
        events[static_cast<std::size_t>(count++)] = ev;
        return true;
    }

    bool push(const MidiEvent& ev) noexcept CL_NONBLOCKING {
        // Reserve top slots so Note Offs / panic flushes can never be dropped.
        if (count >= kMaxMidiEventsPerBlock - kReservedNoteOffHeadroom)
            return false;
        events[static_cast<std::size_t>(count++)] = ev;
        return true;
    }

    void sort() noexcept CL_NONBLOCKING {
        // Stable insertion sort: zero allocations, optimal for tiny nearly-sorted buffers.
        for (int i = 1; i < count; ++i) {
            const MidiEvent key = events[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0) {
                const MidiEvent& cur = events[static_cast<std::size_t>(j)];
                if (cur.sampleOffset < key.sampleOffset ||
                    (cur.sampleOffset == key.sampleOffset && cur.order <= key.order)) {
                    break;
                }
                events[static_cast<std::size_t>(j + 1)] = cur;
                --j;
            }
            events[static_cast<std::size_t>(j + 1)] = key;
        }
    }
};

} // namespace cl
