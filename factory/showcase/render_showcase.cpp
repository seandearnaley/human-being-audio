// SPDX-License-Identifier: GPL-3.0-or-later
// Unified Human Being Audio web showcase renderer.
// Links all 5 pure C++20 engines (LivecutEngine, tf_engine, pf_engine, cl_engine, tg_engine)
// to render 30 composed, parameter-automated, record-ready musical clips (6 per product)
// at 48 kHz stereo float WAV + catalog metadata JSON.

#include "cl/engine.h"
#include "cl/preset.h"
#include "engine/Cutter.h"
#include "engine/PadPrograms.h"
#include "pf/engine.h"
#include "pf/preset.h"
#include "sb/engine.h"
#include "sb/preset.h"
#include "tf/engine.h"
#include "tf/preset.h"
#include "tg/engine.h"
#include "tg/preset.h"

#include <hba/dsp/svf.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;
constexpr double kTwoPi = 6.28318530717959;

double midiToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

float lerpF(float a, float b, double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return a + static_cast<float>(c) * (b - a);
}

double smoothStep(double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

std::string readTextFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Write 48 kHz 32-bit IEEE float stereo WAV
bool writeWavFloat32(const fs::path& path, const std::vector<float>& stereo, int sampleRate) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const auto dataBytes = static_cast<std::uint32_t>(stereo.size() * sizeof(float));
    const std::uint32_t riffSize = 36u + dataBytes;
    const std::uint16_t formatTag = 3u; // IEEE float
    const std::uint16_t channels = 2u;
    const auto sr = static_cast<std::uint32_t>(sampleRate);
    const std::uint32_t byteRate = sr * channels * 4u;
    const std::uint16_t blockAlign = channels * 4u;
    const std::uint16_t bits = 32u;
    const std::uint32_t fmtSize = 16u;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&formatTag), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    out.write(reinterpret_cast<const char*>(stereo.data()), static_cast<std::streamsize>(dataBytes));
    return static_cast<bool>(out);
}

// Gentle studio polish: DC block, RMS-aware target normalization, soft safety ceiling (-1.2 dBFS), clean fades
void masterClip(std::vector<float>& stereo, double targetRmsDb = -13.5, double peakCeilDb = -1.2) {
    const std::size_t frames = stereo.size() / 2;
    if (frames < 128) return;

    // 1. 12 Hz DC blocker
    double x1L = 0, y1L = 0, x1R = 0, y1R = 0;
    const double R = 0.9984;
    for (std::size_t i = 0; i < frames; ++i) {
        const double xL = stereo[i * 2];
        const double xR = stereo[i * 2 + 1];
        const double yL = xL - x1L + R * y1L;
        const double yR = xR - x1R + R * y1R;
        x1L = xL; y1L = yL;
        x1R = xR; y1R = yR;
        stereo[i * 2] = static_cast<float>(yL);
        stereo[i * 2 + 1] = static_cast<float>(yR);
    }

    // 2. Measure active RMS and peak
    double sumSq = 0.0;
    double peak = 1e-6;
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = stereo[i * 2];
        const double r = stereo[i * 2 + 1];
        sumSq += 0.5 * (l * l + r * r);
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(frames));
    const double targetRms = std::pow(10.0, targetRmsDb / 20.0);
    const double ceilLin = std::pow(10.0, peakCeilDb / 20.0);

    // Choose gain to hit balanced perceived loudness without crushing dynamic peaks
    const double gainByRms = targetRms / std::max(rms, 1e-5);
    const double gainByPeak = (ceilLin * 1.18) / peak;
    const double gain = std::min(gainByRms, gainByPeak);

    const std::size_t fadeIn = std::min<std::size_t>(frames / 10, static_cast<std::size_t>(0.006 * kSr));
    const std::size_t fadeOut = std::min<std::size_t>(frames / 4, static_cast<std::size_t>(0.065 * kSr));

    for (std::size_t i = 0; i < frames; ++i) {
        double env = 1.0;
        if (i < fadeIn) env *= static_cast<double>(i) / static_cast<double>(fadeIn);
        if (i >= frames - fadeOut)
            env *= static_cast<double>(frames - 1 - i) / static_cast<double>(fadeOut);

        auto shape = [&](double v) {
            v *= gain;
            // Transparent soft ceiling above 0.78 * ceilLin
            const double knee = ceilLin * 0.78;
            const double av = std::abs(v);
            if (av > knee) {
                const double head = ceilLin - knee;
                const double s = knee + head * std::tanh((av - knee) / head);
                v = v < 0.0 ? -s : s;
            }
            return static_cast<float>(v * env);
        };
        stereo[i * 2] = shape(stereo[i * 2]);
        stereo[i * 2 + 1] = shape(stereo[i * 2 + 1]);
    }
}

