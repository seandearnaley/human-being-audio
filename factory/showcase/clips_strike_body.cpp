// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"
#include "sb/engine.h"
#include "sb/preset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace hba::showcase {

// ============================================================================
// 6. STRIKE BODY — 6 Phase-Locked Drum & Nonlinear Modal Percussion Showcases
// ============================================================================
struct SbHit {
    double beat;
    int midiNote; // Kit Map: 36=Main, 37=Ghost, 38=Rim, 39=Roll; Chromatic: 60=C3 nominal
    float vel;
};

struct SbLayerSpec {
    fs::path presetPath;
    std::vector<SbHit> hits;
    float gain = 1.0f;
    std::function<void(sb::Params&, double)> automate;
};

std::vector<float> renderStrikeBodyMix(double bpm, int bars, const std::vector<SbLayerSpec>& layers) {
    const double beatsTotal = static_cast<double>(bars) * 4.0;
    const int totalFrames = static_cast<int>(std::llround((beatsTotal * 60.0 / bpm + 0.35) * kSr));
    const double beatsPerSample = bpm / 60.0 / kSr;
    std::vector<float> mix(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);

    for (const auto& layer : layers) {
        const auto parsed = sb::parsePresetJson(readTextFile(layer.presetPath));
        if (!parsed)
            throw std::runtime_error("Cannot parse Strike Body preset: " + layer.presetPath.string());
        sb::Params base = parsed->params;
        const auto aligned = sb::solveAutoAlign(base, kSr);
        base.set(sb::ParamId::bodyPhase, aligned.bodyPhaseDeg);
        base.set(sb::ParamId::shellOffsetMs, aligned.shellOffsetMs);
        base.set(sb::ParamId::shellInvert, aligned.shellInvert ? 1.0f : 0.0f);
        base.set(sb::ParamId::wireOffsetMs, aligned.wireOffsetMs);
        base.set(sb::ParamId::wireInvert, aligned.wireInvert ? 1.0f : 0.0f);

        auto sortedHits = layer.hits;
        std::sort(sortedHits.begin(), sortedHits.end(), [](const SbHit& a, const SbHit& b) { return a.beat < b.beat; });

        sb::Engine eng;
        eng.prepare(kSr, kBlock, 4);
        std::vector<float> bufL(kBlock), bufR(kBlock);
        std::size_t idx = 0;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double bStart = static_cast<double>(pos) * beatsPerSample;
            const double bEnd = static_cast<double>(pos + n) * beatsPerSample;
            const double t = std::clamp(bStart / std::max(1.0, beatsTotal), 0.0, 1.0);

            sb::Params cur = base;
            if (layer.automate)
                layer.automate(cur, t);

            while (idx < sortedHits.size() && sortedHits[idx].beat >= bStart && sortedHits[idx].beat < bEnd) {
                eng.noteOn(sortedHits[idx].midiNote, sortedHits[idx].vel, cur);
                ++idx;
            }
            eng.process(cur, bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i) {
                mix[static_cast<std::size_t>(pos + i) * 2u] += bufL[static_cast<std::size_t>(i)] * layer.gain;
                mix[static_cast<std::size_t>(pos + i) * 2u + 1u] += bufR[static_cast<std::size_t>(i)] * layer.gain;
            }
        }
    }
    return mix;
}

void renderStrikeBodyClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path sbDir = repoRoot / "products/strike-body/presets";

    // 1. Neuro Snare Roller (Full 4-bar 172 BPM DnB drum kit: 3 Strike Body instances — Kick + Neuro Snare + Hat!)
    {
        std::vector<SbHit> kickHits, snareHits, hatHits;
        for (int bar = 0; bar < 4; ++bar) {
            const double b = static_cast<double>(bar) * 4.0;
            kickHits.push_back({b + 0.00, 36, 1.00f});
            kickHits.push_back({b + 2.50, 36, 0.96f});
            if (bar == 1 || bar == 3)
                kickHits.push_back({b + 3.75, 36, 0.86f});

            snareHits.push_back({b + 1.00, 36, 1.00f});
            snareHits.push_back({b + 1.75, 37, 0.58f});
            snareHits.push_back({b + 2.25, 37, 0.52f});
            snareHits.push_back({b + 3.00, 36, 0.98f});
            if (bar == 1) {
                snareHits.push_back({b + 3.50, 38, 0.88f});
            } else if (bar == 3) {
                snareHits.push_back({b + 3.25, 39, 0.78f});
                snareHits.push_back({b + 3.50, 39, 0.88f});
                snareHits.push_back({b + 3.75, 39, 0.96f});
            } else {
                snareHits.push_back({b + 3.75, 37, 0.60f});
            }

            for (int step = 0; step < 8; ++step) {
                const double pos = b + static_cast<double>(step) * 0.5;
                hatHits.push_back({pos, (step % 2 == 0) ? 36 : 37, (step % 2 == 0) ? 0.80f : 0.48f});
            }
        }
        auto audio = renderStrikeBodyMix(172.0, 4, {
            {sbDir / "07 Phase-Locked DnB Kick.json", kickHits, 0.96f,
             [](sb::Params& p, double t) { p.set(sb::ParamId::macroPunch, lerpF(10.0f, 38.0f, t)); }},
            {sbDir / "01 Neuro Crack 185Hz.json", snareHits, 0.98f,
             [](sb::Params& p, double t) {
                 p.set(sb::ParamId::macroSmack, lerpF(8.0f, 44.0f, t));
                 p.set(sb::ParamId::macroMetal, lerpF(0.0f, 28.0f, t));
             }},
            {sbDir / "22 Razor Neuro Hat.json", hatHits, 0.36f,
             [](sb::Params& p, double t) { p.set(sb::ParamId::macroTight, lerpF(20.0f, 45.0f, t)); }},
        });
        emitClip(outDir, "strike-body", "01-neuro-snare-roller", "Neuro Snare Roller", "Neuro DnB", 172.0, "F#3 · 185 Hz",
                 "Full 172 BPM DnB drum groove built from three phase-locked Strike Body instances sweeping Punch & Smack",
                 std::move(audio), -11.5);
    }

    // 2. Dispersed Laser Kick (16-stage series allpass Disperser sweeping Disp Hz over a slamming techno/DnB pulse!)
    {
        std::vector<SbHit> hits, rims;
        for (int bar = 0; bar < 4; ++bar) {
            const double b = static_cast<double>(bar) * 4.0;
            for (int q = 0; q < 4; ++q)
                hits.push_back({b + static_cast<double>(q), 36, q == 0 ? 1.00f : 0.94f});
            if (bar == 1 || bar == 3)
                hits.push_back({b + 3.50, 36, 0.88f});
            rims.push_back({b + 1.50, 38, 0.72f});
            rims.push_back({b + 3.25, 37, 0.62f});
        }
        auto audio = renderStrikeBodyMix(142.0, 4, {
            {sbDir / "08 Dispersed Laser Kick.json", hits, 1.00f,
             [](sb::Params& p, double t) {
                 p.set(sb::ParamId::disperserHz, lerpF(105.0f, 260.0f, smoothStep(t)));
                 p.set(sb::ParamId::macroSmack, lerpF(15.0f, 52.0f, t));
             }},
            {sbDir / "03 Liquid Rim Snare.json", rims, 0.48f, nullptr},
        });
        emitClip(outDir, "strike-body", "02-dispersed-laser-kick", "Dispersed Laser Kick", "Club Kick", 142.0, "F#1 · 46 Hz",
                 "16-stage series flat-magnitude allpass Disperser sweeping 105–260 Hz with Laser Snap mallet & ADAA2 clip",
                 std::move(audio), -11.5);
    }

    // 3. Studio Acoustic Brass Break (Skin Thump mallet + Sympathetic Wire Buzz + Studio Micro-Room + Humanize!)
    {
        std::vector<SbHit> snare, kick;
        for (int bar = 0; bar < 4; ++bar) {
            const double b = static_cast<double>(bar) * 4.0;
            kick.push_back({b + 0.00, 36, 0.96f});
            kick.push_back({b + 1.50, 36, 0.80f});
            kick.push_back({b + 2.50, 36, 0.94f});
            snare.push_back({b + 1.00, 36, 0.98f});
            snare.push_back({b + 1.75, 37, 0.56f});
            snare.push_back({b + 2.25, 37, 0.48f});
            snare.push_back({b + 3.00, 36, 0.96f});
            snare.push_back({b + 3.50, 38, 0.86f});
            snare.push_back({b + 3.75, 37, 0.54f});
        }
        auto audio = renderStrikeBodyMix(162.0, 4, {
            {sbDir / "09 909 Chest Punch.json", kick, 0.82f, nullptr},
            {sbDir / "05 Studio Acoustic Brass.json", snare, 0.98f,
             [](sb::Params& p, double t) {
                 p.set(sb::ParamId::strikePos, lerpF(24.0f, 58.0f, 0.5 - 0.5 * std::cos(kTwoPi * t)));
                 p.set(sb::ParamId::roomAmt, lerpF(22.0f, 44.0f, t));
             }},
        });
        emitClip(outDir, "strike-body", "03-studio-brass-break", "Studio Brass Pocket", "Acoustic", 162.0, "G3 · 196 Hz",
                 "Skin Thump mallet, sympathetic snare-wire buzz, and tight stereo Studio Room across ghost notes and rims",
                 std::move(audio), -12.0);
    }

    // 4. Hyperpop SOPHIE Slap (Hollow Pipe + Laser Snap + Buchla ADAA2 + Layered Clap!)
    {
        std::vector<SbHit> slap, clap, sub;
        for (int bar = 0; bar < 4; ++bar) {
            const double b = static_cast<double>(bar) * 4.0;
            sub.push_back({b + 0.00, 36, 1.00f});
            sub.push_back({b + 2.25, 36, 0.92f});
            slap.push_back({b + 0.75, 36, 0.92f});
            slap.push_back({b + 1.50, 38, 0.88f});
            slap.push_back({b + 2.75, 36, 0.96f});
            slap.push_back({b + 3.50, 39, 0.94f});
            clap.push_back({b + 1.00, 36, 0.95f});
            clap.push_back({b + 3.00, 36, 0.98f});
        }
        auto audio = renderStrikeBodyMix(146.0, 4, {
            {sbDir / "07 Phase-Locked DnB Kick.json", sub, 0.86f, nullptr},
            {sbDir / "13 Hyperpop SOPHIE Slap.json", slap, 0.90f,
             [](sb::Params& p, double t) {
                 p.set(sb::ParamId::shellTension, lerpF(62.0f, 96.0f, t));
                 p.set(sb::ParamId::strikePos, lerpF(25.0f, 82.0f, t));
             }},
            {sbDir / "12 Layered Neuro Clap.json", clap, 0.84f, nullptr},
        });
        emitClip(outDir, "strike-body", "04-hyperpop-sophie-slap", "Hyperpop Plastic Slap", "Hyperpop", 146.0, "C#4 · 272 Hz",
                 "Hollow Pipe modes excited by Laser Snap & Rim Flam mallets sweeping tension into Buchla ADAA2",
                 std::move(audio), -11.8);
    }

    // 5. Kirchhoff Anvil Ritual (Industrial Kirchhoff Plate + Metal Hammer + Halftime Brick Snare!)
    {
        std::vector<SbHit> anvil, brick;
        const std::vector<double> pat = {0.0, 0.75, 1.5, 2.0, 3.0, 3.5};
        for (int bar = 0; bar < 4; ++bar) {
            const double b = static_cast<double>(bar) * 4.0;
            for (std::size_t i = 0; i < pat.size(); ++i)
                anvil.push_back({b + pat[i], (i % 3 == 0) ? 36 : 38, (i == 0 || i == 4) ? 1.0f : 0.78f});
            brick.push_back({b + 2.00, 36, 1.00f});
            if (bar == 3)
                brick.push_back({b + 3.50, 39, 0.92f});
        }
        auto audio = renderStrikeBodyMix(134.0, 4, {
            {sbDir / "15 Kirchhoff Anvil Hit.json", anvil, 0.88f,
             [](sb::Params& p, double t) {
                 p.set(sb::ParamId::macroMetal, lerpF(10.0f, 70.0f, t));
                 p.set(sb::ParamId::shellStretch, lerpF(10.0f, 44.0f, t));
             }},
            {sbDir / "04 Halftime Brick Snare.json", brick, 0.92f, nullptr},
        });
        emitClip(outDir, "strike-body", "05-kirchhoff-anvil-ritual", "Kirchhoff Anvil Ritual", "Industrial", 134.0, "D3 · 148 Hz",
                 "24-mode Kirchhoff steel plate hammered with Metal Hammer over a halftime Sub-Square brick snare",
                 std::move(audio), -11.8);
    }

    // 6. Chromatic Glass Bell & Tensioned Floor Tom (Poly 4x chromatic melodic percussion call-and-response!)
    {
        const std::vector<SbHit> bells = {
            {0.00, 60, 0.92f}, {0.75, 63, 0.82f}, {1.50, 67, 0.88f}, {2.50, 70, 0.85f},
            {4.00, 62, 0.94f}, {4.75, 65, 0.84f}, {5.50, 67, 0.90f}, {6.50, 72, 0.92f},
            {8.00, 60, 0.95f}, {8.75, 67, 0.88f}, {9.50, 70, 0.92f}, {10.50, 75, 0.90f},
            {12.00, 65, 0.92f}, {12.75, 63, 0.86f}, {13.50, 62, 0.90f}, {14.50, 60, 0.96f},
        };
        const std::vector<SbHit> toms = {
            {0.00, 55, 0.94f}, {2.00, 60, 0.88f}, {3.25, 58, 0.82f},
            {4.00, 53, 0.95f}, {6.00, 57, 0.90f}, {7.50, 55, 0.86f},
            {8.00, 55, 0.96f}, {10.00, 62, 0.90f}, {11.25, 60, 0.88f},
            {12.00, 51, 0.96f}, {14.00, 55, 0.92f}, {15.00, 48, 0.98f},
        };
        auto audio = renderStrikeBodyMix(128.0, 4, {
            {sbDir / "17 Glass Bell Perc.json", bells, 0.80f,
             [](sb::Params& p, double t) { p.set(sb::ParamId::roomAmt, lerpF(35.0f, 65.0f, t)); }},
            {sbDir / "19 Tensioned Floor Tom.json", toms, 0.90f,
             [](sb::Params& p, double t) { p.set(sb::ParamId::shellTension, lerpF(45.0f, 75.0f, t)); }},
        });
        emitClip(outDir, "strike-body", "06-chromatic-tom-bells", "Chromatic Glass & Toms", "Chromatic", 128.0, "C Minor",
                 "Poly 4× Chromatic Glass Bell shimmer trading melodic phrases with nonlinear Tensioned Floor Toms",
                 std::move(audio), -12.6);
    }
}


} // namespace hba::showcase
