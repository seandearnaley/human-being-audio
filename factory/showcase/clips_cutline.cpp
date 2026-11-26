// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "cl/engine.h"
#include "cl/preset.h"
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
// 4. CUTLINE (6 clips: REAL Cutline MIDI/MPE/CC driving Tract Form, MPE Synth & Drums!)
// ============================================================================
std::vector<float> renderCutlineIntoTract(const fs::path& clPresetPath, const fs::path& tfPresetPath,
                                          const fs::path& pfPresetPath, double bpm, int bars,
                                          std::function<void(cl::Params&, tf::Params&, double prog)> automate) {
    const auto clParsed = cl::parsePreset(readTextFile(clPresetPath));
    const auto tfParsed = tf::parsePreset(readTextFile(tfPresetPath));
    if (!clParsed || !tfParsed) throw std::runtime_error("Bad CL/TF preset");

    cl::Params clParams = clParsed->params;
    clParams[cl::ParamId::audition] = 0.0f; // ZERO weak built-in audition tone!
    tf::Params tfParams = tfParsed->params;

    cl::Engine clEng;
    clEng.prepare(kSr, kBlock);
    clEng.setLaneCurves(clParsed->curves);
    clEng.setOverrides(clParsed->overrides);

    tf::Engine tfEng;
    tfEng.prepare(kSr, kBlock, 4);
    tfEng.setLaneCurves(tfParsed->curves);
    const int pbIdx = std::clamp(clParams.choice(cl::ParamId::bendRange), 0, 2);
    const float bendRange = static_cast<float>(cl::kBendRangeSemitones[static_cast<std::size_t>(pbIdx)]);
    tfEng.setPitchBendRange(bendRange);

    std::optional<pf::Preset> pfParsed;
    pf::Engine pfEng;
    if (!pfPresetPath.empty()) {
        pfParsed = pf::parsePreset(readTextFile(pfPresetPath));
        pfEng.prepare(kSr, kBlock, 4);
        pfEng.setLaneCurves(pfParsed->curves);
    }

    const int totalFrames = static_cast<int>(std::llround((4.0 * bars * 60.0 / bpm) * kSr));
    const double beatsPerSample = bpm / 60.0 / kSr;
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    std::vector<float> bufL(kBlock), bufR(kBlock);
    cl::MidiEventBuffer midi;

    for (int pos = 0; pos < totalFrames; pos += kBlock) {
        const int n = std::min(kBlock, totalFrames - pos);
        const double beat = static_cast<double>(pos) * beatsPerSample;
        const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
        if (automate) automate(clParams, tfParams, prog);

        midi.clear();
        clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);

        // Route Cutline's sample-accurate MIDI/CC/Bend directly into Tract Form
        for (int e = 0; e < midi.count; ++e) {
            const auto& ev = midi.events[static_cast<std::size_t>(e)];
            const std::uint8_t status = ev.data[0] & 0xF0u;
            if (ev.isNoteOn()) {
                // Keep bass turnarounds inside a musical bass register (no accidental chipmunk octave leaps)
                int note = ev.noteNumber();
                while (note > 53) note -= 12;
                tfEng.noteOn(note, static_cast<float>(ev.data[2]) / 127.0f);
            } else if (ev.isNoteOff()) {
                int note = ev.noteNumber();
                while (note > 53) note -= 12;
                tfEng.noteOff(note);
            } else if (status == 0xE0u && ev.size == 3) {
                const int v14 = static_cast<int>(ev.data[1]) | (static_cast<int>(ev.data[2]) << 7);
                const float norm = (static_cast<float>(v14) - 8192.0f) / 8191.0f;
                tfEng.pitchBend(std::clamp(norm * bendRange, -7.0f, 7.0f));
            } else if (status == 0xB0u && ev.size == 3) {
                const float norm = static_cast<float>(ev.data[2]) / 127.0f;
                if (ev.data[1] == 1) tfEng.setModWheel(norm);
                else if (ev.data[1] == 74) tfEng.setTimbre(norm);
            } else if (status == 0xD0u && ev.size >= 2) {
                tfEng.setAftertouch(static_cast<float>(ev.data[1]) / 127.0f);
            }
        }

        tfEng.process(tfParams, {bpm, beat, true}, bufL.data(), bufR.data(), n);
        if (pfParsed) {
            pf::Params pfP = pfParsed->params;
            pfP[pf::ParamId::mix] = 0.65f;
            pfP[pf::ParamId::subLock] = 1.0f;
            pfP[pf::ParamId::morphX] = static_cast<float>(0.35 + 0.45 * prog);
            pfEng.process(pfP, {bpm, beat, true}, bufL.data(), bufR.data(), n);
        }
        for (int i = 0; i < n; ++i) {
            out[static_cast<std::size_t>(pos + i) * 2u] = bufL[i];
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[i];
        }
    }
    return out;
}

void renderCutlineClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path clDir = repoRoot / "products/cutline/presets";
    const fs::path tfDir = repoRoot / "products/tract-form/presets";
    const fs::path pfDir = repoRoot / "products/pole-field/presets";

    // 1. Neuro Whip -> Tract Form (real BBCut phrase grammar + MPE bends driving Coupled Reese)
    {
        auto audio = renderCutlineIntoTract(clDir / "neuro-whip.json", tfDir / "coupled-reese.json",
                                            pfDir / "neuro-chew.json", 172.0, 4,
            [](cl::Params& cp, tf::Params& tp, double t) {
                cp[cl::ParamId::octave] = 0.0f; // Octave 1 bass weight
                cp[cl::ParamId::gravity] = lerpF(0.28f, 0.65f, t);
                cp[cl::ParamId::ratchet] = lerpF(0.22f, 0.48f, t);
                cp[cl::ParamId::glide] = lerpF(0.45f, 0.72f, t);
                tp[tf::ParamId::tract] = lerpF(0.35f, 0.78f, t);
                tp[tf::ParamId::drive] = 0.44f;
            });
        emitClip(outDir, "cutline", "01-neuro-whip-reese", "Neuro Whip → Tract Form", "Phrase Grammar", 172.0, "F Phrygian",
                 "Cutline’s BBCut Statement/Answer/Ratchet MIDI + pitch bends driving Tract Form into Pole Field",
                 std::move(audio), -13.0);
    }

    // 2. MPE Liquid Chords -> Lush Polyphonic MPE Synth
    {
        const auto clParsed = cl::parsePreset(readTextFile(clDir / "mpe-liquid-chords.json"));
        cl::Params clParams = clParsed->params;
        clParams[cl::ParamId::audition] = 0.0f;
        cl::Engine clEng;
        clEng.prepare(kSr, kBlock);
        clEng.setLaneCurves(clParsed->curves);
        clEng.setOverrides(clParsed->overrides);

        LushMpeSynth mpeSynth;
        mpeSynth.globalCutoffHz = 1600.0;
        mpeSynth.resonance = 1.45;
        mpeSynth.attackSec = 0.04;
        mpeSynth.releaseSec = 0.62;
        const int pbIdx = std::clamp(clParams.choice(cl::ParamId::bendRange), 0, 2);
        const double bendRange = static_cast<double>(cl::kBendRangeSemitones[static_cast<std::size_t>(pbIdx)]);

        const double bpm = 122.0;
        const int totalFrames = static_cast<int>(std::llround((16.0 * 60.0 / bpm) * kSr));
        const double beatsPerSample = bpm / 60.0 / kSr;
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
        cl::MidiEventBuffer midi;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double beat = static_cast<double>(pos) * beatsPerSample;
            const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
            clParams[cl::ParamId::glide] = lerpF(0.28f, 0.58f, prog);
            mpeSynth.globalCutoffHz = 1250.0 + 1350.0 * smoothStep(prog);

            midi.clear();
            clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);
            for (int e = 0; e < midi.count; ++e) {
                const auto& ev = midi.events[static_cast<std::size_t>(e)];
                const std::uint8_t st = ev.data[0] & 0xF0u;
                if (ev.isNoteOn()) mpeSynth.noteOn(ev.channel(), ev.noteNumber(), ev.data[2] / 127.0);
                else if (ev.isNoteOff()) mpeSynth.noteOff(ev.channel(), ev.noteNumber());
                else if (st == 0xE0u && ev.size == 3) {
                    const int v14 = ev.data[1] | (ev.data[2] << 7);
                    const double norm = (static_cast<double>(v14) - 8192.0) / 8191.0;
                    mpeSynth.pitchBend(ev.channel(), std::clamp(norm * bendRange * 0.65, -7.0, 7.0));
                } else if (st == 0xB0u && ev.data[1] == 74) {
                    mpeSynth.slideCc74(ev.channel(), ev.data[2] / 127.0);
                }
            }
            for (int i = 0; i < n; ++i) {
                float l = 0.0f, r = 0.0f;
                mpeSynth.tick(l, r, 0.36f);
                out[static_cast<std::size_t>(pos + i) * 2u] = l;
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = r;
            }
        }
        emitClip(outDir, "cutline", "02-mpe-liquid-chords", "MPE Liquid Chords", "Poly MPE", bpm, "D Dorian",
                 "Per-note MPE Lower Zone pitch slides and CC74 brightness driving a lush polyphonic synth",
                 std::move(out), -13.6);
    }

    // 3. BBCut Acid Grammar -> Tract Form Acid Snap
    {
        auto audio = renderCutlineIntoTract(clDir / "bbcut-acid.json", tfDir / "acid-snap.json", {}, 140.0, 4,
            [](cl::Params& cp, tf::Params& tp, double t) {
                cp[cl::ParamId::density] = lerpF(0.45f, 0.72f, t);
                cp[cl::ParamId::ratchet] = lerpF(0.18f, 0.44f, t);
                tp[tf::ParamId::tractShift] = lerpF(-3.0f, 5.5f, t);
                tp[tf::ParamId::drive] = lerpF(0.28f, 0.56f, t);
            });
        emitClip(outDir, "cutline", "03-bbcut-acid-grammar", "BBCut Acid Grammar", "Acid MIDI", 140.0, "A Minor",
                 "Gravitational microtiming and climbing ratchets articulating an expressive acid bassline",
                 std::move(audio), -13.0);
    }

    // 4. Halftime Sub Weights -> Tract Form 808 Hammer
    {
        auto audio = renderCutlineIntoTract(clDir / "halftime-sub-weights.json", tfDir / "sub-808-hammer.json", {}, 138.0, 4,
            [](cl::Params& cp, tf::Params& tp, double t) {
                cp[cl::ParamId::octave] = 0.0f;
                cp[cl::ParamId::gravity] = lerpF(0.35f, 0.72f, t);
                cp[cl::ParamId::ratchet] = lerpF(0.15f, 0.35f, t);
                tp[tf::ParamId::subWarmth] = lerpF(0.22f, 0.58f, t);
            });
        emitClip(outDir, "cutline", "04-halftime-sub-weights", "Halftime Sub Weights", "Halftime", 138.0, "F Minor",
                 "Heavy halftime phrase articulation with swung slides and bar-4 turnaround drops",
                 std::move(audio), -12.6);
    }

    // 5. Amen Ghosts -> Studio Drum Kit (Cutline Drum MIDI output triggering velocity-sensitive drums!)
    {
        const auto clParsed = cl::parsePreset(readTextFile(clDir / "amen-ghosts.json"));
        cl::Params clParams = clParsed->params;
        clParams[cl::ParamId::audition] = 0.0f;
        cl::Engine clEng;
        clEng.prepare(kSr, kBlock);
        clEng.setLaneCurves(clParsed->curves);
        clEng.setOverrides(clParsed->overrides);

        StudioDrumKit kit;
        const double bpm = 172.0;
        const int totalFrames = static_cast<int>(std::llround((16.0 * 60.0 / bpm) * kSr));
        const double beatsPerSample = bpm / 60.0 / kSr;
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
        cl::MidiEventBuffer midi;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double beat = static_cast<double>(pos) * beatsPerSample;
            const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
            clParams[cl::ParamId::ratchet] = lerpF(0.25f, 0.78f, prog);
            clParams[cl::ParamId::velArc] = lerpF(0.45f, 0.85f, prog);

            midi.clear();
            clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);

            // Sample-accurate triggering inside the 64-sample block
            int evCursor = 0;
            for (int i = 0; i < n; ++i) {
                while (evCursor < midi.count && midi.events[evCursor].sampleOffset <= i) {
                    const auto& ev = midi.events[evCursor++];
                    if (ev.isNoteOn()) kit.trigger(ev.noteNumber(), ev.data[2] / 127.0);
                }
                float l = 0.0f, r = 0.0f;
                kit.tick(l, r, 1.0);
                out[static_cast<std::size_t>(pos + i) * 2u] = l;
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = r;
            }
        }
        emitClip(outDir, "cutline", "05-amen-ghosts-kit", "Amen Ghosts → Drum Kit", "Drum MIDI", bpm, "Drum Kit",
                 "Cutline’s Drum Kit articulation generating human ghost snares, swung pockets, and turnaround fills",
                 std::move(out), -12.8);
    }

    // 6. Evolving Dark Reese -> Tract Form + Pole Field (Evolve mode mutating across 4 bars)
    {
        auto audio = renderCutlineIntoTract(clDir / "evolving-dark-reese.json", tfDir / "vowel-slap.json",
                                            pfDir / "liquid-jaw.json", 172.0, 4,
            [](cl::Params& cp, tf::Params& tp, double t) {
                cp[cl::ParamId::octave] = 0.0f;
                cp[cl::ParamId::mutate] = lerpF(0.22f, 0.65f, t);
                cp[cl::ParamId::ratchet] = lerpF(0.18f, 0.42f, t);
                cp[cl::ParamId::glide] = lerpF(0.25f, 0.55f, t);
                tp[tf::ParamId::morph] = lerpF(0.20f, 0.88f, t);
            });
        emitClip(outDir, "cutline", "06-evolving-dark-reese", "Evolving Phrygian Jaw", "Evolve Mode", 172.0, "F Phrygian",
                 "Deterministic phrase mutation evolving across 4 bars into Tract Form and Pole Field",
                 std::move(audio), -13.0);
    }
}


} // namespace hba::showcase