// ============================================================================
// Musical Building Blocks: Studio Drum Kit, Poly MPE Synth, Diffusion Air
// ============================================================================
struct StudioRoom {
    // Lightweight stereo Schroeder diffusion + early reflection gloss for cinematic pads/drums
    static constexpr int kTapL[4] = {1087, 1601, 2293, 3041};
    static constexpr int kTapR[4] = {1153, 1699, 2411, 3169};
    std::array<std::vector<float>, 4> dL, dR;
    std::array<int, 4> idxL{}, idxR{};
    float lpL = 0.0f, lpR = 0.0f;

    StudioRoom() {
        for (int i = 0; i < 4; ++i) {
            dL[i].assign(static_cast<std::size_t>(kTapL[i]), 0.0f);
            dR[i].assign(static_cast<std::size_t>(kTapR[i]), 0.0f);
        }
    }

    void process(float& l, float& r, float wet = 0.14f, float fb = 0.42f) {
        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const float outL = dL[i][static_cast<std::size_t>(idxL[i])];
            const float outR = dR[i][static_cast<std::size_t>(idxR[i])];
            const float sign = (i & 1) ? -1.0f : 1.0f;
            sumL += sign * outL;
            sumR += sign * outR;
            dL[i][static_cast<std::size_t>(idxL[i])] = l + outR * fb;
            dR[i][static_cast<std::size_t>(idxR[i])] = r + outL * fb;
            idxL[i] = (idxL[i] + 1) % kTapL[i];
            idxR[i] = (idxR[i] + 1) % kTapR[i];
        }
        lpL += 0.28f * (sumL * 0.25f - lpL);
        lpR += 0.28f * (sumR * 0.25f - lpR);
        l = l + wet * lpL;
        r = r + wet * lpR;
    }
};

struct StudioDrumKit {
    // Punchy, tight DnB / Jungle / Halftime drum synthesizer
    std::uint32_t rng = 0x51a7c3d1u;
    double kickEnv = 0.0, kickPitch = 0.0, kickPhase = 0.0, kickClick = 0.0;
    double snareEnv = 0.0, snareBodyEnv = 0.0, snarePhase = 0.0, snareTone = 212.0;
    double ghostEnv = 0.0, ghostPhase = 0.0;
    double hatEnv = 0.0, hatOpen = 0.0, hatDecay = 0.04;
    double rideEnv = 0.0, percEnv = 0.0, percPhase = 0.0, percFreq = 135.0;
    double hatPh[4]{};
    hba::dsp::SvfState snBp{}, hatHp{}, percBp{};
    StudioRoom room;

