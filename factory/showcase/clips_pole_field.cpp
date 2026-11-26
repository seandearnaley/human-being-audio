// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "pf/engine.h"
#include "pf/preset.h"
#include "tf/engine.h"
#include "tf/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 3. POLE FIELD (6 clips: real basslines, film chords, drum breaks, LPC glass)
// ============================================================================
std::vector<float> processWithPoleField(const fs::path& presetPath, double bpm, std::vector<float> audio,
                                        std::function<void(pf::Params&, double prog)> automate) {
    const auto parsed = pf::parsePreset(readTextFile(presetPath));
    if (!parsed) throw std::runtime_error("Bad PF preset: " + presetPath.string());
    pf::Params params = parsed->params;

    pf::Engine engine;
    engine.prepare(kSr, kBlock, 4);
    engine.setLaneCurves(parsed->curves);
    if (parsed->imprint.hasAny()) engine.setImprint(parsed->imprint);

    const int totalFrames = static_cast<int>(audio.size() / 2);
    const double beatsPerSample = bpm / 60.0 / kSr;
    std::vector<float> bufL(kBlock), bufR(kBlock);

    for (int pos = 0; pos < totalFrames; pos += kBlock) {
        const int n = std::min(kBlock, totalFrames - pos);
        const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
        if (automate) automate(params, prog);
        for (int i = 0; i < n; ++i) {
            bufL[i] = audio[static_cast<std::size_t>(pos + i) * 2u];
            bufR[i] = audio[static_cast<std::size_t>(pos + i) * 2u + 1u];
        }
        engine.process(params, {bpm, static_cast<double>(pos) * beatsPerSample, true}, bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i) {
            audio[static_cast<std::size_t>(pos + i) * 2u] = bufL[i];
            audio[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[i];
        }
    }
    return audio;
}

std::vector<float> renderChordStem(double bpm, int bars, const std::vector<std::array<int, 4>>& chords,
                                   double cutoffHz = 2800.0, double roomWet = 0.25) {
    const int totalFrames = static_cast<int>(std::llround((4.0 * bars * 60.0 / bpm) * kSr));
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    LushMpeSynth synth;
    synth.globalCutoffHz = cutoffHz;
    synth.resonance = 1.2;
    synth.attackSec = 0.06;
    synth.releaseSec = 0.55;

    const double beatsPerSample = bpm / 60.0 / kSr;
    int lastBar = -1;
    for (int i = 0; i < totalFrames; ++i) {
        const int bar = static_cast<int>((static_cast<double>(i) * beatsPerSample) / 4.0);
        if (bar != lastBar && bar < static_cast<int>(chords.size())) {
            if (lastBar >= 0) {
                for (int n : chords[static_cast<std::size_t>(lastBar)]) synth.noteOff(1, n);
            }
            for (int n : chords[static_cast<std::size_t>(bar)]) synth.noteOn(1, n, 0.85);
            lastBar = bar;
        }
        float l = 0.0f, r = 0.0f;
        synth.tick(l, r, static_cast<float>(roomWet));
        out[static_cast<std::size_t>(i) * 2u] = l;
        out[static_cast<std::size_t>(i) * 2u + 1u] = r;
    }
    return out;
}

void renderPoleFieldClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path pfDir = repoRoot / "products/pole-field/presets";
    const fs::path tfDir = repoRoot / "products/tract-form/presets";

    // 1. Neuro Chew Bassline (Tract Form raw reese riff -> Pole Field Neuro Chew with figure-8 XY morph)
    {
        const std::vector<NoteEvent> riff = {
            {0.00, 1.75, 29, 0.96f, 0.0f},  {2.00, 0.90, 32, 0.92f, -2.0f},
            {3.00, 0.90, 30, 0.94f, -1.0f}, {4.00, 1.75, 29, 0.98f, 3.0f},
            {6.00, 0.90, 36, 0.92f, -4.0f}, {7.00, 0.90, 30, 0.95f, -1.0f},
            {8.00, 1.75, 29, 0.98f, 0.0f},  {10.00, 0.90, 34, 0.92f, 0.0f},
            {11.00, 0.90, 32, 0.92f, -2.0f},{12.00, 3.60, 29, 1.00f, 0.0f},
        };
        auto rawBass = renderTractSequence(tfDir / "coupled-reese.json", 172.0, 4, riff,
            [](tf::Params& p, tf::Engine&, double) {
                p[tf::ParamId::tract] = 0.0f;
                p[tf::ParamId::drive] = 0.36f;
                p[tf::ParamId::coupling] = 0.72f;
            });
        auto wet = processWithPoleField(pfDir / "neuro-chew.json", 172.0, std::move(rawBass),
            [](pf::Params& p, double t) {
                p[pf::ParamId::morphX] = static_cast<float>(0.5 + 0.46 * std::sin(kTwoPi * t * 1.5));
                p[pf::ParamId::morphY] = static_cast<float>(0.5 + 0.44 * std::cos(kTwoPi * t * 2.0));
                p[pf::ParamId::morphZ] = lerpF(0.18f, 0.78f, t);
                p[pf::ParamId::res] = lerpF(0.55f, 0.76f, t);
                p[pf::ParamId::drive] = lerpF(0.35f, 0.68f, t);
                p[pf::ParamId::subLock] = 1.0f;
                p[pf::ParamId::mix] = 1.0f;
            });
        emitClip(outDir, "pole-field", "01-neuro-chew-bassline", "Neuro Chew Bassline", "14P Z-Plane", 172.0, "F Phrygian",
                 "14-pole E-MU Morpheus talking cube chewing a live bassline with XY morph automation and mono Sub Lock",
                 std::move(wet), -13.0);
    }

    // 2. Morphing Vocal Cinema (Dark film chord progression -> Parallel Choir Vocal Matrix)
    {
        // F minor 9 -> Db maj7 -> Bb min9 -> C7alt film-score progression
        const std::vector<std::array<int, 4>> chords = {
            {41, 56, 60, 67},
            {37, 56, 60, 65},
            {46, 56, 60, 65},
            {48, 55, 61, 67},
        };
        auto stem = renderChordStem(108.0, 4, chords, 4200.0, 0.28);
        auto wet = processWithPoleField(pfDir / "parallel-choir.json", 108.0, std::move(stem),
            [](pf::Params& p, double t) {
                p[pf::ParamId::morphX] = lerpF(0.05f, 0.95f, smoothStep(t));
                p[pf::ParamId::morphY] = static_cast<float>(0.5 - 0.45 * std::cos(kTwoPi * t));
                p[pf::ParamId::spread] = lerpF(0.30f, 0.78f, t);
                p[pf::ParamId::res] = lerpF(0.45f, 0.68f, std::sin(kTwoPi * 0.5 * t));
                p[pf::ParamId::mix] = 0.92f;
            });
        emitClip(outDir, "pole-field", "02-vocal-choir-cinema", "Morphing Vocal Cinema", "Film Score", 108.0, "F Minor",
                 "Dark 4-chord film progression morphed through Parallel Choir (Ah → Ee → Oo → Whisper) with binaural spread",
                 std::move(wet), -13.8);
    }

    // 3. Z-Plane Break Mangler (Punchy Amen break -> Amen Mangler with sweeping formants)
    {
        auto stem = renderBreakStem(amenBreak(172.0), 4);
        auto wet = processWithPoleField(pfDir / "amen-mangler.json", 172.0, std::move(stem),
            [](pf::Params& p, double t) {
                p[pf::ParamId::morphX] = static_cast<float>(0.5 + 0.45 * std::sin(kTwoPi * t * 2.0));
                p[pf::ParamId::morphZ] = lerpF(0.15f, 0.88f, t);
                p[pf::ParamId::freq] = lerpF(-6.0f, 9.0f, smoothStep(t));
                p[pf::ParamId::mix] = lerpF(0.68f, 0.94f, t);
                p[pf::ParamId::subLock] = 1.0f;
            });
        emitClip(outDir, "pole-field", "03-amen-mangler-break", "Z-Plane Break Mangler", "Breakbeat FX", 172.0, "F min",
                 "Amen drum break morphed into metallic liquid formants while Sub Lock keeps kick weight intact",
                 std::move(wet), -12.8);
    }

    // 4. Phase Razor Roller (Syncopated rolling saw riff -> Razor Comb deep alternating notches)
    {
        const std::vector<NoteEvent> roller = {
            {0.00, 0.70, 33, 0.94f, 0.0f},  {0.75, 0.70, 33, 0.88f, 0.0f},
            {1.50, 0.90, 36, 0.95f, 0.0f},  {2.50, 0.70, 35, 0.92f, 0.0f},
            {3.25, 0.65, 33, 0.88f, 0.0f},  {4.00, 0.70, 33, 0.96f, 0.0f},
            {4.75, 0.70, 40, 0.94f, -2.0f}, {5.50, 0.90, 38, 0.92f, 0.0f},
            {6.50, 1.35, 36, 0.94f, -3.0f}, {8.00, 0.70, 33, 0.96f, 0.0f},
            {8.75, 0.70, 33, 0.90f, 0.0f},  {9.50, 0.90, 41, 0.96f, -1.0f},
            {10.50, 0.70, 40, 0.92f, 0.0f}, {11.25, 0.65, 36, 0.88f, 0.0f},
            {12.00, 1.40, 35, 0.95f, 0.0f}, {13.50, 2.30, 33, 1.00f, 0.0f},
        };
        auto raw = renderTractSequence(tfDir / "coupled-reese.json", 170.0, 4, roller,
            [](tf::Params& p, tf::Engine&, double) {
                p[tf::ParamId::tract] = 0.0f;
                p[tf::ParamId::drive] = 0.22f;
            });
        auto wet = processWithPoleField(pfDir / "razor-comb.json", 170.0, std::move(raw),
            [](pf::Params& p, double t) {
                p[pf::ParamId::morphX] = lerpF(0.10f, 0.92f, t);
                p[pf::ParamId::morphY] = static_cast<float>(0.5 + 0.42 * std::sin(kTwoPi * t * 3.0));
                p[pf::ParamId::spread] = lerpF(0.35f, 0.85f, t);
                p[pf::ParamId::mix] = 0.92f;
                p[pf::ParamId::subLock] = 1.0f;
            });
        emitClip(outDir, "pole-field", "04-razor-comb-roller", "Phase Razor Roller", "Comb Sweep", 170.0, "A Minor",
                 "Rolling techstep bass carved by 14-pole allpass-adjacent comb notches and wide binaural spread",
                 std::move(wet), -13.0);
    }

    // 5. Envelope Acid Wah (Funky staccato 16th bass triggering Pole Field's internal envelope follower)
    {
        const std::vector<NoteEvent> funk = {
            {0.00, 0.40, 36, 0.98f, 0.0f},  {0.75, 0.35, 48, 0.92f, 0.0f},
            {1.50, 0.45, 39, 0.94f, 0.0f},  {2.25, 0.40, 43, 0.96f, 0.0f},
            {3.00, 0.40, 46, 0.95f, -3.0f}, {3.50, 0.35, 39, 0.88f, 0.0f},
            {4.00, 0.40, 36, 1.00f, 0.0f},  {4.75, 0.35, 51, 0.95f, 0.0f},
            {5.50, 0.45, 48, 0.92f, 0.0f},  {6.25, 0.40, 43, 0.94f, 0.0f},
            {7.00, 0.75, 41, 0.96f, -2.0f}, {8.00, 0.40, 36, 0.98f, 0.0f},
            {8.75, 0.35, 48, 0.92f, 0.0f},  {9.50, 0.45, 46, 0.96f, 0.0f},
            {10.25, 0.40, 43, 0.94f, 0.0f}, {11.00, 0.75, 39, 0.95f, 0.0f},
            {12.00, 0.40, 36, 1.00f, 0.0f}, {12.75, 0.35, 43, 0.94f, 0.0f},
            {13.50, 0.45, 41, 0.92f, 0.0f}, {14.25, 0.45, 39, 0.90f, 0.0f},
            {15.00, 0.80, 36, 1.00f, 0.0f},
        };
        auto raw = renderTractSequence(tfDir / "acid-snap.json", 132.0, 4, funk,
            [](tf::Params& p, tf::Engine&, double) { p[tf::ParamId::drive] = 0.25f; });
        auto wet = processWithPoleField(pfDir / "env-chew-wah.json", 132.0, std::move(raw),
            [](pf::Params& p, double t) {
                p[pf::ParamId::res] = lerpF(0.45f, 0.74f, t);
                p[pf::ParamId::drive] = lerpF(0.25f, 0.56f, t);
                p[pf::ParamId::mix] = 0.95f;
            });
        emitClip(outDir, "pole-field", "05-env-acid-wah", "Envelope Acid Wah", "Env Follower", 132.0, "C Minor",
                 "Internal envelope follower snapping Z-plane formants open on every staccato acid note",
                 std::move(wet), -13.2);
    }

    // 6. Burg LPC Glass Chamber (Arpeggio + warm chord stamped through Imprint Glass Harp)
    {
        const std::vector<std::array<int, 4>> chords = {
            {48, 55, 62, 67},
            {44, 55, 60, 67},
            {46, 53, 60, 65},
            {48, 55, 60, 63},
        };
        auto stem = renderChordStem(116.0, 4, chords, 5200.0, 0.22);
        auto wet = processWithPoleField(pfDir / "imprint-glass-harp.json", 116.0, std::move(stem),
            [](pf::Params& p, double t) {
                p[pf::ParamId::morphX] = lerpF(0.05f, 0.88f, smoothStep(t));
                p[pf::ParamId::morphY] = lerpF(0.20f, 0.85f, std::sin(kTwoPi * 0.5 * t));
                p[pf::ParamId::transform] = lerpF(-0.35f, 0.45f, t);
                p[pf::ParamId::mix] = 0.90f;
            });
        emitClip(outDir, "pole-field", "06-burg-glass-chamber", "Burg LPC Glass Chamber", "Audio Imprint", 116.0, "C Minor",
                 "14th-order Burg LPC imprinted glass-harp resonances transforming dark synths into crystalline acoustics",
                 std::move(wet), -14.0);
    }
}


} // namespace hba::showcase
