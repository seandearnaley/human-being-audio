// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/smf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cl {

namespace {

struct SmfEvent {
    std::uint32_t tick = 0;
    int order = 0;
    std::vector<std::uint8_t> bytes;
};

void appendVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
    std::uint8_t buf[5]{};
    int n = 0;
    buf[n++] = static_cast<std::uint8_t>(value & 0x7Fu);
    value >>= 7U;
    while (value > 0U) {
        buf[n++] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
        value >>= 7U;
    }
    for (int i = n - 1; i >= 0; --i)
        out.push_back(buf[i]);
}

int toMidi7(double bipolar) { return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127); }

int toPb14(double semitones, double rangeSt) {
    const double norm = std::clamp(semitones / std::max(1.0, rangeSt), -1.0, 1.0);
    return std::clamp(static_cast<int>(std::lround(8192.0 + norm * 8191.0)), 0, 16383);
}

} // namespace

std::vector<std::uint8_t> exportPhraseSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held,
                                          double bpm, std::int64_t phraseIndex, const PhraseOverrides& overrides) {
    PhrasePlan plan;
    compilePhrase(params, phraseIndex, held, plan, overrides);

    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const auto usPerQuarter = static_cast<std::uint32_t>(std::llround(60000000.0 / safeBpm));
    const auto mode = static_cast<OutputMode>(std::clamp(params.choice(ParamId::outputMode), 0, 2));
    const auto pbIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::bendRange), 0, 2));
    const double pbRangeSt = mode == OutputMode::mpe ? 48.0 : static_cast<double>(kBendRangeSemitones[pbIdx]);
    const auto phraseEndTick = static_cast<std::uint32_t>(std::llround(plan.phraseBeats * kSmfPpq));

    std::vector<SmfEvent> events;
    events.reserve(static_cast<std::size_t>(plan.noteCount * 10 + 256));

    // Tempo Meta Event
    events.push_back(
        {0,
         0,
         {0xFF, 0x51, 0x03, static_cast<std::uint8_t>((usPerQuarter >> 16U) & 0xFFU),
          static_cast<std::uint8_t>((usPerQuarter >> 8U) & 0xFFU), static_cast<std::uint8_t>(usPerQuarter & 0xFFU)}});

    // Setup RPN: MPE Lower Zone (Ch 1 MCM + Ch 2 ±48 st) or Mono Bend Range (Ch 1 RPN 00 00)
    if (mode == OutputMode::mpe) {
        events.push_back({0, 1, {0xB0, 101, 0}});
        events.push_back({0, 2, {0xB0, 100, 6}});
        events.push_back({0, 3, {0xB0, 6, 14}});
        events.push_back({0, 4, {0xB0, 101, 127}});
        events.push_back({0, 5, {0xB0, 100, 127}});
        events.push_back({0, 6, {0xB1, 101, 0}});
        events.push_back({0, 7, {0xB1, 100, 0}});
        events.push_back({0, 8, {0xB1, 6, 48}});
        events.push_back({0, 9, {0xB1, 101, 127}});
        events.push_back({0, 10, {0xB1, 100, 127}});
    } else if (mode == OutputMode::mono) {
        const auto bendSt = static_cast<std::uint8_t>(std::clamp(static_cast<int>(pbRangeSt), 1, 48));
        events.push_back({0, 1, {0xB0, 101, 0}});
        events.push_back({0, 2, {0xB0, 100, 0}});
        events.push_back({0, 3, {0xB0, 6, bendSt}});
        events.push_back({0, 4, {0xB0, 101, 127}});
        events.push_back({0, 5, {0xB0, 100, 127}});
    }

    const double pOrigin = static_cast<double>(phraseIndex) * plan.phraseBeats;
    auto evalSends = [&](double beat) {
        std::array<float, kLaneCount> sends{};
        for (int l = 0; l < kLaneCount; ++l)
            sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, pOrigin + beat));
        return sends;
    };
    auto targetActive = [&](LaneTarget target) {
        for (int l = 0; l < kLaneCount; ++l) {
            if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == target)
                return true;
        }
        return false;
    };
    const bool hasCc74 = targetActive(LaneTarget::cc74Slide);
    const bool hasAt = targetActive(LaneTarget::pressure);
    const bool hasPbLane = targetActive(LaneTarget::pitchBend);

    struct NoteSpan {
        std::uint8_t ch = 0;
        std::uint8_t note = 0;
        std::uint32_t startTick = 0;
        std::uint32_t endTick = 0;
        float bendSemitones = 0.0f;
    };
    std::vector<NoteSpan> scheduledNotes;
    scheduledNotes.reserve(static_cast<std::size_t>(plan.noteCount));

    // Precompute non-overlapping note intervals per (channel, note) and dynamically avoid MPE channel collisions.
    std::array<std::uint32_t, 16> mpeChanFreeTick{};
    int nextMpeCh = 1; // 0-based channel 1..14 => MIDI Ch 2..15

    for (int i = 0; i < plan.noteCount; ++i) {
        const CompiledNote& n = plan.notes[static_cast<std::size_t>(i)];
        if (n.muted)
            continue;
        const auto startTick = static_cast<std::uint32_t>(std::clamp<long long>(
            std::llround(n.warpedBeats * kSmfPpq), 0LL, static_cast<long long>(phraseEndTick) - 16LL));
        const auto endTick = static_cast<std::uint32_t>(std::clamp<long long>(
            std::llround((n.warpedBeats + n.durationBeats) * kSmfPpq), static_cast<long long>(startTick) + 12LL,
            static_cast<long long>(phraseEndTick) - 1LL));

        std::uint8_t ch = static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.channel), 1, 16) - 1);
        if (mode == OutputMode::mpe) {
            int picked = -1;
            for (int t = 0; t < 14; ++t) {
                const int cand = 1 + ((nextMpeCh - 1 + t) % 14);
                if (mpeChanFreeTick[static_cast<std::size_t>(cand)] <= startTick) {
                    picked = cand;
                    break;
                }
            }
            if (picked < 0) {
                picked = 1;
                for (int c = 2; c <= 14; ++c) {
                    if (mpeChanFreeTick[static_cast<std::size_t>(c)] <
                        mpeChanFreeTick[static_cast<std::size_t>(picked)])
                        picked = c;
                }
            }
            ch = static_cast<std::uint8_t>(picked);
            nextMpeCh = 1 + ((picked - 1 + 1) % 14);
            mpeChanFreeTick[static_cast<std::size_t>(ch)] = endTick;
        }

        // Trim any earlier overlapping note on the same (ch, note) (or same MPE member channel) so
        // legato intervals never collide or cut off the incoming note.
        const auto noteByte = static_cast<std::uint8_t>(n.noteNumber & 0x7F);
        for (auto& prev : scheduledNotes) {
            const bool collision =
                (prev.ch == ch) && (mode == OutputMode::mpe || prev.note == noteByte) && (prev.endTick > startTick);
            if (collision)
                prev.endTick = startTick > prev.startTick ? startTick : prev.startTick + 1U;
        }

        scheduledNotes.push_back({ch, noteByte, startTick, endTick, n.bendSemitones});

        const auto startSends = evalSends(n.warpedBeats);
        const int velBias = static_cast<int>(std::lround(laneSum(params, startSends, LaneTarget::velocity) * 28.0f));
        const auto vel = static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.velocity) + velBias, 1, 127));

        if (mode != OutputMode::drum) {
            const double laneBendSt =
                static_cast<double>(std::clamp(laneSum(params, startSends, LaneTarget::pitchBend), -1.0f, 1.0f)) *
                std::min(pbRangeSt, 12.0);
            const int initPb = mode == OutputMode::mpe ? toPb14(laneBendSt * 0.5, 48.0) : toPb14(laneBendSt, pbRangeSt);
            events.push_back({startTick,
                              15,
                              {static_cast<std::uint8_t>(0xE0u | ch), static_cast<std::uint8_t>(initPb & 0x7F),
                               static_cast<std::uint8_t>((initPb >> 7) & 0x7F)}});
            if (mode == OutputMode::mpe) {
                if (hasCc74) {
                    const int v74 = toMidi7(
                        std::clamp(static_cast<double>(laneSum(params, startSends, LaneTarget::cc74Slide)), -1.0, 1.0));
                    events.push_back(
                        {startTick, 16, {static_cast<std::uint8_t>(0xB0u | ch), 74, static_cast<std::uint8_t>(v74)}});
                }
                if (hasAt) {
                    const int vAt = toMidi7(
                        std::clamp(static_cast<double>(laneSum(params, startSends, LaneTarget::pressure)), -1.0, 1.0));
                    events.push_back(
                        {startTick, 17, {static_cast<std::uint8_t>(0xD0u | ch), static_cast<std::uint8_t>(vAt)}});
                }
            }
        }

        events.push_back({startTick, 20, {static_cast<std::uint8_t>(0x90u | ch), noteByte, vel}});
    }

    // Emit trimmed Note Offs (order 18 < NoteOn 20 so contiguous notes release before re-triggering)
    for (const auto& sn : scheduledNotes) {
        events.push_back({sn.endTick, 18, {static_cast<std::uint8_t>(0x80u | sn.ch), sn.note, 0}});
    }

    // Sample continuous CC / Pressure / Pitch Bend lanes every 1/16 beat (60 ticks at 960 PPQ)
    std::array<std::array<int, 16>, 5> lastCc{};
    for (auto& row : lastCc)
        row.fill(-1);
    std::array<int, 16> lastAt{};
    lastAt.fill(-1);
    std::array<int, 16> lastPb{};
    lastPb.fill(-1);

    constexpr std::array<std::pair<LaneTarget, int>, 5> kCcMap{{
        {LaneTarget::cc74Slide, 74},
        {LaneTarget::cc1Mod, 1},
        {LaneTarget::cc11Expr, 11},
        {LaneTarget::cc2Breath, 2},
        {LaneTarget::cc71Res, 71},
    }};

    for (double b = 0.0; b < plan.phraseBeats; b += 0.0625) {
        const auto tick = static_cast<std::uint32_t>(
            std::min<long long>(std::llround(b * kSmfPpq), static_cast<long long>(phraseEndTick) - 1LL));
        const auto sends = evalSends(b);

        for (std::size_t k = 0; k < kCcMap.size(); ++k) {
            if (!targetActive(kCcMap[k].first))
                continue;
            const int v = toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, kCcMap[k].first)), -1.0, 1.0));
            if (mode == OutputMode::mpe && kCcMap[k].first == LaneTarget::cc74Slide) {
                for (const auto& sn : scheduledNotes) {
                    if (tick >= sn.startTick && tick < sn.endTick && lastCc[k][sn.ch] != v) {
                        events.push_back({tick,
                                          25,
                                          {static_cast<std::uint8_t>(0xB0u | sn.ch),
                                           static_cast<std::uint8_t>(kCcMap[k].second), static_cast<std::uint8_t>(v)}});
                        lastCc[k][sn.ch] = v;
                    }
                }
            } else if (lastCc[k][0] != v) {
                events.push_back(
                    {tick, 25, {0xB0, static_cast<std::uint8_t>(kCcMap[k].second), static_cast<std::uint8_t>(v)}});
                lastCc[k][0] = v;
            }
        }

        if (hasAt) {
            const int v =
                toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
            if (mode == OutputMode::mpe) {
                for (const auto& sn : scheduledNotes) {
                    if (tick >= sn.startTick && tick < sn.endTick && lastAt[sn.ch] != v) {
                        events.push_back(
                            {tick, 26, {static_cast<std::uint8_t>(0xD0u | sn.ch), static_cast<std::uint8_t>(v)}});
                        lastAt[sn.ch] = v;
                    }
                }
            } else if (lastAt[0] != v) {
                events.push_back({tick, 26, {0xD0, static_cast<std::uint8_t>(v)}});
                lastAt[0] = v;
            }
        }

        if (mode != OutputMode::drum) {
            const double laneBendSt =
                hasPbLane
                    ? static_cast<double>(std::clamp(laneSum(params, sends, LaneTarget::pitchBend), -1.0f, 1.0f)) *
                          std::min(pbRangeSt, 12.0)
                    : 0.0;
            if (mode == OutputMode::mpe) {
                for (const auto& sn : scheduledNotes) {
                    if (tick < sn.startTick || tick >= sn.endTick)
                        continue;
                    if (!hasPbLane && std::abs(sn.bendSemitones) <= 0.2f)
                        continue;
                    const double dur = std::max(1.0, static_cast<double>(sn.endTick - sn.startTick));
                    const double frac = std::clamp(static_cast<double>(tick - sn.startTick) / dur, 0.0, 1.0);
                    const int pb =
                        toPb14(static_cast<double>(sn.bendSemitones) * (frac * frac) + laneBendSt * 0.5, 48.0);
                    if (lastPb[sn.ch] < 0 || std::abs(pb - lastPb[sn.ch]) >= 16) {
                        events.push_back(
                            {tick,
                             22,
                             {static_cast<std::uint8_t>(0xE0u | sn.ch), static_cast<std::uint8_t>(pb & 0x7F),
                              static_cast<std::uint8_t>((pb >> 7) & 0x7F)}});
                        lastPb[sn.ch] = pb;
                    }
                }
            } else {
                const NoteSpan* newest = nullptr;
                for (const auto& sn : scheduledNotes) {
                    if (tick >= sn.startTick && tick < sn.endTick &&
                        (newest == nullptr || sn.startTick >= newest->startTick)) {
                        newest = &sn;
                    }
                }
                if (newest != nullptr && (hasPbLane || std::abs(newest->bendSemitones) > 0.2f)) {
                    const double dur = std::max(1.0, static_cast<double>(newest->endTick - newest->startTick));
                    const double frac = std::clamp(static_cast<double>(tick - newest->startTick) / dur, 0.0, 1.0);
                    const int pb =
                        toPb14(static_cast<double>(newest->bendSemitones) * (frac * frac) + laneBendSt, pbRangeSt);
                    if (lastPb[0] < 0 || std::abs(pb - lastPb[0]) >= 16) {
                        events.push_back({tick,
                                          22,
                                          {0xE0, static_cast<std::uint8_t>(pb & 0x7F),
                                           static_cast<std::uint8_t>((pb >> 7) & 0x7F)}});
                        lastPb[0] = pb;
                    }
                }
            }
        }
    }

    // End of Track strictly last
    events.push_back({phraseEndTick, 99, {0xFF, 0x2F, 0x00}});

    std::stable_sort(events.begin(), events.end(), [](const SmfEvent& a, const SmfEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.order < b.order;
    });

    std::vector<std::uint8_t> track;
    track.reserve(events.size() * 5);
    std::uint32_t prevTick = 0;
    for (const auto& ev : events) {
        const std::uint32_t delta = ev.tick >= prevTick ? ev.tick - prevTick : 0U;
        prevTick = ev.tick;
        appendVlq(track, delta);
        track.insert(track.end(), ev.bytes.begin(), ev.bytes.end());
    }

    std::vector<std::uint8_t> file;
    file.reserve(22 + track.size());
    // MThd header (Type 0, 1 track, 960 PPQ)
    const std::array<std::uint8_t, 14> header{
        0x4D,
        0x54,
        0x68,
        0x64,
        0x00,
        0x00,
        0x00,
        0x06,
        0x00,
        0x00,
        0x00,
        0x01,
        static_cast<std::uint8_t>((kSmfPpq >> 8) & 0xFF),
        static_cast<std::uint8_t>(kSmfPpq & 0xFF),
    };
    file.insert(file.end(), header.begin(), header.end());

    const auto trkLen = static_cast<std::uint32_t>(track.size());
    const std::array<std::uint8_t, 8> trkHead{
        0x4D,
        0x54,
        0x72,
        0x6B,
        static_cast<std::uint8_t>((trkLen >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((trkLen >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((trkLen >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(trkLen & 0xFFU),
    };
    file.insert(file.end(), trkHead.begin(), trkHead.end());
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

} // namespace cl