    double randNoise() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<double>(rng) / 2147483648.0 - 1.0;
    }

    void trigger(int midiNote, double vel01) {
        const double v = std::clamp(vel01, 0.05, 1.0);
        if (midiNote == 36) { // Kick
            kickEnv = v;
            kickPitch = 1.0;
            kickClick = v;
            kickPhase = 0.0;
        } else if (midiNote == 38) { // Main Crack Snare
            snareEnv = v;
            snareBodyEnv = v;
            snareTone = 198.0 + 24.0 * v;
            snarePhase = 0.0;
        } else if (midiNote == 39) { // Ghost Snare / Rim
            ghostEnv = v * 0.72;
            ghostPhase = 0.0;
        } else if (midiNote == 42) { // Closed Hat
            hatEnv = v;
            hatDecay = 0.032;
        } else if (midiNote == 46) { // Open Hat / Amen Ride
            hatEnv = v * 0.85;
            hatDecay = 0.145;
            rideEnv = v * 0.55;
        } else if (midiNote == 43) { // Tuned Tom / Perc
            percEnv = v * 0.85;
            percFreq = 130.0 + 55.0 * v;
            percPhase = 0.0;
        }
    }

    void tick(float& outL, float& outR, double subWeight = 1.0) {
        const double n = randNoise();

        // Kick: crisp sub sweep 165 Hz -> 49 Hz + short beater click
        double kSig = 0.0;
        if (kickEnv > 1e-5) {
            const double f = 49.0 + 130.0 * kickPitch * kickPitch;
            kickPhase += f / kSr;
            kSig = std::sin(kTwoPi * kickPhase) * kickEnv * 1.15 * subWeight;
            kSig += n * kickClick * 0.25;
            kickEnv *= std::exp(-1.0 / (0.115 * kSr));
            kickPitch *= std::exp(-1.0 / (0.024 * kSr));
            kickClick *= std::exp(-1.0 / (0.004 * kSr));
        }

        // Snare: resonant body + snappy bandpassed wire noise
        const auto snCoefs = hba::dsp::SvfCoefs::make(2400.0, 1.1, kSr);
        double snV1 = 0.0, snV2 = 0.0;
        snBp.tick(snCoefs, n, snV1, snV2);
        const double snBpOut = snCoefs.k * snV1;
        const double snHpOut = n - snCoefs.k * snV1 - snV2;
        double sSigL = 0.0, sSigR = 0.0;
        if (snareEnv > 1e-5) {
            const double f = snareTone * (1.0 + 0.55 * snareBodyEnv);
            snarePhase += f / kSr;
            const double body = std::sin(kTwoPi * snarePhase) * snareBodyEnv * 0.78;
            const double wire = (snBpOut * 0.95 + snHpOut * 0.45) * snareEnv * 0.85;
            const double sn = std::tanh((body + wire) * 1.35);
            sSigL = sn * 0.96;
            sSigR = sn * 1.02;
            snareEnv *= std::exp(-1.0 / (0.095 * kSr));
            snareBodyEnv *= std::exp(-1.0 / (0.042 * kSr));
        }

        // Ghost snare (crisp subtle funk)
        if (ghostEnv > 1e-5) {
            ghostPhase += 275.0 / kSr;
            const double g = (std::sin(kTwoPi * ghostPhase) * 0.32 + snBpOut * 0.75) * ghostEnv * 0.58;
            sSigL += g * 1.06;
            sSigR += g * 0.92;
            ghostEnv *= std::exp(-1.0 / (0.044 * kSr));
        }

        // Metallic Hi-Hat / Ride (inharmonic square cluster + highpass noise)
        const auto hpCoefs = hba::dsp::SvfCoefs::make(7600.0, 1.3, kSr);
        const double hpOut = hatHp.highpass(hpCoefs, n);
        double hSigL = 0.0, hSigR = 0.0;
        if (hatEnv > 1e-5 || rideEnv > 1e-5) {
            constexpr double kMetalHz[4] = {3140.0, 4490.0, 5810.0, 7920.0};
            double metal = 0.0;
            for (int i = 0; i < 4; ++i) {
                hatPh[i] += kMetalHz[i] / kSr;
                if (hatPh[i] >= 1.0) hatPh[i] -= 1.0;
                metal += (hatPh[i] < 0.5 ? 0.25 : -0.25);
            }
            const double h = (hpOut * 0.78 + metal * 0.22) * (hatEnv * 0.46 + rideEnv * 0.30);
            hSigL = h * 0.88;
            hSigR = h * 1.12;
            hatEnv *= std::exp(-1.0 / (hatDecay * kSr));
            rideEnv *= std::exp(-1.0 / (0.21 * kSr));
        }

        // Tom / Perc
        double pSig = 0.0;
        if (percEnv > 1e-5) {
            percPhase += (percFreq * (0.72 + 0.55 * percEnv)) / kSr;
            pSig = std::sin(kTwoPi * percPhase) * percEnv * 0.55;
            percEnv *= std::exp(-1.0 / (0.075 * kSr));
        }

        float l = static_cast<float>(kSig + sSigL + hSigL + pSig * 0.8);
        float r = static_cast<float>(kSig + sSigR + hSigR + pSig * 1.1);
        room.process(l, r, 0.11f, 0.36f);
        outL = l;
        outR = r;
    }
};

