// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes held on the MIDI input, lowest first. Fixed capacity, safe on the audio thread.
#pragma once

#include "cl/nonblocking.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cl {

inline constexpr int kMaxHeldInputNotes = 16;

struct HeldNotes {
    std::array<std::uint8_t, kMaxHeldInputNotes> notes{};
    std::array<std::uint8_t, kMaxHeldInputNotes> velocities{};
    int count = 0;
    std::uint8_t lastPressed = 0;

    void clear() noexcept CL_NONBLOCKING {
        count = 0;
        lastPressed = 0;
    }
    void push(int note, int vel = 100) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        const auto v = static_cast<std::uint8_t>(vel < 1 ? 1 : (vel > 127 ? 127 : vel));
        lastPressed = n;
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n) {
                velocities[static_cast<std::size_t>(i)] = v;
                return;
            }
        }
        if (count >= kMaxHeldInputNotes) {
            if (n < notes[0])
                return;
            for (int i = 0; i < kMaxHeldInputNotes - 1; ++i) {
                notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i + 1)];
                velocities[static_cast<std::size_t>(i)] = velocities[static_cast<std::size_t>(i + 1)];
            }
            --count;
        }
        int insertAt = count;
        while (insertAt > 0 && notes[static_cast<std::size_t>(insertAt - 1)] > n) {
            notes[static_cast<std::size_t>(insertAt)] = notes[static_cast<std::size_t>(insertAt - 1)];
            velocities[static_cast<std::size_t>(insertAt)] = velocities[static_cast<std::size_t>(insertAt - 1)];
            --insertAt;
        }
        notes[static_cast<std::size_t>(insertAt)] = n;
        velocities[static_cast<std::size_t>(insertAt)] = v;
        ++count;
    }
    void remove(int note) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n) {
                for (int j = i; j < count - 1; ++j) {
                    notes[static_cast<std::size_t>(j)] = notes[static_cast<std::size_t>(j + 1)];
                    velocities[static_cast<std::size_t>(j)] = velocities[static_cast<std::size_t>(j + 1)];
                }
                --count;
                if (count == 0)
                    lastPressed = 0;
                else if (lastPressed == n)
                    lastPressed = notes[static_cast<std::size_t>(count - 1)];
                return;
            }
        }
    }

    // True when both hold the same notes at the same velocities.
    bool sameAs(const HeldNotes& other) const noexcept CL_NONBLOCKING {
        if (count != other.count || lastPressed != other.lastPressed)
            return false;
        for (int i = 0; i < count; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (notes[idx] != other.notes[idx] || velocities[idx] != other.velocities[idx])
                return false;
        }
        return true;
    }
};

} // namespace cl
