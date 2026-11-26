// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "cl/engine.h"
#include "cl/preset.h"
#include "pf/engine.h"
#include "pf/preset.h"
#include "sb/engine.h"
#include "sb/preset.h"
#include "tf/engine.h"
#include "tf/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 4. CUTLINE 2.0 (6 clips: Breakbeats, Live X/Y Articulation, Strike Body, Polyrhythm & MIDI Slicer!)
// ============================================================================
namespace {

std::vector<float> renderCutlineDrums(const fs::path& clPresetPath, double bpm, int bars,
                                      std::function<void(cl::Params&, double prog)> automate) {
    const auto clParsed = cl::parsePreset(readTextFile(clPresetPath));
    if (!clParsed)
        throw std::runtime_error("Bad CL preset: " + clPresetPath.string());

    cl::Params clParams = clParsed->params;
    clParams[cl::ParamId::audition] = 0.0f;

    cl::Engine clEng;
    clEng.prepare(kSr, kBlock);
    clEng.setLaneCurves(clParsed->curves);
    clEng.setPatternBank(clParsed->bank);

    StudioDrumKit kit;
    const int totalFrames = static_cast<int>(std::llround((4.0 * bars * 60.0 / bpm) * kSr));
    const double beatsPerSample = bpm / 60.0 / kSr;
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    cl::MidiEventBuffer midi;

    for (int pos = 0; pos < totalFrames; pos += kBlock) {
        const int n = std::min(kBlock, totalFrames - pos);
        const double beat = static_cast<double>(pos) * beatsPerSample;
        const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
        if (automate)
            automate(clParams, prog);

        midi.clear();
        clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);

        int evCursor = 0;
        for (int i = 0; i < n; ++i) {
            while (evCursor < midi.count && midi.events[static_cast<std::size_t>(evCursor)].sampleOffset <= i) {
                const auto& ev = midi.events[static_cast<std::size_t>(evCursor++)];
                if (ev.isNoteOn())
                    kit.trigger(ev.noteNumber(), static_cast<double>(ev.data[2]) / 127.0);
            }
            float l = 0.0f, r = 0.0f;
            kit.tick(l, r, 1.0);
            out[static_cast<std::size_t>(pos + i) * 2u] = l;
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = r;
        }
    }
    return out;
}

std::vector<float> renderCutlineSlicerIntoTract(const fs::path& clPresetPath, const fs::path& tfPresetPath,
                                                const fs::path& pfPresetPath, const std::vector<int>& chordNotes,
                                                double bpm, int bars,
                                                std::function<void(cl::Params&, tf::Params&, double prog)> automate) {
    const auto clParsed = cl::parsePreset(readTextFile(clPresetPath));
    const auto tfParsed = tf::parsePreset(readTextFile(tfPresetPath));
    if (!clParsed || !tfParsed)
        throw std::runtime_error("Bad CL/TF preset");

    cl::Params clParams = clParsed->params;
    clParams[cl::ParamId::audition] = 0.0f;
    clParams[cl::ParamId::slicerLatch] = 1.0f;
    tf::Params tfParams = tfParsed->params;

    cl::Engine clEng;
    clEng.prepare(kSr, kBlock);
    clEng.setLaneCurves(clParsed->curves);
    clEng.setPatternBank(clParsed->bank);
    for (int note : chordNotes)
        clEng.inputNoteOn(note, 108, 0.0);

    tf::Engine tfEng;
    tfEng.prepare(kSr, kBlock, 4);
    tfEng.setLaneCurves(tfParsed->curves);

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
        if (automate)
            automate(clParams, tfParams, prog);

        midi.clear();
        clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);

        for (int e = 0; e < midi.count; ++e) {
            const auto& ev = midi.events[static_cast<std::size_t>(e)];
            const std::uint8_t status = ev.data[0] & 0xF0u;
            if (ev.isNoteOn()) {
                int note = ev.noteNumber();
                while (note > 53)
                    note -= 12;
                tfEng.noteOn(note, static_cast<float>(ev.data[2]) / 127.0f);
            } else if (ev.isNoteOff()) {
                int note = ev.noteNumber();
                while (note > 53)
                    note -= 12;
                tfEng.noteOff(note);
            } else if (status == 0xB0u && ev.size == 3) {
                const float norm = static_cast<float>(ev.data[2]) / 127.0f;
                if (ev.data[1] == 1)
                    tfEng.setModWheel(norm);
                else if (ev.data[1] == 74)
                    tfEng.setTimbre(norm);
            } else if (status == 0xD0u && ev.size >= 2) {
                tfEng.setAftertouch(static_cast<float>(ev.data[1]) / 127.0f);
            }
        }

        tfEng.process(tfParams, {bpm, beat, true}, bufL.data(), bufR.data(), n);
        if (pfParsed) {
            pf::Params pfP = pfParsed->params;
            pfP[pf::ParamId::mix] = 0.60f;
            pfP[pf::ParamId::subLock] = 1.0f;
            pfP[pf::ParamId::morphX] = static_cast<float>(0.32 + 0.48 * prog);
            pfEng.process(pfP, {bpm, beat, true}, bufL.data(), bufR.data(), n);
        }
        for (int i = 0; i < n; ++i) {
            out[static_cast<std::size_t>(pos + i) * 2u] = bufL[static_cast<std::size_t>(i)];
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[static_cast<std::size_t>(i)];
        }
    }
    return out;
}

} // namespace

void renderCutlineClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path clDir = repoRoot / "products/cutline/presets";
    const fs::path tfDir = repoRoot / "products/tract-form/presets";
    const fs::path pfDir = repoRoot / "products/pole-field/presets";
    const fs::path sbDir = repoRoot / "products/strike-body/presets";

    // 1. Amen Chop 172 -> Studio Drum Kit with Live X/Y Poly + Slice Performance Sweeps
    {
        auto audio = renderCutlineDrums(clDir / "amen-chop-172.json", 172.0, 4, [](cl::Params& p, double t) {
            p[cl::ParamId::sceneQuantize] = static_cast<float>(cl::Quantize::sixteenth);
            if (t >= 0.45 && t < 0.78) {
                p[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::poly);
                p[cl::ParamId::padTouch] = 1.0f;
                p[cl::ParamId::padX] = lerpF(0.35f, 0.85f, (t - 0.45) / 0.33);
                p[cl::ParamId::padY] = lerpF(0.40f, 0.88f, (t - 0.45) / 0.33);
            } else if (t >= 0.78) {
                p[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::slice);
                p[cl::ParamId::padTouch] = 1.0f;
                p[cl::ParamId::padX] = lerpF(0.55f, 0.92f, (t - 0.78) / 0.22);
                p[cl::ParamId::padY] = 0.78f;
            } else {
                p[cl::ParamId::padTouch] = 0.0f;
            }
        });
        emitClip(outDir, "cutline", "01-amen-xy-poly-chop", "Amen Chop 172 · X/Y Poly", "Breakbeat", 172.0, "Drum Kit",
                 "Cutline 2.0 Amen breakbeat sweeping live into the Poly and Slice X/Y performance stages",
                 std::move(audio), -12.4);
    }

    // 2. Strike Body Neuro Snare -> Native Strike Body Kit Map + Live Roll Escalation
    {
        const auto clParsed = cl::parsePreset(readTextFile(clDir / "strike-body-neuro-snare.json"));
        const auto sbParsed = sb::parsePresetJson(readTextFile(sbDir / "01 Neuro Crack 185Hz.json"));
        if (!clParsed || !sbParsed)
            throw std::runtime_error("Bad Cutline/Strike Body preset");

        cl::Params clP = clParsed->params;
        clP[cl::ParamId::audition] = 0.0f;
        cl::Engine clEng;
        clEng.prepare(kSr, kBlock);
        clEng.setLaneCurves(clParsed->curves);
        clEng.setPatternBank(clParsed->bank);

        sb::Params sbP = sbParsed->params;
        sb::Engine sbEng;
        sbEng.prepare(kSr, kBlock, 4);

        const double bpm = 172.0;
        const int totalFrames = static_cast<int>(std::llround((16.0 * 60.0 / bpm) * kSr));
        const double beatsPerSample = bpm / 60.0 / kSr;
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
        std::vector<float> bufL(kBlock), bufR(kBlock);
        cl::MidiEventBuffer midi;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double beat = static_cast<double>(pos) * beatsPerSample;
            const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
            if (prog > 0.72) {
                clP[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::roll);
                clP[cl::ParamId::padTouch] = 1.0f;
                clP[cl::ParamId::padX] = lerpF(0.30f, 0.85f, (prog - 0.72) / 0.28);
                clP[cl::ParamId::padY] = lerpF(0.45f, 0.96f, (prog - 0.72) / 0.28);
            } else {
                clP[cl::ParamId::padTouch] = 0.0f;
            }
            midi.clear();
            clEng.process(clP, {bpm, beat, true}, midi, nullptr, nullptr, n);
            for (int e = 0; e < midi.count; ++e) {
                const auto& ev = midi.events[static_cast<std::size_t>(e)];
                if (ev.isNoteOn())
                    sbEng.noteOn(ev.noteNumber(), static_cast<float>(ev.data[2]) / 127.0f, sbP);
            }
            sbEng.process(sbP, bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i) {
                out[static_cast<std::size_t>(pos + i) * 2u] = bufL[static_cast<std::size_t>(i)];
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[static_cast<std::size_t>(i)];
            }
        }
        emitClip(outDir, "cutline", "02-strike-body-neuro-snare", "Strike Body Articulation", "Strike Body", bpm,
                 "Kit Map", "Cutline's native Strike Body kit map (Main/Ghost/Rim/Roll) triggering Strike Body live",
                 std::move(out), -12.2);
    }

    // 3. Afro 7/11 Polyrhythm + Bounce Gravity X/Y
    {
        auto audio = renderCutlineDrums(clDir / "afro-7-11-polyrhythm.json", 126.0, 4, [](cl::Params& p, double t) {
            if (t >= 0.62 && t < 0.88) {
                p[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::bounce);
                p[cl::ParamId::padTouch] = 1.0f;
                p[cl::ParamId::padX] = lerpF(0.25f, 0.82f, (t - 0.62) / 0.26);
                p[cl::ParamId::padY] = 0.72f;
            } else {
                p[cl::ParamId::padTouch] = 0.0f;
            }
        });
        emitClip(outDir, "cutline", "03-afro-7-11-polyrhythm", "Afro 7/11 Polyrhythm", "Polyrhythm", 126.0, "Drum Kit",
                 "Per-lane 7- and 11-step polymetric percussion with a live gravity Bounce X/Y turnaround",
                 std::move(audio), -12.6);
    }

    // 4. UKG Chord Stabs -> Pitch-Transparent MIDI Slicer driving Lush Poly Synth
    {
        const auto clParsed = cl::parsePreset(readTextFile(clDir / "slicer-ukg-chord-stabs.json"));
        if (!clParsed)
            throw std::runtime_error("Bad slicer-ukg-chord-stabs preset");
        cl::Params clParams = clParsed->params;
        clParams[cl::ParamId::audition] = 0.0f;
        clParams[cl::ParamId::slicerLatch] = 1.0f;

        cl::Engine clEng;
        clEng.prepare(kSr, kBlock);
        clEng.setLaneCurves(clParsed->curves);
        clEng.setPatternBank(clParsed->bank);
        for (int n : {48, 55, 60, 63, 67, 70}) // Cmin9 user-played chord!
            clEng.inputNoteOn(n, 102, 0.0);

        LushMpeSynth synth;
        synth.globalCutoffHz = 1400.0;
        synth.resonance = 1.4;
        synth.attackSec = 0.008;
        synth.releaseSec = 0.34;

        const double bpm = 132.0;
        const int totalFrames = static_cast<int>(std::llround((16.0 * 60.0 / bpm) * kSr));
        const double beatsPerSample = bpm / 60.0 / kSr;
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
        cl::MidiEventBuffer midi;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double beat = static_cast<double>(pos) * beatsPerSample;
            const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);
            clParams[cl::ParamId::gravity] = lerpF(0.32f, 0.62f, prog);
            synth.globalCutoffHz = 1100.0 + 1700.0 * smoothStep(prog);

            midi.clear();
            clEng.process(clParams, {bpm, beat, true}, midi, nullptr, nullptr, n);
            for (int e = 0; e < midi.count; ++e) {
                const auto& ev = midi.events[static_cast<std::size_t>(e)];
                const std::uint8_t st = ev.data[0] & 0xF0u;
                if (ev.isNoteOn())
                    synth.noteOn(ev.channel(), ev.noteNumber(), static_cast<double>(ev.data[2]) / 127.0);
                else if (ev.isNoteOff())
                    synth.noteOff(ev.channel(), ev.noteNumber());
                else if (st == 0xB0u && ev.data[1] == 74)
                    synth.slideCc74(ev.channel(), static_cast<double>(ev.data[2]) / 127.0);
            }
            for (int i = 0; i < n; ++i) {
                float l = 0.0f, r = 0.0f;
                synth.tick(l, r, 0.28f);
                out[static_cast<std::size_t>(pos + i) * 2u] = l;
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = r;
            }
        }
        emitClip(outDir, "cutline", "04-ukg-chord-slicer", "UKG Stab Slicer", "MIDI Slicer", bpm, "C min9",
                 "100% pitch-transparent rhythmic slicing of a held Cmin9 chord with MPC swing and CC74 filter motion",
                 std::move(out), -13.2);
    }

    // 5. Halftime Colossus (Half-Time 0.5x -> Normal 1x + Drop Build)
    {
        auto audio = renderCutlineDrums(clDir / "halftime-85-colossus.json", 170.0, 4, [](cl::Params& p, double t) {
            p[cl::ParamId::seqRate] =
                static_cast<float>(t < 0.50 ? cl::SeqRate::half : cl::SeqRate::normal);
            if (t >= 0.78) {
                p[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::drop);
                p[cl::ParamId::padTouch] = 1.0f;
                p[cl::ParamId::padX] = 0.78f;
                p[cl::ParamId::padY] = 0.85f;
            } else {
                p[cl::ParamId::padTouch] = 0.0f;
            }
        });
        emitClip(outDir, "cutline", "05-halftime-colossus-drop", "Halftime 0.5× → Drop", "Halftime", 170.0, "Drum Kit",
                 "Global 0.5× Half-Time opening slamming into 1× with a live Drill Drop X/Y buildup",
                 std::move(audio), -12.4);
    }

    // 6. Neuro Bass/Stab Slicer -> Tract Form + Pole Field
    {
        auto audio = renderCutlineSlicerIntoTract(
            clDir / "slicer-neuro-bass-stab.json", tfDir / "coupled-reese.json", pfDir / "neuro-chew.json",
            {41, 48, 53, 56}, 172.0, 4, [](cl::Params& cp, tf::Params& tp, double t) {
                cp[cl::ParamId::padProgram] = static_cast<float>(cl::PadProgram::stutter);
                cp[cl::ParamId::padTouch] = (t > 0.68 && t < 0.86) ? 1.0f : 0.0f;
                cp[cl::ParamId::padX] = 0.58f;
                cp[cl::ParamId::padY] = 0.76f;
                tp[tf::ParamId::tract] = lerpF(0.32f, 0.80f, t);
                tp[tf::ParamId::drive] = 0.42f;
            });
        emitClip(outDir, "cutline", "06-neuro-bass-slicer", "Neuro Bass Slicer → Tract Form", "MIDI Slicer", 172.0,
                 "F Minor", "Live Bass/Stab MIDI Slicer with Stutter X/Y chop and CC74 expression driving Tract Form",
                 std::move(audio), -13.0);
    }
}

} // namespace hba::showcase
