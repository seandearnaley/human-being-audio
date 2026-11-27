// SPDX-License-Identifier: AGPL-3.0-or-later
// Turns a resolved bar into timed hits. Swing, ratchets, nudge, lane offset and flams are all
// applied here and nowhere else, so live playback, MIDI export and tests agree on timing.
#pragma once

#include "cl/groove.h"
#include "cl/nonblocking.h"
#include "cl/resolve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cl {

inline constexpr double kFlamMs = 20.0;
inline constexpr int kFlamVelocityDrop = 40;
inline constexpr int kOpenHatLane = 5;
inline constexpr int kPercBLane = 7;
inline constexpr int kMaxNudge = 24;            // 1/96 beats
inline constexpr int kMaxOffsetMs = 20;         // whole-lane pocket
inline constexpr double kMaxSwingBeats = 0.125; // a fully swung odd 16th is delayed by this much

// Open hat and perc B ring for a fixed length; the other lanes follow Gate.
constexpr bool isLongNoteLane(int lane) noexcept CL_NONBLOCKING { return lane == kOpenHatLane || lane == kPercBLane; }

// How far before its step a hit can start: a full negative nudge, a full negative lane offset and the flam.
inline double maxEarlyBeats(double beatsPerMs) noexcept CL_NONBLOCKING {
    return static_cast<double>(kMaxNudge) / 96.0 + (static_cast<double>(kMaxOffsetMs) + kFlamMs) * beatsPerMs;
}

// How far after the end of its straight-grid step a hit can start: a full positive nudge, a full positive
// lane offset, and the swing slack. Swing stretches an even step by up to 0.125 beats, so the last ratchet of
// a swung step starts that much past the unswung step end.
inline double maxLateBeats(double beatsPerMs) noexcept CL_NONBLOCKING {
    return static_cast<double>(kMaxNudge) / 96.0 + kMaxSwingBeats + static_cast<double>(kMaxOffsetMs) * beatsPerMs;
}

// Bars between fills for each Fill choice: Off, 2, 4, 8, 16.
inline constexpr std::array<int, 5> kFillEveryBars{0, 2, 4, 8, 16};

// MPC-style 16th swing as a strictly increasing map of beat position. Even 16ths never move;
// odd 16ths are delayed by swing * 0.125 beats (0 = straight, 0.67 = triplet, 1 = dotted).
inline double swingBeat(double beat, double swing) noexcept CL_NONBLOCKING {
    const double s = std::clamp(swing, 0.0, 1.0);
    const double pair = std::floor(beat / 0.5);
    const double local = beat - pair * 0.5;
    const double mid = 0.25 + 0.125 * s;
    const double warped = local <= 0.25 ? (local / 0.25) * mid : mid + ((local - 0.25) / 0.25) * (0.5 - mid);
    return pair * 0.5 + warped;
}

// Start of `step` within its bar, in beats. Step 16 is the next bar's downbeat (4.0).
inline double swungStepBeats(int step, double swing) noexcept CL_NONBLOCKING {
    return swingBeat(static_cast<double>(step) * 0.25, swing);
}

// A fill set to every N bars fires in bars N, 2N, 3N... of a ruler that counts from 1.
constexpr bool fillScheduled(std::int64_t absBar, int everyBars) noexcept CL_NONBLOCKING {
    return everyBars > 0 && absBar >= 0 && ((absBar + 1) % everyBars) == 0;
}

struct Hit {
    double beat = 0.0;     // absolute start, in pattern beats; may precede its bar or be negative
    double durBeats = 0.1; // note length
    std::int8_t lane = 0;
    std::int8_t step = 0; // step in bar that produced it
    std::uint8_t velocity = 0;
    bool grace = false; // the flam's early hit
};

struct HitTiming {
    double swing = 0.0;      // 0..1
    double gateLen = 0.6;    // 0..1
    double beatsPerMs = 0.0; // pattern beats per millisecond at the current tempo and rate
};

// Calls fn(const Hit&) for every note-on of one resolved bar, lane by lane, step by step.
template <typename Fn>
void forEachHit(const Pattern& pattern, const ResolvedBar& bar, double barOriginBeats, const HitTiming& timing,
                Fn&& fn) {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const auto li = static_cast<std::size_t>(lane);
        const int offsetMs = std::clamp(static_cast<int>(pattern.lanes[li].offsetMs), -kMaxOffsetMs, kMaxOffsetMs);
        const double offsetBeats = static_cast<double>(offsetMs) * timing.beatsPerMs;
        const double baseDur = isLongNoteLane(lane) ? 0.36 : 0.12 + 0.14 * std::clamp(timing.gateLen, 0.0, 1.0);
        for (int step = 0; step < kStepsPerBar; ++step) {
            const ResolvedStep& r = bar.lanes[li][static_cast<std::size_t>(step)];
            if (r.velocity == 0)
                continue;
            const double start = barOriginBeats + swungStepBeats(step, timing.swing);
            const double next = barOriginBeats + swungStepBeats(step + 1, timing.swing);
            const int count = std::clamp(static_cast<int>(r.ratchet), 1, 4);
            const double interval = (next - start) / static_cast<double>(count);
            const double base = start + static_cast<double>(r.nudge) / 96.0 + offsetBeats;
            if (r.flam) {
                Hit grace;
                grace.beat = base - kFlamMs * timing.beatsPerMs;
                grace.durBeats = 0.08;
                grace.lane = static_cast<std::int8_t>(lane);
                grace.step = static_cast<std::int8_t>(step);
                grace.velocity =
                    static_cast<std::uint8_t>(std::max(1, static_cast<int>(r.velocity) - kFlamVelocityDrop));
                grace.grace = true;
                fn(grace);
            }
            for (int i = 0; i < count; ++i) {
                Hit hit;
                hit.beat = base + static_cast<double>(i) * interval;
                hit.durBeats = count > 1 ? std::min(baseDur, interval * 0.78) : baseDur;
                hit.lane = static_cast<std::int8_t>(lane);
                hit.step = static_cast<std::int8_t>(step);
                hit.velocity = r.velocity;
                fn(hit);
            }
        }
    }
}

} // namespace cl
