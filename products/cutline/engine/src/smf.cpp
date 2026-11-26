// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/smf.h"

#include <hba/core/smf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cl {

namespace {
int toMidi7(double bipolar) { return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127); }
} // namespace

std::vector<std::uint8_t> exportPhraseSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held,
                                          double bpm, int sceneIdx, const PatternBank& bank) {
    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double rateMul = kSeqRateMultipliers[rateIdx];

    PhrasePlan plan;
    compilePhrase(params, sceneIdx, 0, held, plan, bank, safeBpm);

    const double clipBeats = plan.phraseBeats;
    const auto phraseEndTick = static_cast<std::uint32_t>(std::llround(clipBeats * kSmfPpq));
    std::vector<hba::core::SmfEvent> events;
    events.reserve(static_cast<std::size_t>(plan.noteCount * 8 + 256));

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

    struct NoteSpan {
        std::uint8_t note = 0;
        std::uint32_t startTick = 0;
        std::uint32_t endTick = 0;
    };
    std::vector<NoteSpan> scheduled;
    scheduled.reserve(static_cast<std::size_t>(plan.noteCount * 2));

    const int phraseCopies = rateMul > 1.5 ? 2 : 1;
    for (int copy = 0; copy < phraseCopies; ++copy) {
        if (copy > 0)
            compilePhrase(params, sceneIdx, copy, held, plan, bank, safeBpm);
        const double copyOriginBeats = static_cast<double>(copy) * clipBeats;
        for (int i = 0; i < plan.noteCount; ++i) {
            const CompiledNote& n = plan.notes[static_cast<std::size_t>(i)];
            if (n.muted)
                continue;
            const double outStartBeats = (copyOriginBeats + n.warpedBeats) / rateMul;
            if (outStartBeats >= clipBeats - 1.0e-6)
                continue;
            const double outDurBeats = std::max(0.025, n.durationBeats / rateMul);
            const auto startTick = static_cast<std::uint32_t>(std::clamp<long long>(
                std::llround(outStartBeats * kSmfPpq), 0LL, static_cast<long long>(phraseEndTick) - 16LL));
            const auto endTick = static_cast<std::uint32_t>(std::clamp<long long>(
                std::llround((outStartBeats + outDurBeats) * kSmfPpq), static_cast<long long>(startTick) + 12LL,
                static_cast<long long>(phraseEndTick) - 1LL));

            const auto noteByte = static_cast<std::uint8_t>(n.noteNumber & 0x7F);
            for (auto& prev : scheduled) {
                if (prev.note == noteByte && prev.endTick > startTick)
                    prev.endTick = startTick > prev.startTick ? startTick : prev.startTick + 1U;
            }
            scheduled.push_back({noteByte, startTick, endTick});

            const auto sends = evalSends(outStartBeats);
            const int velBias = static_cast<int>(std::lround(laneSum(params, sends, LaneTarget::velocity) * 26.0f));
            const auto vel = static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.velocity) + velBias, 1, 127));
            events.push_back(hba::core::SmfEvent::msg(startTick, 20, 0x90, noteByte, vel));
        }
    }

    for (const auto& sn : scheduled)
        events.push_back(hba::core::SmfEvent::msg(sn.endTick, 18, 0x80, sn.note, 0));

    // Optional CC expression automation (zero Pitch Bend unless explicitly enabled on Pitch Scoop)
    constexpr std::array<std::pair<LaneTarget, int>, 4> kCcMap{{
        {LaneTarget::cc74Slide, 74},
        {LaneTarget::cc1Mod, 1},
        {LaneTarget::cc11Expr, 11},
        {LaneTarget::cc71Res, 71},
    }};
    std::array<int, 4> lastCc{-1, -1, -1, -1};
    int lastAt = -1;
    const bool hasAt = targetActive(LaneTarget::pressure);

    for (double b = 0.0; b < plan.phraseBeats; b += 0.125) {
        const auto tick = static_cast<std::uint32_t>(
            std::min<long long>(std::llround(b * kSmfPpq), static_cast<long long>(phraseEndTick) - 1LL));
        const auto sends = evalSends(b);
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

    return hba::core::encodeSmfType0(std::move(events), kSmfPpq, safeBpm, phraseEndTick);
}

} // namespace cl
