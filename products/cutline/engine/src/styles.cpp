// SPDX-License-Identifier: AGPL-3.0-or-later
#include "styles.h"

#include <algorithm>
#include <initializer_list>

namespace cl::styles {

namespace {

constexpr std::uint32_t m32(std::initializer_list<int> steps) noexcept {
    std::uint32_t mask = 0U;
    for (int st : steps) {
        if (st >= 0 && st < 32)
            mask |= (1U << static_cast<unsigned>(st));
    }
    return mask;
}

// Repeat a 1-bar (16-step) mask identically across Bar 1 and Bar 2.
constexpr std::uint32_t dup16(std::initializer_list<int> steps) noexcept {
    const std::uint32_t bar = m32(steps);
    return bar | (bar << 16U);
}

// Hand-crafted 32-step (2-bar A -> A'/B) kits across 12 iconic electronic rhythm families.
// Lanes: 0=Kick, 1=Snare, 2=Ghost, 3=Clap/Rim, 4=ClosedHat, 5=OpenHat, 6=PercA, 7=PercB/Crash.
constexpr std::array<StyleKit, kDrumStyleCount> kStyles{{
    // 0: AMEN / JUNGLE — authentic 2-bar Amen break with Bar 2 displaced snare (step 27/30) & ride lift
    {
        .coreMask = {m32({0, 2, 10, 11, 16, 18, 26}), m32({4, 12, 20, 30}), m32({7, 9, 23, 25}), m32({}),
                     dup16({0, 2, 4, 6, 8, 10, 12}), m32({14, 30}), m32({}), m32({0})},
        .grooveMask = {m32({0, 2, 10, 11, 16, 18, 26}), m32({4, 12, 20, 27, 30}), m32({7, 9, 15, 23, 25, 29, 31}),
                       m32({12, 30}), dup16({0, 2, 4, 6, 8, 10, 12}), m32({14, 30}), m32({11, 22}), m32({0, 16})},
        .spiceMask = {m32({3, 19, 27}), m32({14}), m32({5, 13, 21, 28}), m32({4, 20, 27}),
                      dup16({1, 3, 5, 7, 9, 11, 13, 15}), m32({6, 22}), m32({3, 14, 25, 29}), m32({8, 24})},
        .accentMask = {m32({0, 10, 16, 26}), m32({4, 12, 20, 30}), m32({9, 25}), m32({12, 30}), dup16({0, 4, 8, 12}),
                       m32({14, 30}), m32({11, 22}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 2.8, 1.5, 3.2, -1.0, 0.8, 1.2, 0.0},
        .slicerStabMask = m32({0, 3, 6, 10, 14, 16, 19, 22, 26, 29}),
        .slicerAccentMask = m32({0, 10, 16, 26}),
        .defaultFill = FillType::drillChop,
    },

    // 1: THINK / LIQUID — rolling soulful 16th shaker/hat momentum, double ghost shuffles & open-hat air
    {
        .coreMask = {m32({0, 10, 16, 26}), dup16({4, 12}), m32({7, 9, 23, 25}), m32({}),
                     dup16({0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14}), m32({7, 15, 23, 31}), m32({}), m32({0})},
        .grooveMask = {m32({0, 10, 16, 22, 26}), dup16({4, 12}), m32({7, 9, 14, 15, 23, 25, 30, 31}), m32({11, 27}),
                       dup16({0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14}), m32({7, 15, 23, 31}), m32({6, 22}),
                       m32({0, 14, 16})},
        .spiceMask = {m32({2, 18, 29}), m32({15, 30}), m32({3, 5, 19, 21}), m32({4, 20, 30}), dup16({7, 15}),
                      m32({11, 27}), m32({3, 10, 19, 26, 29}), m32({8, 24})},
        .accentMask = {m32({0, 10, 16, 26}), dup16({4, 12}), m32({9, 25}), m32({11, 27}), dup16({0, 2, 4, 6, 8, 10, 12}),
                       m32({7, 15, 23, 31}), m32({6, 22}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 16, 32, 32},
        .pocketMs = {0.0, 3.6, 2.0, 2.5, -0.8, 1.0, 1.5, 0.5},
        .slicerStabMask = m32({0, 6, 10, 14, 16, 22, 26, 30}),
        .slicerAccentMask = m32({0, 10, 16, 26}),
        .defaultFill = FillType::snareRoll,
    },

    // 2: NEURO 2-STEP — surgical neurofunk 2-step kick, massive backbeat + clap stack, razor ghost chatter
    {
        .coreMask = {m32({0, 10, 16, 26}), dup16({4, 12}), m32({7, 9, 23, 25}), m32({12, 28}),
                     dup16({0, 2, 4, 6, 8, 10, 12, 14}), m32({6, 22}), m32({}), m32({0})},
        .grooveMask = {m32({0, 10, 16, 22, 26}), dup16({4, 12}), m32({7, 9, 14, 23, 25, 30, 31}), m32({4, 12, 20, 28}),
                       dup16({0, 2, 3, 4, 6, 8, 10, 11, 12, 14}), m32({6, 22}), m32({14, 27}), m32({0, 16})},
        .spiceMask = {m32({2, 15, 18, 29}), m32({29}), m32({5, 11, 21, 27}), m32({14, 30}), dup16({1, 7, 9, 13, 15}),
                      m32({14, 30}), m32({3, 7, 19, 25, 31}), m32({8, 24})},
        .accentMask = {m32({0, 10, 16, 26}), dup16({4, 12}), m32({9, 25}), m32({12, 28}), dup16({0, 4, 8, 12}),
                       m32({6, 22}), m32({14, 27}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 2.0, 0.8, 2.2, -1.4, 0.5, 0.6, 0.0},
        .slicerStabMask = m32({0, 3, 7, 10, 12, 16, 19, 22, 26, 28, 30}),
        .slicerAccentMask = m32({0, 10, 16, 26}),
        .defaultFill = FillType::snareRoll,
    },

    // 3: HALFTIME / 85 — colossal beat-3 snare, heavy sub-kick skips, spacious hats + late flurries
    {
        .coreMask = {m32({0, 11, 16, 27}), m32({8, 24}), m32({5, 15, 21}), m32({8, 24}),
                     dup16({0, 2, 4, 6, 8, 10, 12, 14}), m32({14, 30}), m32({}), m32({0})},
        .grooveMask = {m32({0, 7, 11, 16, 22, 27, 30}), m32({8, 24}), m32({5, 14, 15, 21, 29, 31}), m32({8, 24}),
                       dup16({0, 2, 3, 4, 6, 8, 10, 12, 13, 14, 15}), m32({14, 30}), m32({6, 18, 28}), m32({0})},
        .spiceMask = {m32({3, 14, 19, 29}), m32({15, 30}), m32({2, 11, 18, 26}), m32({6, 22}), dup16({1, 5, 7, 9, 11}),
                      m32({6, 22}), m32({3, 11, 14, 22, 31}), m32({16})},
        .accentMask = {m32({0, 11, 16, 27}), m32({8, 24}), m32({15, 31}), m32({8, 24}), dup16({0, 4, 8, 12}),
                       m32({14, 30}), m32({6, 28}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 5.2, 2.4, 5.5, -0.5, 1.5, 2.0, 0.0},
        .slicerStabMask = m32({0, 6, 11, 16, 22, 27}),
        .slicerAccentMask = m32({0, 11, 16, 27}),
        .defaultFill = FillType::halfDrop,
    },

    // 4: UKG / 2-STEP — skippy off-grid kicks, swung ghost-snare shuffles, offbeat open hats & crisp claps
    {
        .coreMask = {m32({0, 10, 16, 27}), dup16({4, 12}), m32({7, 9, 15, 23, 25, 31}), dup16({4, 12}),
                     dup16({0, 2, 6, 8, 10, 14}), dup16({2, 6, 10, 14}), m32({}), m32({0})},
        .grooveMask = {m32({0, 10, 16, 22, 27}), dup16({4, 12}), m32({7, 9, 15, 23, 25, 29, 31}),
                       m32({4, 12, 14, 20, 28}), dup16({0, 3, 4, 7, 8, 11, 12, 15}), dup16({2, 6, 10, 14}),
                       m32({3, 11, 19, 27}), m32({0, 14})},
        .spiceMask = {m32({6, 14, 19, 30}), m32({14, 30}), m32({1, 5, 13, 17, 21}), m32({7, 23, 30}),
                      dup16({1, 5, 9, 13}), m32({15, 31}), m32({6, 14, 22, 30}), m32({16})},
        .accentMask = {m32({0, 10, 16, 27}), dup16({4, 12}), m32({7, 15, 23, 31}), dup16({4, 12}), dup16({0, 8}),
                       dup16({2, 6, 10, 14}), m32({11, 27}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 16, 32, 32},
        .pocketMs = {0.0, 4.2, 3.0, 4.5, 1.5, 2.2, 2.0, 0.0},
        .slicerStabMask = m32({0, 3, 6, 10, 14, 16, 19, 23, 26, 30}),
        .slicerAccentMask = m32({0, 6, 14, 16, 23, 30}),
        .defaultFill = FillType::airCut,
    },

    // 5: FOOTWORK / JUKE — 3-against-4 dotted-8th kick/snare polyrhythm + frantic tom/clap callouts
    {
        .coreMask = {m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 29}), m32({4, 12, 20, 28}), m32({7, 14, 23, 30}),
                     m32({4, 11, 20, 27}), dup16({0, 2, 4, 6, 8, 10, 12, 14}), m32({6, 14, 22, 30}),
                     m32({2, 9, 18, 25}), m32({0})},
        .grooveMask = {m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 29}), m32({4, 12, 14, 20, 28, 30}),
                       m32({5, 7, 15, 21, 23, 31}), m32({4, 11, 14, 20, 27, 30}),
                       dup16({0, 1, 2, 4, 6, 8, 9, 10, 12, 14}), m32({6, 14, 22, 30}),
                       m32({2, 5, 9, 15, 18, 21, 25, 31}), m32({0, 12, 24})},
        .spiceMask = {m32({8, 15, 24, 31}), m32({7, 23}), m32({1, 11, 17, 27}), m32({2, 18, 25}),
                      dup16({3, 5, 7, 11, 13, 15}), m32({3, 19}), m32({7, 13, 23, 29}), m32({6, 18, 30})},
        .accentMask = {m32({0, 6, 12, 16, 22, 29}), m32({4, 12, 20, 28}), m32({14, 30}), m32({11, 27}),
                       dup16({0, 4, 8, 12}), m32({14, 30}), m32({2, 9, 18, 25}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 1.5, 0.8, 1.8, -1.2, 0.4, 0.6, 0.0},
        .slicerStabMask = m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 29}),
        .slicerAccentMask = m32({0, 6, 12, 16, 22}),
        .defaultFill = FillType::tomRun,
    },

    // 6: STEPPER / TECH — hypnotic 4x4 stepper drive with polymetric 7-step & 5-step interlocking perc
    {
        .coreMask = {dup16({0, 4, 8, 12}), dup16({4, 12}), m32({7, 15, 23, 31}), dup16({4, 12}),
                     dup16({0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15}), dup16({2, 6, 10, 14}), m32({0, 2, 5}),
                     m32({0, 3})},
        .grooveMask = {m32({0, 4, 8, 12, 16, 20, 24, 28, 30}), dup16({4, 12}), m32({3, 7, 15, 19, 23, 31}),
                       dup16({4, 12}), dup16({0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15}), dup16({2, 6, 10, 14}),
                       m32({0, 2, 4, 5}), m32({0, 2, 3})},
        .spiceMask = {m32({14, 15, 27, 31}), m32({14, 30}), m32({1, 9, 11, 17, 25, 27}), m32({14, 30}), dup16({2, 10}),
                      m32({15, 31}), m32({1, 3, 6}), m32({1, 4})},
        .accentMask = {dup16({0, 4, 8, 12}), dup16({4, 12}), m32({7, 23}), dup16({4, 12}), dup16({0, 4, 8, 12}),
                       dup16({2, 6, 10, 14}), m32({0, 4}), m32({0})},
        .defaultLen = {32, 16, 32, 16, 16, 16, 7, 5}, // Polymetric Perc A (7) & Perc B (5)!
        .pocketMs = {0.0, 1.8, 1.0, 2.0, -1.2, 0.8, 0.5, -0.5},
        .slicerStabMask = dup16({0, 2, 3, 6, 8, 10, 11, 14}),
        .slicerAccentMask = dup16({2, 6, 10, 14}),
        .defaultFill = FillType::snareRoll,
    },

    // 7: ELECTRO / 808 BREAK — classic syncopated 808 electro kicks, snappy snare+clap, cowbell/perc hook
    {
        .coreMask = {m32({0, 6, 10, 16, 22, 26}), dup16({4, 12}), m32({15, 31}), dup16({4, 12}),
                     dup16({0, 2, 4, 6, 8, 10, 12, 14}), m32({14, 30}), m32({0, 3, 6, 10, 16, 19, 22, 26}), m32({0})},
        .grooveMask = {m32({0, 6, 10, 16, 22, 26, 29}), dup16({4, 12}), m32({7, 15, 23, 30, 31}),
                       m32({4, 12, 20, 28, 30}), dup16({0, 1, 2, 4, 6, 8, 10, 12, 14, 15}), m32({6, 14, 22, 30}),
                       m32({0, 3, 6, 10, 13, 16, 19, 22, 26, 30}), m32({0, 16})},
        .spiceMask = {m32({3, 14, 19, 31}), m32({14, 27}), m32({2, 9, 18, 25}), m32({15, 26}),
                      dup16({3, 5, 7, 9, 11, 13}), m32({2, 18}), m32({8, 14, 24, 28}), m32({8, 24})},
        .accentMask = {m32({0, 6, 10, 16, 22, 26}), dup16({4, 12}), m32({15, 31}), dup16({4, 12}), dup16({0, 4, 8, 12}),
                       m32({14, 30}), m32({0, 6, 10, 16, 22, 26}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 1.5, 0.8, 1.8, -0.8, 0.4, 0.5, 0.0},
        .slicerStabMask = m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 29}),
        .slicerAccentMask = m32({0, 6, 10, 16, 22, 26}),
        .defaultFill = FillType::tomRun,
    },

    // 8: DRILL / IDM — Aphex/Squarepusher micro-chopped polyrhythmic kicks, shifted snares & frantic rolls
    {
        .coreMask = {m32({0, 3, 10, 14, 16, 18, 27}), m32({4, 11, 20, 25, 30}), m32({1, 6, 9, 15, 19, 23, 28}),
                     m32({13, 29}), dup16({0, 1, 3, 4, 6, 8, 9, 11, 13, 14}), m32({2, 12, 22}), m32({6, 14, 21, 28}),
                     m32({0, 16})},
        .grooveMask = {m32({0, 3, 7, 10, 14, 16, 18, 23, 27, 29}), m32({4, 11, 13, 20, 25, 30}),
                       m32({1, 5, 6, 9, 15, 17, 21, 22, 26, 28, 31}), m32({4, 13, 20, 29}),
                       dup16({0, 1, 3, 4, 6, 7, 8, 9, 11, 13, 14, 15}), m32({2, 12, 18, 28}),
                       m32({5, 6, 14, 21, 24, 28, 31}), m32({0, 10, 16, 26})},
        .spiceMask = {m32({5, 12, 21, 31}), m32({7, 15, 22, 27}), m32({2, 8, 12, 19, 24}), m32({7, 15, 23, 31}),
                      dup16({2, 5, 10, 12}), m32({7, 15, 23, 31}), m32({1, 3, 9, 11, 17, 19, 27}), m32({4, 12, 20, 28})},
        .accentMask = {m32({0, 10, 16, 27}), m32({4, 11, 20, 30}), m32({6, 15, 23, 31}), m32({13, 29}),
                       dup16({0, 3, 6, 9, 13}), m32({2, 12}), m32({6, 14, 28}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 1.0, 0.5, 1.2, -1.5, 0.2, 0.4, -0.4},
        .slicerStabMask = m32({0, 2, 3, 6, 9, 11, 14, 16, 18, 21, 25, 27, 30}),
        .slicerAccentMask = m32({0, 6, 11, 16, 25, 30}),
        .defaultFill = FillType::drillChop,
    },

    // 9: AFRO / POLYRHYTHM — 3:2 clave, 12-step shaker hat, 11-step & 7-step interlocking perc polymeters
    {
        .coreMask = {m32({0, 6, 10, 16, 22, 26}), m32({4, 12, 20, 28}), m32({2, 7, 11}),
                     m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 28}), m32({0, 2, 3, 5, 6, 8, 9, 11}), m32({6, 14, 22, 30}),
                     m32({0, 3, 5, 8}), m32({0, 2, 5})},
        .grooveMask = {m32({0, 6, 10, 16, 22, 26, 30}), m32({4, 12, 20, 28}), m32({2, 5, 7, 9, 11}),
                       m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 28}), m32({0, 1, 2, 3, 5, 6, 7, 8, 9, 11}),
                       m32({6, 14, 22, 30}), m32({0, 2, 3, 5, 8, 10}), m32({0, 2, 4, 5})},
        .spiceMask = {m32({3, 14, 19, 28}), m32({14, 30}), m32({0, 4, 8, 10}), m32({8, 14, 24, 30}), m32({4, 10}),
                      m32({2, 10, 18, 26}), m32({1, 4, 7, 9}), m32({1, 3, 6})},
        .accentMask = {m32({0, 10, 16, 26}), m32({4, 12, 20, 28}), m32({2, 7}), m32({0, 6, 12, 16, 22, 28}),
                       m32({0, 3, 6, 9}), m32({6, 14, 22, 30}), m32({0, 5, 8}), m32({0, 5})},
        .defaultLen = {32, 32, 12, 32, 12, 32, 11, 7}, // Deep interlocking 12 / 11 / 7 polymeter!
        .pocketMs = {0.0, 3.0, 2.0, 1.5, 1.2, 1.8, 1.0, 0.5},
        .slicerStabMask = m32({0, 3, 6, 10, 12, 16, 19, 22, 26, 28}),
        .slicerAccentMask = m32({0, 6, 12, 16, 22, 28}),
        .defaultFill = FillType::tomRun,
    },

    // 10: TRAP / GRIME — sparse heavy 808 kick, razor beat-3 snare+clap, rolling hi-hat ratchet pockets
    {
        .coreMask = {m32({0, 10, 16, 22, 29}), m32({8, 24}), m32({15, 31}), m32({8, 24}),
                     dup16({0, 2, 4, 6, 8, 10, 12, 14}), m32({6, 22}), m32({}), m32({0})},
        .grooveMask = {m32({0, 7, 10, 16, 22, 26, 29}), m32({8, 24}), m32({5, 14, 15, 21, 30, 31}), m32({8, 24}),
                       dup16({0, 2, 4, 6, 8, 10, 11, 12, 13, 14, 15}), m32({6, 22}), m32({6, 14, 20, 30}),
                       m32({0, 16})},
        .spiceMask = {m32({3, 14, 19, 27}), m32({14, 30}), m32({3, 11, 19, 27}), m32({14, 30}), dup16({1, 3, 5, 7, 9}),
                      m32({14, 30}), m32({3, 11, 18, 27}), m32({8, 24})},
        .accentMask = {m32({0, 10, 16, 29}), m32({8, 24}), m32({15, 31}), m32({8, 24}), dup16({0, 4, 8, 12}),
                       m32({6, 22}), m32({14, 30}), m32({0})},
        .defaultLen = {32, 32, 32, 32, 16, 32, 32, 32},
        .pocketMs = {0.0, 3.8, 2.0, 4.0, -1.0, 1.2, 1.5, 0.0},
        .slicerStabMask = m32({0, 7, 10, 14, 16, 22, 29}),
        .slicerAccentMask = m32({0, 10, 16, 29}),
        .defaultFill = FillType::snareRoll,
    },

    // 11: EUCLIDEAN LAB — pure polymetric Bjorklund Euclidean lab (computed dynamically in phrase.cpp!)
    {
        .coreMask = {},
        .grooveMask = {},
        .spiceMask = {},
        .accentMask = {dup16({0, 8}), dup16({4, 12}), dup16({0, 5}), dup16({0, 4}), dup16({0, 4, 8, 12}),
                       dup16({2, 10}), m32({0, 4}), m32({0, 3})},
        .defaultLen = {16, 16, 11, 12, 16, 16, 7, 5},
        .pocketMs = {0.0, 2.2, 1.2, 1.8, -0.8, 0.8, 0.6, -0.4},
        .slicerStabMask = m32({0, 3, 6, 9, 12, 16, 19, 22, 25, 28}),
        .slicerAccentMask = m32({0, 6, 12, 16, 22, 28}),
        .defaultFill = FillType::tomRun,
    },
}};

} // namespace

