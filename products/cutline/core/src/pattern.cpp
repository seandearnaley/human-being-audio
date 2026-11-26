// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/pattern.h"

#include <hba/core/note_name.h>
#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>

namespace cl {

std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, const LaneConfig& cfg) noexcept CL_NONBLOCKING {
    if (cfg.midiNote >= 1 && cfg.midiNote <= 127)
        return static_cast<std::int8_t>(cfg.midiNote);
    const auto mIdx = static_cast<std::size_t>(std::clamp(static_cast<int>(map), 0, kNoteMapCount - 1));
    const auto lIdx = static_cast<std::size_t>(std::clamp(lane, 0, kDrumLaneCount - 1));
    return kKitNoteMaps[mIdx][lIdx];
}

SceneData effectiveSceneData(const PatternBank& bank, int sceneIdx) noexcept CL_NONBLOCKING {
    const auto idx = static_cast<std::size_t>(std::clamp(sceneIdx, 0, kSceneCount - 1));
    SceneData out = bank.scenes[idx];
    if (!out.customized && !out.hasAny()) {
        // Out-of-the-box musical 4-scene bank (Intro / Main / Busy Chop / Polymetric Contrast)
        if (idx == 1) {
            out.variationOffset = 2;
            out.complexityBias = 0.20f;
            out.chopBias = 0.24f;
            out.ghostBias = 0.14f;
        } else if (idx == 2) {
            out.variationOffset = 1;
            out.complexityBias = -0.30f;
            out.chopBias = -0.25f;
            out.ghostBias = -0.22f;
        } else if (idx == 3) {
            out.variationOffset = 3;
            out.complexityBias = 0.12f;
            out.chopBias = 0.35f;
            out.lanes[4].stepLength = 12; // Polymetric hats
            out.lanes[6].stepLength = 7;  // 7-step perc
            out.lanes[7].stepLength = 5;  // 5-step crash/perc
        }
    }
    return out;
}

void mutateUnlockedLanes(SceneData& scene, std::uint32_t seed) noexcept {
    scene.customized = true;
    std::uint32_t h = hba::dsp::laneHash(seed ^ 0x9e3779b9U);
    auto nextU32 = [&]() noexcept {
        h = hba::dsp::laneHash(h + 0x9e3779b9U);
        return h;
    };

    scene.variationOffset = static_cast<std::int8_t>((static_cast<int>(nextU32() % 19U)) - 9);
    for (int l = 0; l < kDrumLaneCount; ++l) {
        auto& lane = scene.lanes[static_cast<std::size_t>(l)];
        if (lane.locked)
            continue;
        // Clear previous ad-hoc mutations on unlocked lanes while keeping explicit length/mute/note.
        for (auto& st : lane.steps)
            st = {};
        const int len = lane.stepLength > 0 ? static_cast<int>(lane.stepLength) : 16;

        // Add subtle musical syncopation/accent/ratchet spice per lane role without destroying groove.
        if (l == 2 || l == 6 || l == 7) {
            // Ghosts & Perc: 2-3 fresh off-beat accents or flams
            const int edits = 2 + static_cast<int>(nextU32() % 2U);
            for (int e = 0; e < edits; ++e) {
                const int stIdx = static_cast<int>(nextU32() % static_cast<std::uint32_t>(len));
                auto& st = lane.steps[static_cast<std::size_t>(stIdx)];
                st.state = StepState::forceOn;
                st.velOffset = static_cast<std::int8_t>(static_cast<int>(nextU32() % 40U) - 20);
                if (l == 2 && (nextU32() % 4U) == 0U)
                    st.flam = true;
                if (l >= 6 && (nextU32() % 3U) == 0U)
                    st.ratchet = static_cast<std::uint8_t>((nextU32() & 1U) ? 2 : 3);
            }
        } else if (l == 4 || l == 5) {
            // Hats: 1-2 ratchet flurries or breath dropouts
            const int stIdx = static_cast<int>(nextU32() % static_cast<std::uint32_t>(len));
            auto& st = lane.steps[static_cast<std::size_t>(stIdx)];
            if (l == 4) {
                st.state = StepState::forceOn;
                st.ratchet = static_cast<std::uint8_t>((nextU32() & 1U) ? 2 : 3);
            } else {
                st.state = (nextU32() & 1U) ? StepState::forceOn : StepState::forceOff;
            }
        } else if (l == 0 || l == 1) {
            // Kick / Snare: subtle syncopated pickup on late steps (never kill beat 1!)
            const int candSteps[4] = {6, 10, 14, 15};
            const int pick = candSteps[nextU32() % 4U] % len;
            if (pick > 0) {
                lane.steps[static_cast<std::size_t>(pick)].state =
                    (nextU32() % 3U) == 0U ? StepState::forceOn : StepState::autoGen;
            }
        }
    }
}

void applyPolymeterToUnlocked(SceneData& scene, std::uint32_t seed) noexcept {
    scene.customized = true;
    std::uint32_t h = hba::dsp::laneHash(seed ^ 0x517cc1b7U);
    auto pickFrom = [&](std::initializer_list<std::uint8_t> vals) noexcept -> std::uint8_t {
        h = hba::dsp::laneHash(h + 0x27d4eb2dU);
        const std::size_t idx = h % vals.size();
        return *(vals.begin() + static_cast<std::ptrdiff_t>(idx));
    };

    // Keep Kick (0) and Snare (1) rock-solid on 16 or 32 unless unlocked & explicitly toggled
    if (!scene.lanes[0].locked)
        scene.lanes[0].stepLength = 16;
    if (!scene.lanes[1].locked)
        scene.lanes[1].stepLength = 16;
    if (!scene.lanes[2].locked)
        scene.lanes[2].stepLength = pickFrom({11, 13, 15, 16});
    if (!scene.lanes[3].locked)
        scene.lanes[3].stepLength = pickFrom({12, 14, 16});
    if (!scene.lanes[4].locked)
        scene.lanes[4].stepLength = pickFrom({10, 12, 14, 15});
    if (!scene.lanes[5].locked)
        scene.lanes[5].stepLength = pickFrom({9, 12, 15, 16});
    if (!scene.lanes[6].locked)
        scene.lanes[6].stepLength = pickFrom({5, 7, 9, 11});
    if (!scene.lanes[7].locked)
        scene.lanes[7].stepLength = pickFrom({5, 7, 11, 13});
}

void cloneSceneSlot(PatternBank& bank, int fromIdx, int toIdx, bool mutateClone, std::uint32_t seed) noexcept {
    const auto src = static_cast<std::size_t>(std::clamp(fromIdx, 0, kSceneCount - 1));
    const auto dst = static_cast<std::size_t>(std::clamp(toIdx, 0, kSceneCount - 1));
    SceneData copy = effectiveSceneData(bank, static_cast<int>(src));
    copy.customized = true;
    if (mutateClone)
        mutateUnlockedLanes(copy, seed);
    bank.scenes[dst] = copy;
}

void clearSceneEdits(SceneData& scene) noexcept { scene = {}; }

std::string midiNoteLabel(int midiNote) { return hba::core::midiNoteName(std::clamp(midiNote, 0, 127)); }

} // namespace cl
