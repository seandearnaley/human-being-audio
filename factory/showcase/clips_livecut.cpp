// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "engine/Cutter.h"
#include "engine/PadPrograms.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 1. LIVECUT REDUX (6 clips)
// ============================================================================
struct ShowcaseSourceContext final : livecut::engine::Cutter::SourceContext {
    std::optional<livecut::engine::Cutter::TransportBounds>
    sourceBoundsAt(std::int64_t) const noexcept HBA_NONBLOCKING override {
        return livecut::engine::Cutter::TransportBounds{0u, 0, std::nullopt};
    }
};

void renderLivecutClips(const fs::path& outDir) {
    using namespace livecut::engine;

    struct LcSpec {
        std::string id, title, tag, key, note;
        BreakPattern pat;
        int bars;
        std::function<void(Cutter&, double barPos, PadZones&)> automate;
    };

    const std::vector<LcSpec> specs = {
        {
            "01-jungle-amen-chop", "Jungle Amen Chopper", "Breakbeat", "F min",
            "CutProc11 live-reordering an Amen break with rising X/Y slice density across 4 bars",
            amenBreak(172.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const auto m = mapPad(PadProgram::cut, lerpF(0.28f, 0.84f, p), lerpF(0.32f, 0.78f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        },
        {
            "02-rush-drop-roll", "Rush Drop Fill", "DnB Fill", "F min",
            "Clean stepper groove snapping into accelerating end-of-bar Rush snare rolls on bars 2 & 4",
            stepperBreak(172.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const bool engage = (p >= 0.25 && p < 0.50) || (p >= 0.68);
                c.setIdle(!engage);
                const auto m = mapPad(PadProgram::rush, lerpF(0.45f, 0.88f, p), lerpF(0.55f, 0.92f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        },
        {
            "03-triplet-bounce", "Triplet Bounce", "Halftime", "F min",
            "Bounce procedure injecting modern triplet rolls and climbing semitone ratchets at 140 BPM",
            halftimeBreak(140.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const auto m = mapPad(PadProgram::bounce, lerpF(0.35f, 0.85f, p), lerpF(0.40f, 0.88f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        },
        {
            "04-tape-brake-dub", "Tape Brake & Catch-Up", "Tape FX", "F min",
            "Grid-locked Tape stops braking a dub breakbeat to a halt and spinning cleanly back onto the downbeat",
            stepperBreak(168.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const auto m = mapPad(PadProgram::tape, lerpF(0.28f, 0.68f, p), lerpF(0.55f, 0.90f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        },
        {
            "05-filter-sing-roll", "Tuned Filter Sing", "Resonant", "F min",
            "Filter & Suction pad automation sweeping resonant low-pass contours and tuned ring-outs",
            amenBreak(172.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const auto prog = (p < 0.5) ? PadProgram::filter : PadProgram::suction;
                const auto m = mapPad(prog, lerpF(0.25f, 0.85f, std::fmod(p * 2.0, 1.0)), lerpF(0.45f, 0.85f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        },
        {
            "06-loop-fold-glitch", "Loop Fold & Glitch", "IDM / Glitch", "F min",
            "Loops folding 1-bar chops down into 1/16th repeats before handing off to crushed Glitch bursts",
            halftimeBreak(156.0), 4,
            [](Cutter& c, double p, PadZones& z) {
                const auto prog = (p < 0.52) ? PadProgram::loops : PadProgram::glitch;
                const auto m = mapPad(prog, lerpF(0.42f, 0.92f, p), lerpF(0.55f, 0.92f, p), z);
                z = m.zones;
                c.setProcedure(m.procedure); c.setUnitsPerBar(m.unitsPerBar); c.setSeed(m.seed);
                c.setParameters(m.cut); c.setCrusher(m.crusher); c.setImpact(m.impact); c.setFracture(m.fracture);
            }
        }
    };

    for (const auto& s : specs) {
        auto stem = renderBreakStem(s.pat, s.bars + 1); // +1 bar pre-roll so cutter buffer is hot
        const int preRollFrames = static_cast<int>(std::llround((4.0 * 60.0 / s.pat.bpm) * kSr));
        const int totalFrames = static_cast<int>(stem.size() / 2) - preRollFrames;

        Cutter cutter;
        cutter.prepare(kSr);
        OnsetDetector onsetDetector;
        onsetDetector.prepare(kSr, 1024u);
        ShowcaseSourceContext sourceCtx;
        cutter.setTransformSource(onsetDetector, sourceCtx);
        PadZones zones{};
        const double beatsPerSample = s.pat.bpm / 60.0 / kSr;
        std::vector<float> left(kBlock), right(kBlock);
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);

        // Pre-roll 1 bar
        for (int pos = 0; pos < preRollFrames; pos += kBlock) {
            const int n = std::min(kBlock, preRollFrames - pos);
            s.automate(cutter, 0.0, zones);
            for (int i = 0; i < n; ++i) {
                left[i] = stem[static_cast<std::size_t>(pos + i) * 2u];
                right[i] = stem[static_cast<std::size_t>(pos + i) * 2u + 1u];
            }
            const double b = static_cast<double>(pos) * beatsPerSample;
            const auto bar = static_cast<std::int64_t>(b / 4.0);
            cutter.process(left.data(), right.data(), n, {s.pat.bpm, 4.0}, {bar, b - 4.0 * bar});
        }

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
            s.automate(cutter, prog, zones);
            for (int i = 0; i < n; ++i) {
                const std::size_t src = static_cast<std::size_t>(preRollFrames + pos + i);
                left[i] = stem[src * 2u];
                right[i] = stem[src * 2u + 1u];
            }
            const double b = static_cast<double>(preRollFrames + pos) * beatsPerSample;
            const auto bar = static_cast<std::int64_t>(b / 4.0);
            cutter.process(left.data(), right.data(), n, {s.pat.bpm, 4.0}, {bar, b - 4.0 * bar});
            for (int i = 0; i < n; ++i) {
                out[static_cast<std::size_t>(pos + i) * 2u] = left[i];
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = right[i];
            }
        }
        emitClip(outDir, "livecut-redux", s.id, s.title, s.tag, s.pat.bpm, s.key, s.note, std::move(out), -12.8);
    }
}


} // namespace hba::showcase
