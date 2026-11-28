// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "tf/engine.h"
#include "tf/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 2. TRACT FORM (6 clips: 5 composed riffs, two over a drum groove, + 1 evolving cinema sub drone)
// ============================================================================
std::vector<float> renderTractSequence(const fs::path& presetPath, double bpm, int bars,
                                       const std::vector<NoteEvent>& notes,
                                       std::function<void(tf::Params&, tf::Engine&, double prog)> automate) {
    const auto parsed = tf::parsePreset(readTextFile(presetPath));
    if (!parsed) throw std::runtime_error("Bad TF preset: " + presetPath.string());
    tf::Params params = parsed->params;

    tf::Engine engine;
    engine.prepare(kSr, kBlock, 4);
    engine.setLaneCurves(parsed->curves);
    engine.setPitchBendRange(12.0f);

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

        // Determine currently sounding note
        int found = -1;
        for (int idx = 0; idx < static_cast<int>(notes.size()); ++idx) {
            if (beat >= notes[idx].startBeat && beat < notes[idx].startBeat + notes[idx].durBeats) {
                found = idx;
                break;
            }
        }
        if (found != activeIdx) {
            if (activeIdx >= 0 && found < 0) engine.noteOff(notes[activeIdx].note);
            if (found >= 0) {
                engine.pitchBend(0.0f);
                engine.noteOn(notes[found].note, notes[found].vel);
            }
            activeIdx = found;
        }
        if (activeIdx >= 0 && std::abs(notes[activeIdx].bendEndSt) > 0.01f) {
            const double rel = (beat - notes[activeIdx].startBeat) / notes[activeIdx].durBeats;
            const double bendT = smoothStep((rel - 0.35) / 0.65);
            engine.pitchBend(static_cast<float>(notes[activeIdx].bendEndSt * bendT));
        }

        if (automate) automate(params, engine, prog);

        engine.process(params, {bpm, beat, true}, bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i) {
            out[static_cast<std::size_t>(pos + i) * 2u] = bufL[i];
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[i];
        }
    }
    return out;
}

// Puts a showcase-kit stepper groove about 5 LU under a bass render, ducking the bass for 250 ms after each
// kick so the sub and the kick take turns, as they would in a mix.
static std::vector<float> withStepperDrums(std::vector<float> bass, double bpm, int bars) {
    BreakPattern pat = stepperBreak(bpm);
    pat.withSub = false;
    pat.withDubStabs = false;
    const auto drums = renderBreakStem(pat, bars);
    const double samplesPerBeat = 60.0 / bpm * kSr;
    std::vector<double> kicks;
    for (int bar = 0; bar < bars; ++bar)
        for (const auto& hit : pat.hits)
            if (hit.note == 36) kicks.push_back((4.0 * bar + hit.beat) * samplesPerBeat);
    const std::size_t frames = std::min(bass.size(), drums.size()) / 2u;
    for (std::size_t i = 0; i < frames; ++i) {
        double duck = 1.0;
        for (const double k : kicks) {
            const double dt = (static_cast<double>(i) - k) / kSr;
            if (dt >= 0.0 && dt < 0.25) duck = std::min(duck, 1.0 - 0.45 * std::exp(-dt / 0.07));
        }
        for (std::size_t c = 0; c < 2u; ++c)
            bass[i * 2u + c] = static_cast<float>(bass[i * 2u + c] * duck + drums[i * 2u + c] * 0.4);
    }
    return bass;
}

void renderTractFormClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path pDir = repoRoot / "products/tract-form/presets";

    // 1. Techstep Roller (172 BPM, F Minor, rolling reese line over a stepper groove)
    {
        const std::vector<NoteEvent> roller = {
            {0.00, 1.45, 29, 1.00f, 0.0f},  {1.75, 0.70, 29, 0.85f, 0.0f},
            {2.50, 0.95, 32, 0.92f, 0.0f},  {3.50, 0.45, 31, 0.86f, 0.0f},
            {4.00, 1.45, 29, 1.00f, 0.0f},  {5.75, 0.70, 36, 0.90f, 0.0f},
            {6.50, 0.95, 34, 0.92f, -2.0f}, {7.50, 0.45, 32, 0.86f, 0.0f},
            {8.00, 1.45, 29, 1.00f, 0.0f},  {9.75, 0.70, 39, 0.90f, 0.0f},
            {10.50, 0.95, 37, 0.92f, -1.0f},{11.50, 0.45, 36, 0.86f, 0.0f},
            {12.00, 1.45, 29, 1.00f, 0.0f}, {13.75, 0.70, 32, 0.90f, 0.0f},
            {14.50, 0.70, 34, 0.92f, 0.0f}, {15.25, 0.70, 36, 0.95f, -7.0f},
        };
        auto audio = renderTractSequence(pDir / "dark-techstep-roller.json", 172.0, 4, roller,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::tract] = lerpF(0.35f, 0.70f, 0.5 - 0.5 * std::cos(kTwoPi * t));
            });
        emitClip(outDir, "tract-form", "01-techstep-roller", "Techstep Roller", "172 DnB", 172.0, "F Minor",
                 "Dark Techstep Roller rolling a 4-bar F minor reese line over a stepper groove, sub ducked under the kicks",
                 withStepperDrums(std::move(audio), 172.0, 4), -13.0);
    }

    // 2. Throat Wobble (172 BPM, F Phrygian, syncopated neuro line that leaves the snares clear)
    {
        const std::vector<NoteEvent> talker = {
            {0.00, 0.70, 29, 1.00f, 0.0f},  {0.75, 0.20, 29, 0.80f, 0.0f},
            {1.25, 0.45, 32, 0.92f, 0.0f},  {1.75, 0.20, 29, 0.80f, 0.0f},
            {2.00, 0.95, 29, 0.98f, 0.0f},  {3.25, 0.45, 36, 0.90f, 0.0f},
            {3.75, 0.20, 34, 0.82f, 0.0f},  {4.00, 0.70, 29, 1.00f, 0.0f},
            {4.75, 0.20, 29, 0.80f, 0.0f},  {5.25, 0.45, 30, 0.92f, 0.0f},
            {5.75, 0.20, 29, 0.80f, 0.0f},  {6.00, 1.20, 29, 0.98f, -2.0f},
            {7.50, 0.45, 32, 0.90f, 0.0f},  {8.00, 0.70, 29, 1.00f, 0.0f},
            {8.75, 0.20, 29, 0.80f, 0.0f},  {9.25, 0.45, 32, 0.92f, 0.0f},
            {9.75, 0.20, 29, 0.80f, 0.0f},  {10.00, 0.95, 29, 0.98f, 0.0f},
            {11.25, 0.45, 37, 0.90f, 0.0f}, {11.75, 0.20, 36, 0.82f, 0.0f},
            {12.00, 0.70, 29, 1.00f, 0.0f}, {12.75, 0.20, 29, 0.80f, 0.0f},
            {13.25, 0.45, 30, 0.92f, 0.0f}, {13.75, 0.20, 32, 0.80f, 0.0f},
            {14.00, 1.70, 29, 1.00f, -5.0f},
        };
        auto audio = renderTractSequence(pDir / "throat-wobble.json", 172.0, 4, talker, nullptr);
        emitClip(outDir, "tract-form", "02-throat-wobble", "Throat Wobble", "172 Neuro", 172.0, "F Phrygian",
                 "Throat Wobble's tempo-locked vowel lanes talking through a syncopated neuro line over a stepper groove",
                 withStepperDrums(std::move(audio), 172.0, 4), -13.0);
    }

    // 3. 808 Sub Hammer (138 BPM, E Minor, cavernous halftime weight + sub slides)
    {
        const std::vector<NoteEvent> sub808 = {
            {0.00, 2.20, 28, 1.00f, 0.0f},  {2.50, 1.25, 31, 0.92f, 0.0f},
            {4.00, 1.75, 28, 1.00f, 7.0f},  {6.00, 1.75, 35, 0.94f, -4.0f},
            {8.00, 2.20, 28, 1.00f, 0.0f},  {10.50, 1.25, 33, 0.94f, -2.0f},
            {12.00, 1.50, 31, 0.96f, 0.0f}, {13.50, 2.20, 28, 1.00f, 0.0f},
        };
        auto audio = renderTractSequence(pDir / "sub-808-hammer.json", 138.0, 4, sub808,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::subWarmth] = lerpF(0.18f, 0.62f, t);
                p[tf::ParamId::drive] = lerpF(0.15f, 0.48f, t);
            });
        emitClip(outDir, "tract-form", "03-808-sub-hammer", "808 Sub Hammer", "Halftime", 138.0, "E Minor",
                 "Heavy halftime 808 glides with phase-locked Sub Punch and warming harmonic saturation",
                 std::move(audio), -12.6);
    }

    // 4. Talking Vowel Slap (172 BPM, F Minor, rhythmic vocal formant conversation)
    {
        const std::vector<NoteEvent> talk = {
            {0.00, 0.90, 29, 0.96f, 0.0f},  {1.00, 0.85, 36, 0.92f, 0.0f},
            {2.00, 0.90, 34, 0.94f, -2.0f}, {3.00, 0.85, 32, 0.90f, 0.0f},
            {4.00, 1.35, 29, 1.00f, 0.0f},  {5.50, 0.70, 37, 0.94f, -1.0f},
            {6.25, 0.70, 36, 0.90f, 0.0f},  {7.00, 0.85, 34, 0.92f, 0.0f},
            {8.00, 0.90, 29, 0.98f, 3.0f},  {9.00, 0.85, 32, 0.94f, 0.0f},
            {10.00, 0.90, 30, 0.95f, -1.0f},{11.00, 0.85, 29, 0.92f, 0.0f},
            {12.00, 1.45, 36, 0.98f, -4.0f},{13.50, 2.30, 29, 1.00f, 0.0f},
        };
        auto audio = renderTractSequence(pDir / "vowel-slap.json", 172.0, 4, talk,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::morph] = lerpF(0.08f, 0.95f, 0.5 + 0.5 * std::sin(kTwoPi * t * 2.5));
                p[tf::ParamId::tractQ] = lerpF(4.0f, 9.5f, t);
                p[tf::ParamId::width] = lerpF(0.45f, 0.88f, t);
            });
        emitClip(outDir, "tract-form", "04-talking-vowel-slap", "Talking Vowel Slap", "Formant Funk", 172.0, "F Minor",
                 "Call-and-response neurofunk vowel line with sweeping Pre/Post formant resonance",
                 std::move(audio), -13.2);
    }

    // 5. West-Coast Buchla Riff (128 BPM, C Minor, dark cyberpunk wavefolder lead/bass)
    {
        const std::vector<NoteEvent> west = {
            {0.00, 0.70, 36, 0.95f, 0.0f},  {0.75, 0.70, 43, 0.88f, 0.0f},
            {1.50, 0.90, 44, 0.92f, -1.0f}, {2.50, 0.70, 39, 0.90f, 0.0f},
            {3.25, 0.65, 38, 0.86f, 0.0f},  {4.00, 0.70, 36, 0.98f, 0.0f},
            {4.75, 0.70, 48, 0.94f, -5.0f}, {5.50, 0.90, 43, 0.92f, 0.0f},
            {6.50, 1.35, 39, 0.96f, -3.0f}, {8.00, 0.70, 36, 0.96f, 0.0f},
            {8.75, 0.70, 46, 0.92f, 0.0f},  {9.50, 0.90, 44, 0.94f, -1.0f},
            {10.50, 0.70, 43, 0.90f, 0.0f}, {11.25, 0.65, 39, 0.88f, 0.0f},
            {12.00, 1.45, 38, 0.98f, -2.0f},{13.50, 2.30, 36, 1.00f, 0.0f},
        };
        auto audio = renderTractSequence(pDir / "west-coast-reese.json", 128.0, 4, west,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::drive] = lerpF(0.22f, 0.60f, smoothStep(t));
                p[tf::ParamId::coupling] = lerpF(0.45f, 0.84f, t);
            });
        emitClip(outDir, "tract-form", "05-buchla-wavefold-riff", "Buchla Wavefold Riff", "Cyber / Electro", 128.0, "C Minor",
                 "West-Coast 5-cell Buchla 259 ADAA2 wavefolding opening across a dark minor arpeggio",
                 std::move(audio), -13.4);
    }

    // 6. Cathedral Sub Drone (ONE sustained evolving note: LR8 mono sub + blooming wide stereo vowels)
    {
        const std::vector<NoteEvent> drone = {{0.0, 15.8, 29, 0.95f, 0.0f}};
        auto audio = renderTractSequence(pDir / "low-drift.json", 136.0, 4, drone,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::morph] = lerpF(0.12f, 0.92f, smoothStep(t));
                p[tf::ParamId::width] = lerpF(0.25f, 0.95f, smoothStep(t));
                p[tf::ParamId::sculptDepth] = lerpF(0.20f, 0.85f, std::sin(kTwoPi * 0.5 * t));
                p[tf::ParamId::drive] = lerpF(0.18f, 0.46f, std::sin(kTwoPi * 0.5 * t));
            });
        emitClip(outDir, "tract-form", "06-cathedral-sub-drone", "Cathedral Sub Drone", "Film Drone", 136.0, "F1 Held",
                 "One sustained F1 sub-locked note blooming from pure weight into wide stereo vowel movement",
                 std::move(audio), -13.6);
    }
}


} // namespace hba::showcase
