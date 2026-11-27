// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/smf.h"

#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/pattern.h"
#include "cl/phrase.h"
#include "cl/resolve.h"

#include <hba/core/smf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cl {

namespace {
int toMidi7(double bipolar) { return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127); }

struct ClipNote {
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};
} // namespace

std::vector<std::uint8_t> exportSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held, double bpm,
                                    int scene, const GrooveBank& bank) {
    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double rate = kSeqRateMultipliers[rateIdx];
    const auto sceneIdx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const GrooveState groove(bank);
    const Pattern& pattern = groove.bank.scenes[sceneIdx];
    const int bars = patternBars(pattern);
    const double clipBeats = static_cast<double>(bars) * 4.0 / rate; // host beats
    const auto clipTicks = static_cast<long long>(std::llround(clipBeats * kSmfPpq));
    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    const auto noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));

    auto evalSends = [&](double beat) {
        std::array<float, kLaneCount> sends{};
        for (int l = 0; l < kLaneCount; ++l)
            sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, beat));
        return sends;
    };
    auto targetActive = [&](LaneTarget target) {
        for (int l = 0; l < kLaneCount; ++l) {
            if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == target)
                return true;
        }
        return false;
    };

    std::vector<ClipNote> notes;
    // `patternBeat` may be negative (a flam, an early nudge or lane offset on bar 1) or past the last bar
    // (a late nudge on the last step). The plugin loops the pattern, so such a hit belongs at the other
    // end of the clip: wrap it.
    const auto add = [&](int note, double patternBeat, double durBeats, int velocity) {
        long long tick = std::llround(patternBeat / rate * kSmfPpq);
        tick = ((tick % clipTicks) + clipTicks) % clipTicks;
        const long long startTick = std::min(tick, clipTicks - 16);
        const long long length = std::llround(std::max(0.025, durBeats / rate) * kSmfPpq);
        const long long endTick = std::clamp(startTick + length, startTick + 12, clipTicks - 1);
        const double hostBeat = static_cast<double>(startTick) / kSmfPpq;
        const int bias =
            static_cast<int>(std::lround(laneSum(params, evalSends(hostBeat), LaneTarget::velocity) * 26.0f));
        notes.push_back({static_cast<std::uint8_t>(std::clamp(note, 0, 127)),
                         static_cast<std::uint8_t>(std::clamp(velocity + bias, 1, 127)),
                         static_cast<std::uint32_t>(startTick), static_cast<std::uint32_t>(endTick)});
    };

    if (mode == EngineMode::drum) {
        const HitTiming timing{static_cast<double>(params[ParamId::swing]),
                               static_cast<double>(params[ParamId::gateLen]), safeBpm / 60000.0 * rate};
        const bool fillOn = params.choice(ParamId::fillEvery) != 0;
        for (int bar = 0; bar < bars; ++bar) {
            ResolvedBar resolved;
            resolveBar(pattern, groove.maps[sceneIdx], bar,
                       {params[ParamId::density], params[ParamId::dynamics], fillOn && bar == bars - 1}, resolved);
            forEachHit(pattern, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) {
                const int note =
                    effectiveLaneMidiNote(noteMap, h.lane, pattern.lanes[static_cast<std::size_t>(h.lane)].midiNote);
                add(note, h.beat, h.durBeats, h.velocity);
            });
        }
    } else {
        const auto plan = std::make_unique<SlicerPlan>();
        compileSlicer(params, groove, static_cast<int>(sceneIdx), held, safeBpm, *plan);
        for (int i = 0; i < plan->noteCount; ++i) {
            const SlicerNote& n = plan->notes[static_cast<std::size_t>(i)];
            add(n.note, n.beat, n.durBeats, n.velocity);
        }
    }

    std::stable_sort(notes.begin(), notes.end(),
                     [](const ClipNote& a, const ClipNote& b) { return a.start < b.start; });

    // Two lanes can share a MIDI note (the Strike Body map does). A pitch can only start once per tick:
    // keep one note, with the higher velocity and the later end.
    std::vector<ClipNote> merged;
    merged.reserve(notes.size());
    for (const ClipNote& n : notes) {
        bool duplicate = false;
        for (auto it = merged.rbegin(); it != merged.rend() && it->start == n.start; ++it) {
            if (it->note != n.note)
                continue;
            it->velocity = std::max(it->velocity, n.velocity);
            it->end = std::max(it->end, n.end);
            duplicate = true;
            break;
        }
        if (!duplicate)
            merged.push_back(n);
    }
    notes = std::move(merged);

    // A note ends when the same pitch starts again.
    for (std::size_t i = 0; i < notes.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (notes[j].note == notes[i].note && notes[j].end > notes[i].start)
                notes[j].end = notes[i].start > notes[j].start ? notes[i].start : notes[j].start + 1U;
        }
    }

    const auto endTick = static_cast<std::uint32_t>(clipTicks);
    std::vector<hba::core::SmfEvent> events;
    events.reserve(notes.size() * 2 + 256);
    for (const auto& n : notes) {
        events.push_back(hba::core::SmfEvent::msg(n.start, 20, 0x90, n.note, n.velocity));
        events.push_back(hba::core::SmfEvent::msg(n.end, 18, 0x80, n.note, 0));
    }

    // Expression lanes as CC and aftertouch. Pitch bend is written only if a lane targets Pitch Scoop,
    // and this exporter never writes it.
    constexpr std::array<std::pair<LaneTarget, int>, 4> kCcMap{{
        {LaneTarget::cc74Slide, 74},
        {LaneTarget::cc1Mod, 1},
        {LaneTarget::cc11Expr, 11},
        {LaneTarget::cc71Res, 71},
    }};
    std::array<int, 4> lastCc{-1, -1, -1, -1};
    int lastAt = -1;
    const bool hasAt = targetActive(LaneTarget::pressure);
    for (double beat = 0.0; beat < clipBeats; beat += 0.125) {
        const auto tick = static_cast<std::uint32_t>(
            std::min<long long>(std::llround(beat * kSmfPpq), static_cast<long long>(endTick) - 1LL));
        const auto sends = evalSends(beat);
        for (std::size_t k = 0; k < kCcMap.size(); ++k) {
            if (!targetActive(kCcMap[k].first))
                continue;
            const int v = toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, kCcMap[k].first)), -1.0, 1.0));
            if (lastCc[k] != v) {
                events.push_back(hba::core::SmfEvent::msg(tick, 25, 0xB0, static_cast<std::uint8_t>(kCcMap[k].second),
                                                          static_cast<std::uint8_t>(v)));
                lastCc[k] = v;
            }
        }
        if (hasAt) {
            const int v =
                toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
            if (lastAt != v) {
                events.push_back(hba::core::SmfEvent::msg2(tick, 26, 0xD0, static_cast<std::uint8_t>(v)));
                lastAt = v;
            }
        }
    }

    return hba::core::encodeSmfType0(std::move(events), kSmfPpq, safeBpm, endTick);
}

} // namespace cl
