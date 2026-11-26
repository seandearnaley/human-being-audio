// SPDX-License-Identifier: GPL-3.0-or-later
#include "showcase_common.h"

#include <hba/core/wav.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace hba::showcase {

std::string readTextFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Write 48 kHz 32-bit IEEE float stereo WAV via shared spec-compliant hba::core::writeWavFloat
bool writeWavFloat32(const fs::path& path, const std::vector<float>& stereo, int sampleRate) {
    fs::create_directories(path.parent_path());
    return hba::core::writeWavFloat(path.string(), stereo, 2, sampleRate);
}

// Gentle studio polish: DC block, RMS-aware target normalization, soft safety ceiling (-1.2 dBFS), clean fades
void masterClip(std::vector<float>& stereo, double targetRmsDb, double peakCeilDb) {
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


StudioRoom::StudioRoom() {
    for (int i = 0; i < 4; ++i) {
        dL[i].assign(static_cast<std::size_t>(kTapL[i]), 0.0f);
        dR[i].assign(static_cast<std::size_t>(kTapR[i]), 0.0f);
    }
}

void StudioRoom::process(float& l, float& r, float wet, float fb) {
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

double StudioDrumKit::randNoise() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<double>(rng) / 2147483648.0 - 1.0;
}

void StudioDrumKit::trigger(int midiNote, double vel01) {
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

void StudioDrumKit::tick(float& outL, float& outR, double subWeight) {
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

void LushMpeSynth::noteOn(int ch, int note, double vel) {
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

void LushMpeSynth::noteOff(int ch, int note) {
        for (auto& v : voices) {
            if (v.active && v.gate && v.note == note && (ch <= 1 || v.channel == ch))
                v.gate = false;
        }
    }

void LushMpeSynth::pitchBend(int ch, double semitones) {
        for (auto& v : voices) {
            if (v.active && (ch <= 1 || v.channel == ch)) v.bendSt = semitones;
        }
    }

void LushMpeSynth::slideCc74(int ch, double norm01) {
        for (auto& v : voices) {
            if (v.active && (ch <= 1 || v.channel == ch)) v.cc74 = norm01;
        }
    }

void LushMpeSynth::tick(float& outL, float& outR, float roomWet) {
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

BreakPattern amenBreak(double bpm) {
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

BreakPattern halftimeBreak(double bpm) {
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

BreakPattern stepperBreak(double bpm) {
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

std::vector<ClipMeta>& manifest() {
    static std::vector<ClipMeta> gManifest;
    return gManifest;
}

void emitClip(const fs::path& outDir, const std::string& product, const std::string& id, const std::string& title,
              const std::string& tag, double bpm, const std::string& key, const std::string& note,
              std::vector<float> stereo, double targetRmsDb) {
    masterClip(stereo, targetRmsDb, -1.2);
    const fs::path wavPath = outDir / product / (id + ".wav");
    if (!writeWavFloat32(wavPath, stereo, static_cast<int>(kSr)))
        throw std::runtime_error("Failed to write " + wavPath.string());
    manifest().push_back({product, id, title, tag, bpm, key, note, wavPath.string()});
    std::cout << "  [" << product << "] " << id << " (" << (stereo.size() / 2.0 / kSr) << "s)\n";
}

} // namespace hba::showcase
