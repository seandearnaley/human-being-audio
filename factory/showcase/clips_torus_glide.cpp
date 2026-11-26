// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "tg/engine.h"
#include "tg/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 5. TORUS GLIDE (6 clips: Sidewinder riser, film-score chords, poly-bends, Möbius)
// ============================================================================
std::vector<float> renderTorusClip(const fs::path& presetPath, double bpm, int bars,
                                   const std::vector<NoteEvent>& notes,
                                   std::function<void(tg::Params&, double prog)> automate) {
    const auto parsed = tg::parsePresetJson(readTextFile(presetPath));
    if (!parsed) throw std::runtime_error("Bad TG preset: " + presetPath.string());
    tg::Params params = parsed->params;

    tg::Engine engine;
    engine.prepare(kSr, kBlock, 4);
    engine.setLaneCurves(parsed->curves);
    engine.setSceneMatrix(parsed->matrix);

    const double totalBeats = 4.0 * bars;
    const int totalFrames = static_cast<int>(std::llround((totalBeats * 60.0 / bpm) * kSr));
    const double beatsPerSample = bpm / 60.0 / kSr;
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    std::vector<float> bufL(kBlock), bufR(kBlock);

    int activeIdx = -1;
    for (int pos = 0; pos < totalFrames; pos += kBlock) {
        const int n = std::min(kBlock, totalFrames - pos);
        const double beat = static_cast<double>(pos) * beatsPerSample;
        const double prog = static_cast<double>(pos) / static_cast<double>(totalFrames);

        int found = -1;
        for (int idx = 0; idx < static_cast<int>(notes.size()); ++idx) {
            if (beat >= notes[idx].startBeat && beat < notes[idx].startBeat + notes[idx].durBeats) {
                found = idx;
                break;
            }
        }
        if (found != activeIdx) {
            if (activeIdx >= 0 && found < 0) engine.noteOff(notes[activeIdx].note);
            if (found >= 0) engine.noteOn(notes[found].note, notes[found].vel, false);
            activeIdx = found;
        }
        if (automate) automate(params, prog);

        engine.process(params, {bpm, beat, true}, bufL.data(), bufR.data(), n, nullptr);
        for (int i = 0; i < n; ++i) {
            out[static_cast<std::size_t>(pos + i) * 2u] = bufL[i];
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[i];
        }
    }
    return out;
}

void renderTorusGlideClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path tgDir = repoRoot / "products/torus-glide/presets";

    // 1. Sidewinder Pumping Bass Riser (Rhythmic F Phrygian Shepard bass climbing over Sub-Anchor!)
    {
        const std::vector<NoteEvent> pump = {
            {0.00, 1.65, 41, 0.96f, 0.0f},  {2.00, 0.80, 41, 0.88f, 0.0f},
            {3.00, 0.80, 44, 0.92f, 0.0f},  {4.00, 1.65, 41, 0.98f, 0.0f},
            {6.00, 0.80, 42, 0.92f, 0.0f},  {7.00, 0.80, 41, 0.88f, 0.0f},
            {8.00, 0.80, 41, 0.96f, 0.0f},  {9.00, 0.80, 44, 0.94f, 0.0f},
            {10.00, 0.80, 42, 0.94f, 0.0f}, {11.00, 0.80, 41, 0.90f, 0.0f},
            {12.00, 3.85, 41, 1.00f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "21 Sidewinder Neuro Notch 4Bar.json", 172.0, 4, pump,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::retrig, 0.0f); // Continuous bar-locked spiral across rhythmic stabs!
                p.set(tg::ParamId::bellCenter, lerpF(190.0f, 540.0f, smoothStep(t)));
                p.set(tg::ParamId::filterHz, lerpF(460.0f, 2600.0f, smoothStep(t)));
                p.set(tg::ParamId::drive, lerpF(36.0f, 70.0f, t));
                p.set(tg::ParamId::space, lerpF(18.0f, 45.0f, t));
            });
        emitClip(outDir, "torus-glide", "01-sidewinder-bass-riser", "Sidewinder Neuro Riser", "Spiral Bass", 172.0, "F Phrygian",
                 "Rhythmic DnB bass stabs riding a continuous 7-octave Shepard spiral over a mono Sub-Anchor",
                 std::move(audio), -13.0);
    }

    // 2. Neo-Riemannian Film Score (ONE long cinematic progression gliding through all 4 scenes!)
    {
        const std::vector<NoteEvent> cue = {
            {0.0, 15.8, 48, 0.92f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "12 Neo-Riemannian Cinema.json", 112.0, 4, cue,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::morphAuto, 0.0f);
                p.set(tg::ParamId::morph, static_cast<float>(smoothStep(t) * 100.0));
                p.set(tg::ParamId::space, lerpF(40.0f, 82.0f, smoothStep(t)));
                p.set(tg::ParamId::filterHz, lerpF(1300.0f, 4600.0f, std::sin(kTwoPi * 0.5 * t)));
            });
        emitClip(outDir, "torus-glide", "02-neo-riemannian-cinema", "Neo-Riemannian Cinema", "Film Score", 112.0, "C Minor",
                 "Five-voice dark film progression bending smoothly across all four scenes via Tymoczko voice-leading",
                 std::move(audio), -13.8);
    }

    // 3. Phrygian Slide Table (Active rhythmic chord hits gliding through Scale Gravity Wells!)
    {
        const std::vector<NoteEvent> riff = {
            {0.00, 1.80, 41, 0.92f, 0.0f}, {2.00, 1.80, 41, 0.88f, 0.0f},
            {4.00, 1.40, 44, 0.94f, 0.0f}, {5.50, 2.30, 42, 0.92f, 0.0f},
            {8.00, 1.80, 41, 0.95f, 0.0f}, {10.00, 1.80, 46, 0.90f, 0.0f},
            {12.00, 1.40, 44, 0.94f, 0.0f},{13.50, 2.30, 41, 0.96f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "08 Phrygian Slide Table.json", 136.0, 4, riff,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::morphAuto, 0.0f);
                p.set(tg::ParamId::morph, static_cast<float>((0.5 - 0.5 * std::cos(kTwoPi * t * 1.5)) * 100.0));
                p.set(tg::ParamId::gravity, lerpF(42.0f, 82.0f, t));
                p.set(tg::ParamId::stagger, 48.0f);
            });
        emitClip(outDir, "torus-glide", "03-phrygian-slide-riff", "Phrygian Slide Table", "Poly-Bend", 136.0, "F Phrygian",
                 "Pedal-steel staggered 5-voice chord bends locking softly into Phrygian Scale Gravity Wells",
                 std::move(audio), -13.2);
    }

    // 4. Velvet Choir Cathedral (Warm Formant Choir blooming from unison into lush Just-Intonation chords!)
    {
        const std::vector<NoteEvent> choir = {
            {0.0, 3.85, 38, 0.88f, 0.0f},
            {4.0, 3.85, 34, 0.92f, 0.0f},
            {8.0, 3.85, 36, 0.94f, 0.0f},
            {12.0, 3.85, 38, 0.96f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "23 Velvet Choir Space Bloom.json", 108.0, 4, choir,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::morphAuto, 0.0f);
                p.set(tg::ParamId::morph, lerpF(18.0f, 100.0f, smoothStep(t)));
                p.set(tg::ParamId::space, lerpF(52.0f, 88.0f, t));
                // Warm vocal formant band (formantRatio 3.8..8.5, zero shrill overtones!)
                p.set(tg::ParamId::filterHz, lerpF(820.0f, 1850.0f, std::sin(kTwoPi * 0.5 * t)));
                p.set(tg::ParamId::drive, lerpF(18.0f, 34.0f, t));
            });
        emitClip(outDir, "torus-glide", "04-velvet-choir-cathedral", "Velvet Choir Cathedral", "Choir / JI", 108.0, "D Minor",
                 "Warm Formant Choir blooming from unison into pure Just-Intonation harmonies with upper-band Stereo Space",
                 std::move(audio), -13.8);
    }

    // 5. Risset Eternal Stutter (Möbius chord spiral + REAL 60..95% accelerating Risset rhythmic pulse gate!)
    {
        const std::vector<NoteEvent> stutter = {
            {0.00, 3.80, 41, 0.94f, 0.0f},
            {4.00, 3.80, 44, 0.96f, 0.0f},
            {8.00, 3.80, 42, 0.96f, 0.0f},
            {12.00, 3.85, 41, 1.00f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "20 Mobius Risset Stutter.json", 168.0, 4, stutter,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::retrig, 0.0f);
                p.set(tg::ParamId::rissetGate, lerpF(62.0f, 95.0f, t));
                p.set(tg::ParamId::filterHz, lerpF(720.0f, 4400.0f, smoothStep(t)));
                p.set(tg::ParamId::drive, lerpF(32.0f, 64.0f, t));
            });
        emitClip(outDir, "torus-glide", "05-risset-eternal-stutter", "Risset Eternal Stutter", "Risset Gate", 168.0, "F Spiral",
                 "Endless ascending Möbius chord spiral sliced by an eternal Risset accelerating pulse gate",
                 std::move(audio), -13.0);
    }

    // 6. Möbius Neuro Bass Riff (Active rhythmic bass re-triggers on Möbius Neuro Notch!)
    {
        const std::vector<NoteEvent> mobius = {
            {0.00, 2.80, 41, 0.96f, 0.0f}, {3.00, 0.90, 44, 0.92f, 0.0f},
            {4.00, 2.80, 42, 0.95f, 0.0f}, {7.00, 0.90, 41, 0.90f, 0.0f},
            {8.00, 2.80, 41, 0.98f, 0.0f}, {11.00, 0.90, 46, 0.94f, 0.0f},
            {12.00, 1.80, 44, 0.95f, 0.0f},{14.00, 1.85, 41, 0.98f, 0.0f},
        };
        auto audio = renderTorusClip(tgDir / "27 Mobius Neuro Notch Bass.json", 172.0, 4, mobius,
            [](tg::Params& p, double t) {
                p.set(tg::ParamId::retrig, 0.0f);
                p.set(tg::ParamId::filterHz, lerpF(520.0f, 2900.0f, 0.5 - 0.5 * std::cos(kTwoPi * t)));
                p.set(tg::ParamId::drive, lerpF(36.0f, 68.0f, t));
            });
        emitClip(outDir, "torus-glide", "06-mobius-neuro-bass", "Möbius Neuro Bass", "Möbius DnB", 172.0, "F Phrygian",
                 "Rhythmic DnB bass phrase combining endless chordal spirals with mono Sub-Anchor and Neuro Notch",
                 std::move(audio), -13.0);
    }
}


} // namespace hba::showcase