// Lush 8-voice MPE-capable Polyphonic Synth (for Pole Field cinema chords & Cutline MPE Chords)
struct LushMpeSynth {
    struct Voice {
        bool active = false;
        int channel = 1;
        int note = 60;
        double vel = 0.8;
        double env = 0.0;
        bool gate = false;
        double bendSt = 0.0;
        double cc74 = 0.5;
        double phA = 0.0, phB = 0.0, phSub = 0.0;
        double pan = 0.0;
        hba::dsp::SvfState lpL{}, lpR{};
    };
    std::array<Voice, 12> voices{};
    double bendRange = 48.0;
    double globalCutoffHz = 1800.0;
    double resonance = 1.6;
    double attackSec = 0.035;
    double releaseSec = 0.38;
    StudioRoom room;

    void noteOn(int ch, int note, double vel) {
        int slot = -1;
        double quietest = 1e9;
        for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
            if (!voices[i].active) { slot = i; break; }
            if (voices[i].env < quietest) { quietest = voices[i].env; slot = i; }
        }
        auto& v = voices[static_cast<std::size_t>(slot)];
        v.active = true;
        v.gate = true;
        v.channel = ch;
        v.note = note;
        v.vel = vel;
        v.bendSt = 0.0;
        v.cc74 = 0.5;
        v.pan = ((slot & 1) ? 0.34 : -0.34) * (0.4 + 0.6 * ((slot % 5) / 4.0));
        v.lpL.reset();
        v.lpR.reset();
    }

    void noteOff(int ch, int note) {
        for (auto& v : voices) {
            if (v.active && v.gate && v.note == note && (ch <= 1 || v.channel == ch))
                v.gate = false;
        }
    }

    void pitchBend(int ch, double semitones) {
        for (auto& v : voices) {
            if (v.active && (ch <= 1 || v.channel == ch)) v.bendSt = semitones;
        }
    }

    void slideCc74(int ch, double norm01) {
        for (auto& v : voices) {
            if (v.active && (ch <= 1 || v.channel == ch)) v.cc74 = norm01;
        }
    }

    void tick(float& outL, float& outR, float roomWet = 0.28f) {
        const double attRate = 1.0 - std::exp(-1.0 / (attackSec * kSr));
        const double relRate = std::exp(-1.0 / (releaseSec * kSr));
        double sumL = 0.0, sumR = 0.0;

        for (auto& v : voices) {
            if (!v.active) continue;
            if (v.gate) v.env += attRate * (1.0 - v.env);
            else {
                v.env *= relRate;
                if (v.env < 1e-4) { v.active = false; continue; }
            }
            const double f = midiToHz(static_cast<double>(v.note) + v.bendSt);
            v.phA += (f * 0.9965) / kSr;
            v.phB += (f * 1.0035) / kSr;
            v.phSub += (f * 0.5) / kSr;
            if (v.phA >= 1.0) v.phA -= 1.0;
            if (v.phB >= 1.0) v.phB -= 1.0;
            if (v.phSub >= 1.0) v.phSub -= 1.0;

            // Warm supersaw + sub triangle blend
            const double sawL = (2.0 * v.phA - 1.0) * 0.55 + std::sin(kTwoPi * v.phSub) * 0.32;
            const double sawR = (2.0 * v.phB - 1.0) * 0.55 + std::sin(kTwoPi * v.phSub) * 0.32;
            const double cut = std::clamp(globalCutoffHz * (0.38 + 1.85 * v.cc74) * (0.55 + 0.65 * v.env), 140.0, 12000.0);
            const auto coefs = hba::dsp::SvfCoefs::make(cut, resonance, kSr);
            const double fl = v.lpL.lowpass(coefs, sawL);
            const double fr = v.lpR.lowpass(coefs, sawR);

            const double g = v.env * v.vel * 0.24;
            const double pL = 0.72 - 0.35 * v.pan;
            const double pR = 0.72 + 0.35 * v.pan;
            sumL += fl * g * pL;
            sumR += fr * g * pR;
        }
        float l = static_cast<float>(sumL);
        float r = static_cast<float>(sumR);
        room.process(l, r, roomWet, 0.55f);
        outL = l;
        outR = r;
    }
};

// ============================================================================
// Composed Breakbeat + Dub Groove Generator (for Livecut Redux & Pole Field)
// ============================================================================
struct BreakPattern {
    struct Hit { double beat; int note; double vel; };
    std::vector<Hit> hits; // 1-bar (4-beat) repeating groove
    double bpm = 172.0;
    bool withSub = false;
    bool withDubStabs = false;
};

