// SPDX-License-Identifier: AGPL-3.0-or-later
// Datum 4-slot Kick Kit variations (A/B/C/D) & 16/32-step Context Sequencer data types.
#pragma once

#include "dt/nonblocking.h"
#include "dt/params.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace dt {

struct KitSlot {
    float tuneOffsetSt = 0.0f; // -12..+12 semitones relative to Tune
    float lengthScale = 1.0f;  // 0.35..2.0x relative to Length
    float clickScale = 1.0f;   // 0.0..1.6x relative to Click
    float driveScale = 1.0f;   // 0.0..1.6x relative to Drive
    float levelScale = 1.0f;   // 0.2..1.25x gain multiplier

    friend bool operator==(const KitSlot&, const KitSlot&) = default;
};

struct SeqStep {
    std::uint8_t velocity = 0; // 0 = off, 1..127 = active velocity
    std::uint8_t slot = 0;     // 0=A, 1=B, 2=C, 3=D
    std::uint8_t ratchet = 1;  // 1=single, 2=double roll, 3=triplet

    friend bool operator==(const SeqStep&, const SeqStep&) = default;
};

struct ComposePattern {
    std::array<KitSlot, kKitSlots> slots{};
    std::array<SeqStep, kMaxSeqSteps> steps{};

    friend bool operator==(const ComposePattern&, const ComposePattern&) = default;
};

inline std::array<KitSlot, kKitSlots> defaultKitSlots() noexcept DT_NONBLOCKING {
    return {{
        {0.0f, 1.00f, 1.00f, 1.00f, 1.00f},  // A · Main
        {1.0f, 0.72f, 0.68f, 0.80f, 0.74f},  // B · Ghost
        {0.5f, 0.66f, 0.90f, 0.95f, 0.88f},  // C · Double
        {-3.0f, 1.48f, 0.45f, 0.85f, 0.98f}, // D · Sub
    }};
}

// Canonical 172 BPM Neurofunk two-step kick groove (16-step & 32-step phrasing)
inline ComposePattern defaultComposePattern() noexcept DT_NONBLOCKING {
    ComposePattern p;
    p.slots = defaultKitSlots();
    // Bar 1: classic DnB stepper (1, 11) + tight ghost on 9
    p.steps[0] = {122, 0, 1};
    p.steps[8] = {68, 1, 1};
    p.steps[10] = {116, 0, 1};
    // Bar 2: stepper + crisp double-kick turnaround on step 30
    p.steps[16] = {124, 0, 1};
    p.steps[24] = {72, 1, 1};
    p.steps[26] = {118, 0, 1};
    p.steps[30] = {96, 2, 2};
    return p;
}

inline void sanitizePattern(ComposePattern& p) noexcept DT_NONBLOCKING {
    for (auto& sl : p.slots) {
        sl.tuneOffsetSt = std::clamp(sl.tuneOffsetSt, -12.0f, 12.0f);
        sl.lengthScale = std::clamp(sl.lengthScale, 0.35f, 2.0f);
        sl.clickScale = std::clamp(sl.clickScale, 0.0f, 1.6f);
        sl.driveScale = std::clamp(sl.driveScale, 0.0f, 1.6f);
        sl.levelScale = std::clamp(sl.levelScale, 0.2f, 1.25f);
    }
    for (auto& st : p.steps) {
        st.velocity = std::min<std::uint8_t>(st.velocity, 127);
        st.slot = std::min<std::uint8_t>(st.slot, static_cast<std::uint8_t>(kKitSlots - 1));
        st.ratchet = std::clamp<std::uint8_t>(st.ratchet, 1, 3);
    }
}

} // namespace dt