std::uint32_t euclidMask(int hits, int steps, int rotation) noexcept CL_NONBLOCKING {
    const int n = std::clamp(steps, 1, 32);
    const int k = std::clamp(hits, 0, n);
    if (k <= 0)
        return 0U;
    std::uint32_t mask = 0U;
    const int rot = ((rotation % n) + n) % n;
    for (int i = 0; i < n; ++i) {
        // Bresenham / Euclidean maximal-evenness distribution starting on beat 0.
        if (((i * k) % n) < k) {
            const int pos = (i + rot) % n;
            mask |= (1U << static_cast<unsigned>(pos));
        }
    }
    return mask;
}

const StyleKit& styleKit(DrumStyle style) noexcept CL_NONBLOCKING {
    const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(style), 0, kDrumStyleCount - 1));
    return kStyles[idx];
}

int euclideanDefaultHits(int lane, float complexity) noexcept CL_NONBLOCKING {
    const float c = std::clamp(complexity, 0.0f, 1.0f);
    switch (lane) {
    case 0: // Kick (len 16)
        return c < 0.25f ? 3 : (c < 0.65f ? 5 : 6);
    case 1: // Snare (len 16)
        return c < 0.70f ? 2 : 3;
    case 2: // Ghost (len 11)
        return c < 0.20f ? 2 : (c < 0.60f ? 4 : 6);
    case 3: // Clap/Rim (len 12)
        return c < 0.35f ? 2 : (c < 0.75f ? 3 : 5);
    case 4: // Closed Hat (len 16)
        return c < 0.25f ? 6 : (c < 0.65f ? 11 : 13);
    case 5: // Open Hat (len 16)
        return c < 0.35f ? 1 : (c < 0.75f ? 3 : 4);
    case 6: // Perc A (len 7)
        return c < 0.25f ? 2 : (c < 0.65f ? 3 : 5);
    case 7: // Perc B (len 5)
        return c < 0.35f ? 1 : (c < 0.75f ? 2 : 3);
    default:
        return 4;
    }
}

int euclideanDefaultRot(int lane, int variation) noexcept CL_NONBLOCKING {
    const int v = std::max(0, variation - 1);
    switch (lane) {
    case 0:
        return (v / 4) % 4;
    case 1:
        return 4; // Lock snare backbeat cleanly onto beat 2 / 4!
    case 2:
        return (2 + v) % 11;
    case 3:
        return (4 + v * 2) % 12;
    case 4:
        return (v % 2);
    case 5:
        return (2 + v * 2) % 16;
    case 6:
        return (1 + v) % 7;
    case 7:
        return (2 + v) % 5;
    default:
        return 0;
    }
}

} // namespace cl::styles