BreakPattern amenBreak(double bpm = 172.0) {
    return {
        {
            {0.00, 36, 1.00}, {0.00, 42, 0.75},
            {0.50, 36, 0.82}, {0.50, 42, 0.55},
            {1.00, 38, 1.00}, {1.00, 46, 0.70},
            {1.50, 42, 0.62}, {1.75, 39, 0.58},
            {2.00, 42, 0.68}, {2.25, 39, 0.52},
            {2.50, 36, 0.94}, {2.50, 42, 0.72},
            {3.00, 38, 0.98}, {3.00, 46, 0.78},
            {3.50, 42, 0.64}, {3.75, 39, 0.65},
        },
        bpm, true, false
    };
}

BreakPattern halftimeBreak(double bpm = 140.0) {
    return {
        {
            {0.00, 36, 1.00}, {0.00, 42, 0.72},
            {0.50, 42, 0.45}, {0.75, 36, 0.78},
            {1.00, 42, 0.60}, {1.50, 43, 0.72},
            {2.00, 38, 1.00}, {2.00, 46, 0.75},
            {2.50, 42, 0.55}, {2.75, 39, 0.62},
            {3.00, 42, 0.68}, {3.25, 36, 0.85},
            {3.50, 39, 0.55}, {3.75, 42, 0.72},
        },
        bpm, true, true
    };
}

BreakPattern stepperBreak(double bpm = 172.0) {
    return {
        {
            {0.00, 36, 1.00}, {0.00, 46, 0.65},
            {0.50, 42, 0.58}, {0.75, 39, 0.48},
            {1.00, 38, 0.98}, {1.00, 42, 0.72},
            {1.50, 42, 0.62}, {1.75, 36, 0.84},
            {2.00, 42, 0.68}, {2.25, 39, 0.55},
            {2.50, 36, 0.95}, {2.50, 46, 0.68},
            {3.00, 38, 1.00}, {3.00, 42, 0.74},
            {3.50, 43, 0.62}, {3.75, 39, 0.62},
        },
        bpm, true, true
    };
}

std::vector<float> renderBreakStem(const BreakPattern& pat, int bars) {
    const double totalBeats = 4.0 * bars;
    const int totalFrames = static_cast<int>(std::llround((totalBeats * 60.0 / pat.bpm) * kSr));
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    StudioDrumKit kit;
    LushMpeSynth dub;
    dub.globalCutoffHz = 1150.0;
    dub.resonance = 2.6;
    dub.attackSec = 0.008;
    dub.releaseSec = 0.24;

    const double beatsPerSample = pat.bpm / 60.0 / kSr;
    double prevBeatInBar = -1.0;
    double subPhase = 0.0;
    constexpr int kSubNotes[4] = {29, 29, 32, 30}; // F1, F1, G#1, F#1

    for (int i = 0; i < totalFrames; ++i) {
        const double globalBeat = static_cast<double>(i) * beatsPerSample;
        const int bar = static_cast<int>(globalBeat / 4.0);
        const double beatInBar = globalBeat - 4.0 * bar;
        const bool wrapped = (beatInBar < prevBeatInBar);

        for (const auto& h : pat.hits) {
            const bool fire = wrapped
                ? (h.beat >= prevBeatInBar || h.beat <= beatInBar)
                : (h.beat > prevBeatInBar && h.beat <= beatInBar);
            if ((i == 0 && h.beat == 0.0) || fire)
                kit.trigger(h.note, h.vel);
        }

        if (pat.withDubStabs) {
            const double stabBeat = 0.75;
            const bool fireStab = wrapped ? false : (stabBeat > prevBeatInBar && stabBeat <= beatInBar);
            if (fireStab && (bar % 2 == 0)) {
                const int root = (bar == 0) ? 53 : 56; // F3 / G#3 minor dub chord
                dub.noteOn(1, root, 0.72);
                dub.noteOn(1, root + 3, 0.64);
                dub.noteOn(1, root + 7, 0.60);
                dub.noteOn(1, root + 10, 0.56);
            }
            const double offBeat = 1.25;
            if (!wrapped && offBeat > prevBeatInBar && offBeat <= beatInBar) {
                for (int n = 48; n <= 72; ++n) dub.noteOff(1, n);
            }
        }
        prevBeatInBar = beatInBar;

        float l = 0.0f, r = 0.0f;
        kit.tick(l, r, 1.0);

        if (pat.withSub) {
            const double subHz = midiToHz(kSubNotes[bar % 4]);
            subPhase += subHz / kSr;
            if (subPhase >= 1.0) subPhase -= 1.0;
            // Sidechain-pumped warm sub under the break
            const double kickDist = std::min(beatInBar, std::abs(beatInBar - 2.5));
            const double duck = std::clamp(kickDist * 2.8, 0.25, 1.0);
            const float sub = static_cast<float>(std::sin(kTwoPi * subPhase) * 0.24 * duck);
            l += sub;
            r += sub;
        }
        if (pat.withDubStabs) {
            float dl = 0.0f, dr = 0.0f;
            dub.tick(dl, dr, 0.34f);
            l += dl * 0.55f;
            r += dr * 0.55f;
        }

        out[static_cast<std::size_t>(i) * 2u] = l;
        out[static_cast<std::size_t>(i) * 2u + 1u] = r;
    }
    return out;
}

