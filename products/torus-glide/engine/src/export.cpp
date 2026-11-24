// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/export.h"

#include "tg/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace tg {

namespace {
void pushBe16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void pushBe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void pushVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
    std::uint8_t buf[5]{};
    int n = 0;
    buf[n++] = static_cast<std::uint8_t>(value & 0x7Fu);
    value >>= 7;
    while (value > 0) {
        buf[n++] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
        value >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        out.push_back(buf[i]);
}

void writeLe16(std::ofstream& f, std::uint16_t v) {
    const char b[2] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu)};
    f.write(b, 2);
}

void writeLe32(std::ofstream& f, std::uint32_t v) {
    const char b[4] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                       static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
    f.write(b, 4);
}
} // namespace

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
    Engine engine;
    engine.prepare(sr, kBlock, request.oversampling);
    engine.setLaneCurves(request.curves);
    engine.setSceneMatrix(request.matrix);
    engine.noteOn(std::clamp(request.note, 12, 96), 0.95f);

    std::vector<float> bufL(kBlock, 0.0f);
    std::vector<float> bufR(kBlock, 0.0f);

    // Pre-warm from ppq = -4.0 up to 0.0 so bar 1.1 starts in exact steady-state phase & envelope!
    int done = 0;
    while (done < warmupSamples) {
        const int n = std::min(kBlock, warmupSamples - done);
        const double ppq = -4.0 + (static_cast<double>(done) / sr) / secondsPerBeat;
        Transport t{bpm, ppq, true};
        engine.process(request.params, t, bufL.data(), bufR.data(), n, nullptr);
        done += n;
    }

    // Record exact bar-locked loop [0 .. loopBeats)
    std::vector<float> interleaved(static_cast<std::size_t>(loopSamples) * 2u, 0.0f);
    done = 0;
    while (done < loopSamples) {
        const int n = std::min(kBlock, loopSamples - done);
        const double ppq = (static_cast<double>(done) / sr) / secondsPerBeat;
        Transport t{bpm, ppq, true};
        engine.process(request.params, t, bufL.data(), bufR.data(), n, nullptr);
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
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(interleavedStereo.size() * sizeof(float));
    const std::uint32_t riffSize = 36u + dataBytes;

    f.write("RIFF", 4);
    writeLe32(f, riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeLe32(f, 16u);
    writeLe16(f, 3u); // IEEE float
    writeLe16(f, 2u); // Stereo
    writeLe32(f, static_cast<std::uint32_t>(sampleRate));
    writeLe32(f, static_cast<std::uint32_t>(sampleRate * 2 * 4));
    writeLe16(f, 8u);
    writeLe16(f, 32u);
    f.write("data", 4);
    writeLe32(f, dataBytes);
    f.write(reinterpret_cast<const char*>(interleavedStereo.data()), static_cast<std::streamsize>(dataBytes));
    return f.good();
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
    const int ratioIdx =
        std::clamp(params.choice(ParamId::spiralRatio), 0, static_cast<int>(kSpiralRatioSemitones.size()) - 1);
    const double ratioSemi = kSpiralRatioSemitones[static_cast<std::size_t>(ratioIdx)];

    struct MidiEvt {
        std::uint32_t tick;
        int order;
        std::uint8_t d0, d1, d2;
        int size;
    };
    std::vector<MidiEvt> evs;
    int seqOrder = 0;
    const auto addMsg = [&](std::uint32_t tick, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, int sz = 3) {
        evs.push_back({tick, seqOrder++, b0, b1, b2, sz});
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
            const double fracBar = beats / loopBeats;
            for (int v = 0; v < kVoiceCount; ++v) {
                const int ci = std::min(v, spiralChord.count - 1);
                p[static_cast<std::size_t>(v)] = static_cast<double>(rootNote) +
                                                 spiralChord.intervals[static_cast<std::size_t>(ci)] +
                                                 fracBar * ratioSemi;
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

    // Sample smooth 14-bit pitch-bend trajectories every 1/32nd note (120 ticks)
    constexpr std::uint32_t kStepTicks = 60; // 1/64th note resolution!
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

    // Note-Offs just before loop end so Ableton clips loop cleanly
    const std::uint32_t offTick = totalTicks > 10 ? totalTicks - 4 : totalTicks;
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto ch = static_cast<std::uint8_t>(v + 1);
        addMsg(offTick, static_cast<std::uint8_t>(0x80 | ch),
               static_cast<std::uint8_t>(baseNotes[static_cast<std::size_t>(v)]), 0);
    }

    std::sort(evs.begin(), evs.end(), [](const MidiEvt& a, const MidiEvt& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    // Build track bytes
    std::vector<std::uint8_t> track;
    // Tempo meta-event at tick 0
    const std::uint32_t usPerBeat = static_cast<std::uint32_t>(std::llround(60'000'000.0 / safeBpm));
    pushVlq(track, 0);
    track.insert(track.end(),
                 {0xFF, 0x51, 0x03, static_cast<std::uint8_t>((usPerBeat >> 16) & 0xFFu),
                  static_cast<std::uint8_t>((usPerBeat >> 8) & 0xFFu), static_cast<std::uint8_t>(usPerBeat & 0xFFu)});

    std::uint32_t prevTick = 0;
    for (const auto& e : evs) {
        pushVlq(track, e.tick - prevTick);
        prevTick = e.tick;
        track.push_back(e.d0);
        if (e.size >= 2)
            track.push_back(e.d1);
        if (e.size >= 3)
            track.push_back(e.d2);
    }
    // End of track at exact totalTicks so DAW loop length matches loopBars!
    pushVlq(track, totalTicks >= prevTick ? (totalTicks - prevTick) : 0u);
    track.insert(track.end(), {0xFF, 0x2F, 0x00});

    std::vector<std::uint8_t> smf;
    smf.insert(smf.end(), {'M', 'T', 'h', 'd'});
    pushBe32(smf, 6u);
    pushBe16(smf, 0u); // Format 0
    pushBe16(smf, 1u); // 1 track
    pushBe16(smf, kPpq);
    smf.insert(smf.end(), {'M', 'T', 'r', 'k'});
    pushBe32(smf, static_cast<std::uint32_t>(track.size()));
    smf.insert(smf.end(), track.begin(), track.end());
    return smf;
}

} // namespace tg
