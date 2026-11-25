// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/engine.h"
#include "sb/params.h"

#include <string>
#include <vector>

namespace sb {

struct OneShotRequest {
    Params params = Params::defaults();
    Articulation articulation = Articulation::main;
    float velocity = 1.0f;
    float pitchRatio = 1.0f;
    double sampleRate = 48000.0;
    int oversampling = 8;
    bool normalizePeak = true;
};

// Renders a single peak-locked, zero-crossing-trimmed stereo one-shot hit (interleaved L/R).
std::vector<float> renderOneShot(const OneShotRequest& req);

// Writes 32-bit IEEE float stereo WAV.
bool writeWavFloat(const std::string& path, const std::vector<float>& interleavedStereo, int sampleRate);

// Builds a key & Hz tagged filename stem, e.g. "Neuro_Crack_F#3_185Hz".
std::string keyTaggedStem(const std::string& presetName, float bodyHz);

} // namespace sb