// ============================================================================
// Structured Note Sequence Helper for Tract Form & Torus Glide
// ============================================================================
struct NoteEvent {
    double startBeat;
    double durBeats;
    int note;
    float vel = 0.9f;
    float bendEndSt = 0.0f; // optional pitch slide during second half of note
};

struct ClipMeta {
    std::string product;
    std::string id;
    std::string title;
    std::string tag;
    double bpm;
    std::string key;
    std::string note;
    std::string wavPath;
};

std::vector<ClipMeta> gManifest;

void emitClip(const fs::path& outDir, const std::string& product, const std::string& id, const std::string& title,
              const std::string& tag, double bpm, const std::string& key, const std::string& note,
              std::vector<float> stereo, double targetRmsDb = -13.2) {
    masterClip(stereo, targetRmsDb, -1.2);
    const fs::path wavPath = outDir / product / (id + ".wav");
    if (!writeWavFloat32(wavPath, stereo, static_cast<int>(kSr)))
        throw std::runtime_error("Failed to write " + wavPath.string());
    gManifest.push_back({product, id, title, tag, bpm, key, note, wavPath.string()});
    std::cout << "  [" << product << "] " << id << " (" << (stereo.size() / 2.0 / kSr) << "s)\n";
}

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

// ============================================================================
// 2. TRACT FORM (6 clips: 5 composed riffs + 1 evolving cinema sub drone)
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

