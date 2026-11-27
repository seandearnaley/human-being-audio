// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/export.h"

#include "tg/engine.h"

#include <hba/core/smf.h>
#include <hba/core/wav.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace tg {

std::vector<float> renderOfflineLoop(const RenderRequest& request) {
    const double sr = request.sampleRate > 8000.0 ? request.sampleRate : 48000.0;
    const double bpm = std::clamp(request.bpm, 30.0, 300.0);
    const int loopIdx =
        std::clamp(request.params.choice(ParamId::loopBars), 0, static_cast<int>(kLoopBarBeats.size()) - 1);
    const double loopBeats = kLoopBarBeats[static_cast<std::size_t>(loopIdx)];
    const double secondsPerBeat = 60.0 / bpm;

    const int loopSamples = std::max(256, static_cast<int>(std::llround(loopBeats * secondsPerBeat * sr)));
    const int warmupSamples =
        std::max(256, static_cast<int>(std::llround(4.0 * secondsPerBeat * sr))); // 1 bar pre-warm

    constexpr int kBlock = 256;
    auto engine = std::make_unique<Engine>();
    engine->prepare(sr, kBlock, request.oversampling);
    engine->setLaneCurves(request.curves);
    engine->setSceneMatrix(request.matrix);
    engine->noteOn(std::clamp(request.note, 12, 96), 0.95f);

    const int latency = std::max(0, engine->latencySamples());
    std::vector<float> bufL(kBlock, 0.0f);
    std::vector<float> bufR(kBlock, 0.0f);

    // Pre-warm from ppq = -4.0 up to 0.0 + latency pre-roll so recorded sample 0 lands on exact bar 1.1!
    const int totalWarmup = warmupSamples + latency;
    int done = 0;
    while (done < totalWarmup) {
        const int n = std::min(kBlock, totalWarmup - done);
        const double ppq = -4.0 + (static_cast<double>(done) / sr) / secondsPerBeat;
        Transport t{bpm, ppq, true};
        engine->process(request.params, t, bufL.data(), bufR.data(), n, nullptr);
        done += n;
    }

    // Record exact bar-locked loop [0 .. loopBeats) (shifted forward by latency so delayed output is aligned)
    std::vector<float> interleaved(static_cast<std::size_t>(loopSamples) * 2u, 0.0f);
    done = 0;
    while (done < loopSamples) {
        const int n = std::min(kBlock, loopSamples - done);
        const double ppq = (static_cast<double>(done + latency) / sr) / secondsPerBeat;
        Transport t{bpm, ppq, true};
        engine->process(request.params, t, bufL.data(), bufR.data(), n, nullptr);
        for (int i = 0; i < n; ++i) {
            const auto idx = static_cast<std::size_t>(done + i) * 2u;
            interleaved[idx] = bufL[static_cast<std::size_t>(i)];
            interleaved[idx + 1] = bufR[static_cast<std::size_t>(i)];
        }
        done += n;
    }
    return interleaved;
}

bool writeWavFloat(const std::string& path, const std::vector<float>& interleavedStereo, int sampleRate) {
    return hba::core::writeWavFloat(path, interleavedStereo, 2, sampleRate);
}

