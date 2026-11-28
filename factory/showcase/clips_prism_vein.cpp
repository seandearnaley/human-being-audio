// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "pv/engine.h"
#include "pv/preset.h"
#include "tf/engine.h"
#include "tf/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 9. PRISM VEIN (6 clips: real instruments coloured, the source kept up front, no drums)
// ============================================================================
namespace {

using Chord = std::vector<int>;

// One chord or note group held for `beats`, then the next.
struct HeldGroup {
    double startBeat;
    double durBeats;
    Chord notes;
    double vel = 0.88;
};

std::vector<float> renderSynthGroups(double bpm, int bars, const std::vector<HeldGroup>& groups, double cutoffHz,
                                     double attackSec, double releaseSec, double roomWet) {
    const int totalFrames = static_cast<int>(std::llround((4.0 * bars * 60.0 / bpm) * kSr));
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    LushMpeSynth synth;
    synth.globalCutoffHz = cutoffHz;
    synth.resonance = 0.9;
    synth.attackSec = attackSec;
    synth.releaseSec = releaseSec;
    const double samplesPerBeat = 60.0 / bpm * kSr;
    for (int i = 0; i < totalFrames; ++i) {
        for (const auto& g : groups) {
            const int on = static_cast<int>(std::llround(g.startBeat * samplesPerBeat));
            const int off = static_cast<int>(std::llround((g.startBeat + g.durBeats) * samplesPerBeat));
            if (i == on) {
                for (int n : g.notes)
                    synth.noteOn(1, n, g.vel);
            } else if (i == off) {
                for (int n : g.notes)
                    synth.noteOff(1, n);
            }
        }
        float l = 0.0f, r = 0.0f;
        synth.tick(l, r, static_cast<float>(roomWet));
        out[static_cast<std::size_t>(i) * 2u] = l;
        out[static_cast<std::size_t>(i) * 2u + 1u] = r;
    }
    return out;
}

// Runs a stereo source through Prism Vein with its own preset, optional MIDI chords (one entry per
// `chordBeats`), a setup override and per-block automation. The engine's look-ahead is compensated so
// the result stays on the source's grid.
std::vector<float> processWithPrismVein(const fs::path& presetPath, double bpm, const std::vector<float>& audio,
                                        const std::vector<Chord>& chords, double chordBeats,
                                        const std::function<void(pv::Params&)>& setup,
                                        const std::function<void(pv::Params&, double prog)>& automate) {
    std::string err;
    const auto parsed = pv::parsePreset(readTextFile(presetPath), &err);
    if (!parsed)
        throw std::runtime_error("Bad PV preset: " + presetPath.string() + " " + err);
    pv::Params params = parsed->params;
    params.set(pv::ParamId::demoAudition, 0.0f);
    if (setup)
        setup(params);

    pv::Engine engine;
    engine.prepare(kSr, kBlock);
    engine.setParams(params);
    engine.setChordMask(parsed->chordMask);
    engine.setLaneCurves(parsed->curves);

    const int frames = static_cast<int>(audio.size() / 2);
    const int latency = std::max(0, engine.latencySamples());
    const int total = frames + latency;
    std::vector<float> out(static_cast<std::size_t>(frames) * 2u, 0.0f);
    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    pv::Transport tr;
    tr.bpm = bpm;
    tr.playing = true;
    int lastChord = -1;
    for (int pos = 0; pos < total; pos += kBlock) {
        const int n = std::min(kBlock, total - pos);
        const double prog = std::min(1.0, static_cast<double>(pos) / static_cast<double>(frames));
        tr.ppq = static_cast<double>(pos) / kSr * (bpm / 60.0);
        engine.setTransport(tr);
        if (!chords.empty()) {
            const int idx = std::min(static_cast<int>(chords.size()) - 1, static_cast<int>(tr.ppq / chordBeats));
            if (idx != lastChord) {
                engine.allNotesOff();
                for (int note : chords[static_cast<std::size_t>(idx)])
                    engine.noteOn(note, 0.9f);
                lastChord = idx;
            }
        }
        if (automate) {
            automate(params, prog);
            engine.setParams(params);
        }
        for (int i = 0; i < n; ++i) {
            const int src = pos + i;
            const bool in = src < frames;
            inL[static_cast<std::size_t>(i)] = in ? audio[static_cast<std::size_t>(src) * 2u] : 0.0f;
            inR[static_cast<std::size_t>(i)] = in ? audio[static_cast<std::size_t>(src) * 2u + 1u] : 0.0f;
        }
        engine.process(inL.data(), inR.data(), nullptr, nullptr, outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i) {
            const int dst = pos + i - latency;
            if (dst >= 0 && dst < frames) {
                out[static_cast<std::size_t>(dst) * 2u] = outL[static_cast<std::size_t>(i)];
                out[static_cast<std::size_t>(dst) * 2u + 1u] = outR[static_cast<std::size_t>(i)];
            }
        }
    }
    return out;
}

// Keep the instrument audible: less generated colour, more of the source's own harmonics.
std::function<void(pv::Params&)> sourceForward(float color, float source) {
    return [color, source](pv::Params& p) {
        p.set(pv::ParamId::color, color);
        p.set(pv::ParamId::inHarmMix, source);
    };
}

} // namespace

void renderPrismVeinClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path pvDir = repoRoot / "products/prism-vein/presets";
    const fs::path tfDir = repoRoot / "products/tract-form/presets";
    constexpr double bpm = 172.0;

    // Long reese notes, two bars each: F, Db, Eb, C.
    const std::vector<NoteEvent> longReese = {
        {0.0, 7.6, 41, 0.92f, 0.0f}, {8.0, 7.6, 37, 0.92f, 0.0f}, {16.0, 7.6, 39, 0.92f, 0.0f}, {24.0, 7.8, 36, 0.94f, 0.0f}};
    const std::vector<Chord> minorChords = {
        {53, 56, 60, 63, 67}, {49, 53, 56, 60, 63}, {51, 55, 58, 62, 65}, {48, 51, 55, 58, 62}};

    // 1. Iridescent Reese (held Tract Form reese through the Chord Lattice, chords following the bass)
    {
        auto reese = renderTractSequence(tfDir / "coupled-reese.json", bpm, 8, longReese,
            [](tf::Params& p, tf::Engine&, double) {
                p[tf::ParamId::tract] = 0.0f;
                p[tf::ParamId::drive] = 0.2f;
            });
        auto wet = processWithPrismVein(pvDir / "01-iridescent-f-min9.json", bpm, reese, minorChords, 8.0,
            sourceForward(0.55f, 0.72f),
            [](pv::Params& p, double t) { p.set(pv::ParamId::motion, lerpF(0.3f, 0.75f, smoothStep(t))); });
        emitClip(outDir, "prism-vein", "01-iridescent-reese", "Iridescent Reese", "Chord Lattice", bpm, "F Minor",
                 "A held reese refracted through the Chord Lattice as it follows F min9, Db maj9, Eb maj9 and C min9, the bass kept up front",
                 std::move(wet), -13.2);
    }

    // 2. Silk Pad Colour (saw chord pad, the lattice following the same chords)
    {
        const std::vector<Chord> silk = {
            {51, 55, 58, 62, 65}, {48, 55, 58, 62, 63}, {44, 51, 55, 58, 60}, {46, 53, 55, 60, 62}};
        std::vector<HeldGroup> groups;
        for (int k = 0; k < 4; ++k)
            groups.push_back({8.0 * k, 7.7, silk[static_cast<std::size_t>(k)], 0.84});
        auto pad = renderSynthGroups(bpm, 8, groups, 4200.0, 0.18, 0.7, 0.22);
        auto wet = processWithPrismVein(pvDir / "14-eb-maj9-silk-pad.json", bpm, pad, silk, 8.0,
            sourceForward(0.5f, 0.72f), nullptr);
        emitClip(outDir, "prism-vein", "02-silk-pad-colour", "Silk Pad Colour", "Chord Lattice", bpm, "Eb Major",
                 "A saw pad moving Eb maj9, C min9, Ab maj9 and Bb 6/9, with the lattice adding glass above the chords",
                 std::move(wet), -13.6);
    }

    // 3. Razor Comb Roller (rolling reese bassline through the Harmonic Comb)
    {
        const std::vector<NoteEvent> roller4 = {
            {0.00, 1.45, 29, 1.00f, 0.0f}, {1.75, 0.70, 29, 0.85f, 0.0f}, {2.50, 0.95, 32, 0.92f, 0.0f},
            {3.50, 0.45, 30, 0.86f, 0.0f}, {4.00, 1.45, 29, 1.00f, 0.0f}, {5.75, 0.70, 36, 0.90f, 0.0f},
            {6.50, 0.95, 34, 0.92f, 0.0f}, {7.50, 0.45, 32, 0.86f, 0.0f}, {8.00, 1.45, 29, 1.00f, 0.0f},
            {9.75, 0.70, 39, 0.90f, 0.0f}, {10.50, 0.95, 37, 0.92f, 0.0f}, {11.50, 0.45, 36, 0.86f, 0.0f},
            {12.00, 1.45, 29, 1.00f, 0.0f}, {13.75, 0.70, 32, 0.90f, 0.0f}, {14.50, 0.70, 34, 0.92f, 0.0f},
            {15.25, 0.70, 36, 0.95f, 0.0f}};
        std::vector<NoteEvent> roller;
        for (int r = 0; r < 2; ++r)
            for (auto n : roller4) {
                n.startBeat += 16.0 * r;
                roller.push_back(n);
            }
        auto bass = renderTractSequence(tfDir / "coupled-reese.json", bpm, 8, roller,
            [](tf::Params& p, tf::Engine&, double) {
                p[tf::ParamId::tract] = 0.0f;
                p[tf::ParamId::drive] = 0.24f;
            });
        auto wet = processWithPrismVein(pvDir / "08-phrygian-razor-comb.json", bpm, bass, {}, 8.0,
            sourceForward(0.5f, 0.66f),
            [](pv::Params& p, double t) { p.set(pv::ParamId::decay, lerpF(0.35f, 0.7f, t)); });
        emitClip(outDir, "prism-vein", "03-razor-comb-roller", "Razor Comb Roller", "Harmonic Comb", bpm, "F Phrygian",
                 "A rolling reese bassline through the 12-voice Harmonic Comb tuned to Phrygian intervals, its decay opening up",
                 std::move(wet), -13.0);
    }

    // 4. Lush Saw Halo (held detuned saw fifths under a lattice halo)
    {
        std::vector<HeldGroup> fifths;
        const int roots[4] = {41, 37, 39, 36};
        for (int k = 0; k < 4; ++k)
            fifths.push_back({8.0 * k, 7.6, {roots[k], roots[k] + 7, roots[k] + 12}, 0.9});
        auto saws = renderSynthGroups(bpm, 8, fifths, 5200.0, 0.12, 0.6, 0.18);
        auto wet = processWithPrismVein(pvDir / "16-lush-saw-halo.json", bpm, saws, minorChords, 8.0,
            sourceForward(0.55f, 0.7f),
            [](pv::Params& p, double t) { p.set(pv::ParamId::stereoFan, lerpF(0.3f, 0.8f, smoothStep(t))); });
        emitClip(outDir, "prism-vein", "04-lush-saw-halo", "Lush Saw Halo", "Chord Lattice", bpm, "F Minor",
                 "Held detuned saw fifths with a lattice halo widening across the stereo fan as the chords move",
                 std::move(wet), -13.4);
    }

    // 5. Glass Freeze Pluck (a plucked arpeggio with a Glass Freeze halo behind it; the arpeggio's chords are
    //    held as MIDI so the frozen halo follows the harmony)
    {
        const std::vector<Chord> arps = {{65, 68, 72, 75, 77, 75, 72, 68},  // F min7
                                         {61, 65, 68, 72, 73, 72, 68, 65},  // Db maj7
                                         {63, 67, 70, 72, 75, 72, 70, 67},  // Eb 6
                                         {60, 63, 67, 70, 72, 70, 67, 63}}; // C min7
        std::vector<HeldGroup> plucks;
        for (int bar = 0; bar < 8; ++bar)
            for (int s = 0; s < 8; ++s)
                plucks.push_back({bar * 4.0 + s * 0.5, 0.32,
                                  {arps[static_cast<std::size_t>(bar / 2)][static_cast<std::size_t>(s)]},
                                  s % 2 == 0 ? 0.9 : 0.72});
        auto pl = renderSynthGroups(bpm, 8, plucks, 3800.0, 0.004, 0.22, 0.2);
        const std::vector<Chord> held = {{65, 68, 72, 75}, {61, 65, 68, 72}, {63, 67, 70, 72}, {60, 63, 67, 70}};
        auto wet = processWithPrismVein(pvDir / "22-glass-halo-freeze.json", bpm, pl, held, 8.0,
            sourceForward(0.42f, 0.78f), nullptr);
        emitClip(outDir, "prism-vein", "05-glass-freeze-pluck", "Glass Freeze Pluck", "Glass Freeze", bpm, "F Minor",
                 "A plucked arpeggio over F min7, Db maj7, Eb 6 and C min7, kept dry and clear while Glass Freeze holds a halo of its harmonics behind it",
                 std::move(wet), -14.0);
    }

    // 6. Soothe Growl Polish (a gritty Tract Form growl calmed by Bark Auto-Tame)
    {
        const std::vector<NoteEvent> growl = {
            {0.0, 3.6, 29, 0.98f, 0.0f}, {4.0, 1.6, 32, 0.92f, 0.0f}, {6.0, 1.7, 31, 0.92f, -1.0f},
            {8.0, 3.6, 29, 0.98f, 0.0f}, {12.0, 1.6, 36, 0.94f, 0.0f}, {14.0, 1.8, 34, 0.94f, -2.0f},
            {16.0, 3.6, 29, 0.98f, 0.0f}, {20.0, 1.6, 32, 0.92f, 0.0f}, {22.0, 1.7, 31, 0.92f, -1.0f},
            {24.0, 3.6, 29, 0.98f, 0.0f}, {28.0, 3.8, 24, 1.00f, 5.0f}};
        auto bass = renderTractSequence(tfDir / "throat-wobble.json", bpm, 8, growl, nullptr);
        // Held F min9 keys so the lattice sheen stays in the growl's key.
        auto wet = processWithPrismVein(pvDir / "18-soothe-prism-polish.json", bpm, bass, {{53, 56, 60, 63, 67}}, 32.0,
            sourceForward(0.5f, 0.62f), nullptr);
        emitClip(outDir, "prism-vein", "06-soothe-growl-polish", "Soothe Growl Polish", "Auto-Tame", bpm, "F Minor",
                 "A wobbling Tract Form growl with Bark Auto-Tame calming its harsh resonances and a light prism sheen on top",
                 std::move(wet), -13.2);
    }
}

} // namespace hba::showcase