void renderTractFormClips(const fs::path& repoRoot, const fs::path& outDir) {
    const fs::path pDir = repoRoot / "products/tract-form/presets";

    // 1. Neuro Drop Riff (172 BPM, F Phrygian, syncopated 4-bar neurofunk bassline)
    {
        const std::vector<NoteEvent> riff = {
            {0.00, 1.40, 29, 0.98f, 0.0f},  {1.50, 0.90, 32, 0.92f, -2.0f},
            {2.50, 0.70, 30, 0.95f, 0.0f},  {3.25, 0.65, 29, 0.88f, 0.0f},
            {4.00, 1.40, 29, 1.00f, 3.0f},  {5.50, 0.85, 36, 0.95f, -4.0f},
            {6.50, 0.90, 30, 0.94f, -1.0f}, {7.50, 0.45, 29, 0.85f, 0.0f},
            {8.00, 1.40, 29, 0.98f, 0.0f},  {9.50, 0.70, 34, 0.92f, 0.0f},
            {10.25, 0.70, 32, 0.90f, 0.0f}, {11.00, 0.90, 30, 0.96f, -1.0f},
            {12.00, 1.65, 29, 1.00f, 0.0f}, {13.75, 0.70, 37, 0.95f, -5.0f},
            {14.50, 1.35, 30, 0.98f, -1.0f},
        };
        auto audio = renderTractSequence(pDir / "coupled-reese.json", 172.0, 4, riff,
            [](tf::Params& p, tf::Engine& e, double t) {
                p[tf::ParamId::tract] = lerpF(0.32f, 0.82f, 0.5 + 0.5 * std::sin(kTwoPi * t * 2.0));
                p[tf::ParamId::drive] = lerpF(0.30f, 0.58f, t);
                p[tf::ParamId::sculpt] = lerpF(3.5f, 9.0f, smoothStep(t));
                e.setModWheel(static_cast<float>(0.3 + 0.6 * t));
            });
        emitClip(outDir, "tract-form", "01-neuro-drop-riff", "Neuro Drop Riff", "172 DnB", 172.0, "F Phrygian",
                 "Coupled Reese playing a syncopated 4-bar F Phrygian neuro bassline with vowel tract & sculpt automation",
                 std::move(audio), -13.0);
    }

    // 2. Acid Snap Sequence (140 BPM, D Minor, funky 16th-note octave glides)
    {
        const std::vector<NoteEvent> acid = {
            {0.00, 0.45, 38, 0.95f, 0.0f},  {0.75, 0.40, 50, 0.88f, 0.0f},
            {1.25, 0.45, 41, 0.90f, 0.0f},  {1.75, 0.65, 45, 0.96f, -2.0f},
            {2.50, 0.40, 38, 0.88f, 0.0f},  {3.00, 0.45, 48, 0.92f, 2.0f},
            {3.50, 0.40, 45, 0.86f, 0.0f},  {4.00, 0.45, 38, 0.98f, 0.0f},
            {4.75, 0.40, 53, 0.94f, -3.0f}, {5.25, 0.45, 50, 0.90f, 0.0f},
            {5.75, 0.65, 45, 0.96f, 0.0f},  {6.50, 0.40, 41, 0.90f, 0.0f},
            {7.00, 0.85, 43, 0.96f, -5.0f}, {8.00, 0.45, 38, 1.00f, 0.0f},
            {8.75, 0.40, 50, 0.92f, 0.0f},  {9.25, 0.45, 46, 0.95f, -1.0f},
            {9.75, 0.65, 45, 0.94f, 0.0f},  {10.50, 0.45, 53, 0.98f, 0.0f},
            {11.00, 0.85, 50, 0.94f, -5.0f},{12.00, 0.45, 38, 1.00f, 0.0f},
            {12.75, 0.40, 50, 0.92f, 0.0f}, {13.25, 0.65, 41, 0.94f, 0.0f},
            {14.00, 0.65, 40, 0.95f, 0.0f}, {14.75, 1.15, 38, 1.00f, 0.0f},
        };
        auto audio = renderTractSequence(pDir / "acid-snap.json", 140.0, 4, acid,
            [](tf::Params& p, tf::Engine&, double t) {
                p[tf::ParamId::tractShift] = lerpF(-4.0f, 7.5f, t);
                p[tf::ParamId::drive] = lerpF(0.24f, 0.62f, t);
                p[tf::ParamId::morph] = lerpF(0.15f, 0.88f, 0.5 - 0.5 * std::cos(kTwoPi * t));
            });
        emitClip(outDir, "tract-form", "02-acid-snap-sequence", "Acid Snap Sequence", "Acid / Electro", 140.0, "D Minor",
                 "Funky 16th-note octave leaps with rising formant shift and ADAA2 wavefold bite",
                 std::move(audio), -13.2);
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: studio-showcase <repo-root> <out-wav-dir>\n";
        return 2;
    }
    const fs::path repoRoot = argv[1];
    const fs::path outDir = argv[2];
    fs::create_directories(outDir);

    try {
        std::cout << "Rendering Human Being Audio Studio Showcase (36 clips)...\n";
        renderLivecutClips(outDir);
        renderTractFormClips(repoRoot, outDir);
        renderPoleFieldClips(repoRoot, outDir);
        renderCutlineClips(repoRoot, outDir);
        renderTorusGlideClips(repoRoot, outDir);
        renderStrikeBodyClips(repoRoot, outDir);

        nlohmann::ordered_json arr = nlohmann::ordered_json::array();
        for (const auto& c : gManifest) {
            arr.push_back({
                {"product", c.product},
                {"id", c.id},
                {"title", c.title},
                {"tag", c.tag},
                {"bpm", c.bpm},
                {"key", c.key},
                {"note", c.note},
                {"wav", c.wavPath},
            });
        }
        std::ofstream mf(outDir / "raw-manifest.json");
        mf << arr.dump(2) << "\n";
        std::cout << "Rendered " << gManifest.size() << " clips -> " << outDir << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
