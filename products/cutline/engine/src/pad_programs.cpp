// SPDX-License-Identifier: AGPL-3.0-or-later
// The pad programs that play notes of their own: Repeat and Roll. All six programs are locked to the grid, and each
// returns to the pattern in phase on release, because the pattern's position always comes from the transport. The
// other four need no code here: Density replaces Density and Dynamics in the block, Rate is a multiplier and a lane
// filter, Drop is silence or a lane filter, and Fill is fillNow.
#include "engine_impl.h"
#include "cl/pad_programs.h"

#include <algorithm>
#include <cmath>

namespace cl {

// Remembers the 16th the pad was engaged on. The window loops from there. Engaged before beat 0, it loops from
// beat 0, where it starts to sound.
void Engine::Impl::beginRepeat(double hostBeat, double rate, bool fillNow, int everyBars) noexcept CL_NONBLOCKING {
    const double scaled = std::max(hostBeat, kTimelineStart) * rate;
    repeatAnchor = std::floor(scaled * 4.0 + 1.0e-6) * 0.25;
    repeatBar = static_cast<std::int64_t>(std::floor(repeatAnchor / 4.0));
    repeatFill = fillScheduled(repeatBar, everyBars) || fillNow;
}

// Repeat: loops the resolved hits that start inside the window. X is the window, Y the gate length.
void Engine::Impl::playRepeat(const Block& b) noexcept CL_NONBLOCKING {
    const Params& params = *b.params;
    const Pattern& pat = pattern();
    const int bars = patternBars(pat);
    const double window = pads::repeatWindowBeats(b.padX);
    const double gate = pads::repeatGate(b.padY);
    if (!b.isDrum) {
        // MIDI Slicer: loop the phrase's notes that start inside the window.
        const double phraseBeats = std::max(1.0, slicerPlanData.phraseBeats);
        const double local = repeatAnchor - std::floor(repeatAnchor / phraseBeats) * phraseBeats;
        const double from = std::min(local, phraseBeats - window);
        const double winStart = b.windowStart * b.rate;
        const double winEnd = b.windowEnd * b.rate;
        const auto first =
            std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((winStart - repeatAnchor) / window)));
        const auto last = static_cast<std::int64_t>(std::floor((winEnd - repeatAnchor) / window));
        for (std::int64_t cycle = first; cycle <= last && cycle < first + 64; ++cycle) {
            const double origin = repeatAnchor + static_cast<double>(cycle) * window;
            for (int i = 0; i < slicerPlanData.noteCount; ++i) {
                const SlicerNote& n = slicerPlanData.notes[static_cast<std::size_t>(i)];
                const double offset = n.beat - from;
                if (offset < 0.0 || offset >= window)
                    continue;
                const double at = origin + offset;
                if (at < winStart - kBeatEps || at >= winEnd - kBeatEps)
                    continue;
                const double dur = std::min(n.durBeats, window * 0.85) * gate;
                pushNoteHit(b, n.note, n.voice, at / b.rate, std::max(0.025, dur / b.rate), n.velocity);
            }
        }
        pos.repeatStartStep = static_cast<int>(std::lround(from * 4.0));
        pos.repeatBeats = window;
        return;
    }
    const double barStart = static_cast<double>(repeatBar) * 4.0;
    const double start = std::min(repeatAnchor, barStart + 4.0 - window); // keep the window inside its bar
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                           b.beatsPerMs};

    ResolvedBar resolved;
    resolveBar(pat, densityMap(), floorMod(repeatBar, bars), {b.density, b.dynamics, repeatFill}, resolved);

    const auto firstCycle =
        std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((scaledStart - repeatAnchor) / window)));
    const auto lastCycle = static_cast<std::int64_t>(std::floor((scaledEnd - repeatAnchor) / window));
    for (std::int64_t cycle = firstCycle; cycle <= lastCycle && cycle < firstCycle + 64; ++cycle) {
        const double origin = repeatAnchor + static_cast<double>(cycle) * window;
        forEachHit(pat, resolved, barStart, timing, [&](const Hit& h) noexcept CL_NONBLOCKING {
            const double offset = h.beat - start;
            if (offset < 0.0 || offset >= window)
                return;
            const double at = origin + offset;
            if (at < scaledStart - kBeatEps || at >= scaledEnd - kBeatEps)
                return;
            const double dur = std::min(h.durBeats, window * 0.85) * gate;
            pushDrumHit(b, pat, h.lane, at / b.rate, std::max(0.025, dur / b.rate), h.velocity);
        });
    }

    pos.repeatStartStep =
        floorMod(repeatBar, bars) * kStepsPerBar + static_cast<int>(std::lround((start - barStart) * 4.0));
    pos.repeatBeats = window;
}

// Roll: one lane's note on a grid, rising from velocity 72 to 120 across each bar. X is the rate, Y the lane (kick,
// snare, closed hat); the other lanes keep playing the pattern (playBlock). In MIDI Slicer mode it retriggers the
// held notes.
void Engine::Impl::playRoll(const Block& b) noexcept CL_NONBLOCKING {
    const double patternStep = pads::rollStepBeats(b.padX);
    const double step = patternStep / b.rate; // host beats
    const int lane = pads::rollLane(b.padY);
    auto index = static_cast<std::int64_t>(std::ceil((b.windowStart - kBeatEps) / step));
    for (int guard = 0; guard < 64; ++guard, ++index) {
        const double beat = static_cast<double>(index) * step;
        if (beat >= b.windowEnd - kBeatEps)
            break;
        // Where the note falls in its bar on the pattern's clock, from its grid index so it is exact.
        const double patternBeat = static_cast<double>(index) * patternStep;
        const double inBar = std::max(0.0, patternBeat - std::floor(patternBeat / 4.0 + 1.0e-9) * 4.0);
        const int velocity = pads::rollVelocity(inBar, patternStep);
        if (b.isDrum) {
            pushDrumHit(b, pattern(), lane, beat, step * 0.8, velocity);
        } else {
            // MIDI Slicer: retrigger the held notes.
            for (int i = 0; i < lastEffectiveHeld.count; ++i)
                pushNoteHit(b, lastEffectiveHeld.notes[static_cast<std::size_t>(i)], i, beat, step * 0.8, velocity);
        }
    }
}

} // namespace cl