std::vector<std::uint8_t> exportMpeSmf(const Params& params, const LaneCurves& curves, const SceneMatrix& matrix,
                                       int rootNote, double bpm) {
    constexpr std::uint16_t kPpq = 960;
    const double safeBpm = std::clamp(bpm, 30.0, 300.0);
    const int loopIdx = std::clamp(params.choice(ParamId::loopBars), 0, static_cast<int>(kLoopBarBeats.size()) - 1);
    const double loopBeats = kLoopBarBeats[static_cast<std::size_t>(loopIdx)];
    const std::uint32_t totalTicks = static_cast<std::uint32_t>(std::llround(loopBeats * kPpq));

    const auto mode = static_cast<Mode>(std::clamp(params.choice(ParamId::mode), 0, 2));
    const int rootPc = std::clamp(params.choice(ParamId::rootKey), 0, 11);
    const int scaleIdx = std::clamp(params.choice(ParamId::scale), 0, kScaleCount - 1);
    const auto gravMode = static_cast<GravityMode>(std::clamp(params.choice(ParamId::gravityMode), 0, 2));
    const double baseGravity = std::clamp(static_cast<double>(params.real(ParamId::gravity)) * 0.01, 0.0, 1.0);
    const double stagger = std::clamp(static_cast<double>(params.real(ParamId::stagger)) * 0.01, 0.0, 1.0);
    const auto staggerOrder = static_cast<StaggerOrder>(std::clamp(params.choice(ParamId::staggerOrder), 0, 3));
    const bool anchorSub = params.flag(ParamId::anchorSub);
    const int rangeSt =
        static_cast<int>(kMpeRanges[static_cast<std::size_t>(std::clamp(params.choice(ParamId::mpeRange), 0, 2))]);
    const int lane1Idx = std::clamp(params.choice(ParamId::lane1Rate), 0, static_cast<int>(kLoopBarBeats.size()) - 1);
    const double lane1Beats = kLoopBarBeats[static_cast<std::size_t>(lane1Idx)];
    const double lane1Depth = static_cast<double>(params.real(ParamId::lane1Depth)) * 0.01;
    const bool morphAuto = params.flag(ParamId::morphAuto);
    const double baseMorph = std::clamp(static_cast<double>(params.real(ParamId::morph)) * 0.01, 0.0, 1.0);
    const SpiralChord spiralChord = spiralChordForStack(params.choice(ParamId::spiralStack));
    const auto spiralDir = static_cast<SpiralDir>(std::clamp(params.choice(ParamId::spiralDir), 0, 2));
    const double phaseOffset = std::clamp(static_cast<double>(params.real(ParamId::phaseOffset)) * 0.01, 0.0, 1.0);
    const int ratioIdx =
        std::clamp(params.choice(ParamId::spiralRatio), 0, static_cast<int>(kSpiralRatioSemitones.size()) - 1);
    const double ratioSemi = kSpiralRatioSemitones[static_cast<std::size_t>(ratioIdx)];

    std::array<hba::core::SmfTrack, kVoiceCount + 1> tracks;
    tracks[0].name = "MPE setup";
    for (int v = 0; v < kVoiceCount; ++v)
        tracks[static_cast<std::size_t>(v + 1)].name = "Voice " + std::to_string(v + 1);

    int seqOrder = 0;
    const auto addMsg = [&](std::uint32_t tick, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2,
                            std::uint8_t sz = 3) {
        tracks[b0 & 0x0fu].events.push_back(hba::core::SmfEvent::msg(tick, seqOrder++, b0, b1, b2, sz));
    };

    // MPE Lower Zone MCM + per-channel RPN 00 00 at tick 0
    addMsg(0, 0xB0, 101, 0);
    addMsg(0, 0xB0, 100, 6);
    addMsg(0, 0xB0, 6, static_cast<std::uint8_t>(kVoiceCount));
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto ch = static_cast<std::uint8_t>(v + 1);
        addMsg(0, static_cast<std::uint8_t>(0xB0 | ch), 101, 0);
        addMsg(0, static_cast<std::uint8_t>(0xB0 | ch), 100, 0);
        addMsg(0, static_cast<std::uint8_t>(0xB0 | ch), 6, static_cast<std::uint8_t>(rangeSt));
        addMsg(0, static_cast<std::uint8_t>(0xB0 | ch), 38, 0);
    }

    const auto samplePitchesAt = [&](double beats) {
        std::array<double, kVoiceCount> p{};
        const double l1Val = static_cast<double>(hba::dsp::evaluate(curves[0], beats / lane1Beats));
        const double morph01 = morphAuto ? std::clamp(0.5 * (l1Val + 1.0) * lane1Depth + baseMorph, 0.0, 1.0)
                                         : std::clamp(baseMorph + 0.5 * l1Val * lane1Depth, 0.0, 1.0);
        if (mode == Mode::spiral) {
            const double cycleCount = (beats / loopBeats) + phaseOffset;
            const double barPhase = hba::dsp::frac(cycleCount);
            double spiralPos = cycleCount;
            if (spiralDir == SpiralDir::fall)
                spiralPos = -cycleCount;
            else if (spiralDir == SpiralDir::pendulum)
                spiralPos = 0.5 - 0.5 * std::cos(hba::dsp::kTwoPi * barPhase);
            for (int v = 0; v < kVoiceCount; ++v) {
                const int ci = std::min(v, spiralChord.count - 1);
                p[static_cast<std::size_t>(v)] = static_cast<double>(rootNote) +
                                                 spiralChord.intervals[static_cast<std::size_t>(ci)] +
                                                 spiralPos * ratioSemi;
            }
        } else {
            const auto eval = evaluateMorphPitches(matrix, static_cast<double>(rootNote), rootPc, scaleIdx,
                                                   mode == Mode::mobius ? (beats / loopBeats) : morph01, baseGravity,
                                                   gravMode, stagger, staggerOrder, anchorSub, mode == Mode::mobius);
            p = eval.pitches;
        }
        return p;
    };

    const auto startPitches = samplePitchesAt(0.0);
    std::array<int, kVoiceCount> baseNotes{};
    std::array<int, kVoiceCount> lastBends{};
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const auto ch = static_cast<std::uint8_t>(v + 1);
        baseNotes[vi] = std::clamp(static_cast<int>(std::lround(startPitches[vi])), 12, 116);
        lastBends[vi] = 8192;
        addMsg(0, static_cast<std::uint8_t>(0xE0 | ch), 0, 64);
        addMsg(0, static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(baseNotes[vi]), 110);
    }

    // Sample smooth 14-bit pitch-bend trajectories every 1/64th note (60 ticks at 960 PPQ)
    constexpr std::uint32_t kStepTicks = 60;
    for (std::uint32_t tick = kStepTicks; tick < totalTicks; tick += kStepTicks) {
        const auto pitches = samplePitchesAt(static_cast<double>(tick) / kPpq);
        for (int v = 0; v < kVoiceCount; ++v) {
            const auto vi = static_cast<std::size_t>(v);
            const auto ch = static_cast<std::uint8_t>(v + 1);
            const double bendSt = std::clamp(pitches[vi] - static_cast<double>(baseNotes[vi]),
                                             -static_cast<double>(rangeSt), static_cast<double>(rangeSt));
            const int bend14 =
                std::clamp(static_cast<int>(std::lround(8192.0 + (bendSt / rangeSt) * 8191.0)), 0, 16383);
            if (std::abs(bend14 - lastBends[vi]) >= 4) {
                addMsg(tick, static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(bend14 & 0x7F),
                       static_cast<std::uint8_t>((bend14 >> 7) & 0x7F));
                lastBends[vi] = bend14;
            }
        }
    }

    // Note-Offs just before loop end so DAW clips loop cleanly
    const std::uint32_t offTick = totalTicks > 10 ? totalTicks - 4 : totalTicks;
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto ch = static_cast<std::uint8_t>(v + 1);
        addMsg(offTick, static_cast<std::uint8_t>(0x80 | ch),
               static_cast<std::uint8_t>(baseNotes[static_cast<std::size_t>(v)]), 0);
    }

    return hba::core::encodeSmf(std::move(tracks), kPpq, safeBpm, totalTicks);
}

} // namespace tg
