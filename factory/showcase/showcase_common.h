// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hba/dsp/pitch.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tf {
class Engine;
struct Params;
} // namespace tf

namespace hba::showcase {

namespace fs = std::filesystem;

inline constexpr double kSr = 48000.0;
inline constexpr int kBlock = 64;
inline constexpr double kTwoPi = 6.28318530717959;

inline double midiToHz(double note) { return hba::dsp::midiToHz(note); }

inline float lerpF(float a, float b, double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return a + static_cast<float>(c) * (b - a);
}

inline double smoothStep(double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

std::string readTextFile(const fs::path& p);
bool writeWavFloat32(const fs::path& path, const std::vector<float>& stereo, int sampleRate);
void masterClip(std::vector<float>& stereo, double targetRmsDb = -13.5, double peakCeilDb = -1.2);

struct StudioRoom {
    static constexpr int kTapL[4] = {1087, 1601, 2293, 3041};
    static constexpr int kTapR[4] = {1153, 1699, 2411, 3169};
    std::array<std::vector<float>, 4> dL, dR;
    std::array<int, 4> idxL{}, idxR{};
    float lpL = 0.0f, lpR = 0.0f;

    StudioRoom();
    void process(float& l, float& r, float wet = 0.14f, float fb = 0.42f);
};

struct StudioDrumKit {
    std::uint32_t rng = 0x51a7c3d1u;
    double kickEnv = 0.0, kickPitch = 0.0, kickPhase = 0.0, kickClick = 0.0;
    double snareEnv = 0.0, snareBodyEnv = 0.0, snarePhase = 0.0, snareTone = 212.0;
    double ghostEnv = 0.0, ghostPhase = 0.0;
    double hatEnv = 0.0, hatOpen = 0.0, hatDecay = 0.04;
    double rideEnv = 0.0, percEnv = 0.0, percPhase = 0.0, percFreq = 135.0;
    double hatPh[4]{};
    hba::dsp::SvfState snBp{}, hatHp{}, percBp{};
    StudioRoom room;

    double randNoise();
    void trigger(int midiNote, double vel01);
    void tick(float& outL, float& outR, double subWeight = 1.0);
};

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

    void noteOn(int ch, int note, double vel);
    void noteOff(int ch, int note);
    void pitchBend(int ch, double semitones);
    void slideCc74(int ch, double norm01);
    void tick(float& outL, float& outR, float roomWet = 0.28f);
};

struct BreakPattern {
    struct Hit { double beat; int note; double vel; };
    std::vector<Hit> hits;
    double bpm = 172.0;
    bool withSub = false;
    bool withDubStabs = false;
};

BreakPattern amenBreak(double bpm = 172.0);
BreakPattern halftimeBreak(double bpm = 140.0);
BreakPattern stepperBreak(double bpm = 172.0);
std::vector<float> renderBreakStem(const BreakPattern& pat, int bars);

struct NoteEvent {
    double startBeat;
    double durBeats;
    int note;
    float vel = 0.9f;
    float bendEndSt = 0.0f;
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

std::vector<ClipMeta>& manifest();

void emitClip(const fs::path& outDir, const std::string& product, const std::string& id, const std::string& title,
              const std::string& tag, double bpm, const std::string& key, const std::string& note,
              std::vector<float> stereo, double targetRmsDb = -13.2);

std::vector<float> renderTractSequence(const fs::path& presetPath, double bpm, int bars,
                                       const std::vector<NoteEvent>& notes,
                                       std::function<void(tf::Params&, tf::Engine&, double prog)> automate);

void renderLivecutClips(const fs::path& outDir);
void renderTractFormClips(const fs::path& repoRoot, const fs::path& outDir);
void renderPoleFieldClips(const fs::path& repoRoot, const fs::path& outDir);
void renderCutlineClips(const fs::path& repoRoot, const fs::path& outDir);
void renderTorusGlideClips(const fs::path& repoRoot, const fs::path& outDir);
void renderStrikeBodyClips(const fs::path& repoRoot, const fs::path& outDir);

} // namespace hba::showcase
