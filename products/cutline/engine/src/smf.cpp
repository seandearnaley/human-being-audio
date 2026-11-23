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

    // MPE Lower Zone RPN setup if in MPE mode
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
    }

    // Track active end ticks per (ch, note) to prevent overlapping identical Note Ons
    for (int i = 0; i < plan.noteCount; ++i) {
        const CompiledNote& n = plan.notes[static_cast<std::size_t>(i)];
        if (n.muted)
            continue;
        const auto ch = static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.channel), 1, 16) - 1);
        const auto startTick = static_cast<std::uint32_t>(std::clamp<long long>(
            std::llround(n.warpedBeats * kSmfPpq), 0LL, static_cast<long long>(phraseEndTick) - 16LL));
        const auto endTick = static_cast<std::uint32_t>(std::clamp<long long>(
            std::llround((n.warpedBeats + n.durationBeats) * kSmfPpq), static_cast<long long>(startTick) + 12LL,
            static_cast<long long>(phraseEndTick) - 1LL));

        if (mode != OutputMode::drum)
            events.push_back({startTick, 15, {static_cast<std::uint8_t>(0xE0u | ch), 0x00, 0x40}});

        events.push_back({startTick,
                          20,
                          {static_cast<std::uint8_t>(0x90u | ch), static_cast<std::uint8_t>(n.noteNumber & 0x7F),
                           static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.velocity), 1, 127))}});

        if (mode != OutputMode::drum && std::abs(n.bendSemitones) > 0.2f) {
            constexpr int kSteps = 8;
            for (int s = 1; s <= kSteps; ++s) {
                const double frac = static_cast<double>(s) / kSteps;
                const auto t = static_cast<std::uint32_t>(
                    std::llround(static_cast<double>(startTick) + static_cast<double>(endTick - startTick) * frac));
                const int pb = toPb14(static_cast<double>(n.bendSemitones) * (frac * frac), pbRangeSt);
                events.push_back({t,
                                  22,
                                  {static_cast<std::uint8_t>(0xE0u | ch), static_cast<std::uint8_t>(pb & 0x7F),
                                   static_cast<std::uint8_t>((pb >> 7) & 0x7F)}});
            }
        }

        events.push_back(
            {endTick, 18, {static_cast<std::uint8_t>(0x80u | ch), static_cast<std::uint8_t>(n.noteNumber & 0x7F), 0}});
    }

    // Sample continuous CC / Pressure lanes every 1/16 beat (60 ticks at 960 PPQ)
    const double pOrigin = static_cast<double>(phraseIndex) * plan.phraseBeats;
    std::array<int, 5> lastCc{-1, -1, -1, -1, -1};
    int lastAt = -1;
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
        std::array<float, kLaneCount> sends{};
        for (int l = 0; l < kLaneCount; ++l)
            sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, pOrigin + b));

        for (std::size_t k = 0; k < kCcMap.size(); ++k) {
            bool active = false;
            for (int l = 0; l < kLaneCount; ++l) {
                if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == kCcMap[k].first) {
                    active = true;
                    break;
                }
            }
            if (!active)
                continue;
            const int v = toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, kCcMap[k].first)), -1.0, 1.0));
            if (v != lastCc[k]) {
                events.push_back(
                    {tick, 25, {0xB0, static_cast<std::uint8_t>(kCcMap[k].second), static_cast<std::uint8_t>(v)}});
                lastCc[k] = v;
            }
        }

        bool hasAt = false;
        for (int l = 0; l < kLaneCount; ++l) {
            if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == LaneTarget::pressure) {
                hasAt = true;
                break;
            }
        }
        if (hasAt) {
            const int v =
                toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
            if (v != lastAt) {
                events.push_back({tick, 26, {0xD0, static_cast<std::uint8_t>(v)}});
                lastAt = v;
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
