// Shared helpers for the engine tests: driving the engine with note events at
// exact frames, random patches, and a few signal measurements.
#pragma once

#include "analysis/analysis.h"
#include "tf/engine.h"
#include "tf/lanes.h"
#include "tf/params.h"

#include <cstdint>
#include <random>
#include <vector>

namespace tf::test {

using analysis::Signal;

struct NoteEvent {
    long long frame = 0;
    int note = 29;
    bool on = true;
    float bend = 0.0f;  // applied with the event when `bendOnly` is set
    bool bendOnly = false;
};

struct Setup {
    Params params = Params::defaults();
    double sampleRate = 48000.0;
    int oversampling = 4;
    int block = 64;
    double bpm = 174.0;
    bool playing = true;
    double ppqStart = 0.0;
    float bend = 0.0f;
    LaneCurves curves = defaultLaneCurves();  // given to the engine before the first block
};

struct Rendered {
    Signal left, right, master, growl;
    Signal growlPre;  // the growl before its clipper (Taps::growlPreClip): not time-aligned
    int latency = 0;
    std::vector<std::array<float, kLaneCount>> laneValues;  // after each block
    std::vector<float> hz;                                  // after each block
    std::vector<float> limiterGr, growlClip;                // after each block, dB
};

// Renders `frames` frames; events take effect at their frame (blocks are split there).
Rendered render(const Setup& setup, const std::vector<NoteEvent>& events, long long frames);
// Same, on an engine the caller already prepared (state carries over).
Rendered render(Engine& engine, const Setup& setup, const std::vector<NoteEvent>& events, long long frames,
                long long frameOffset = 0);

// Default patch with every lane off (no movement).
Params staticPatch();

// A dense, hard-driven patch: coupling, drive and warp at 1, growl 1, feedback 0.6, squash 0.6.
Params hotPatch();

// Uniformly random value for every parameter.
Params randomParams(std::mt19937& rng);

// A random drawn curve: 1-8 points, random positions, values, tensions and segments.
Curve randomCurve(std::mt19937& rng);

// Hash (FNV-1a, 64 bit) of the float bits of the left and right outputs.
std::uint64_t hashOutput(const Rendered& r);

double maxAbs(const Signal& x);
bool allFinite(const Signal& x);
double maxAbsDiff(const Signal& a, const Signal& b);
Signal slice(const Signal& x, std::size_t begin, std::size_t end);
Signal mid(const Rendered& r);
Signal side(const Rendered& r);

double noteHz(double note);
// The engine's sub/growl split for `note`, in Hz: subSplit x f0 (tf::subSplitHz).
double splitHz(const Params& params, int note);
long long frames(double seconds, double sampleRate);

}
