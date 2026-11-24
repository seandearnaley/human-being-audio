// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/chord_matrix.h"
#include "tg/params.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tg {

struct RenderRequest {
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    SceneMatrix matrix = matrixForPack(0);
    int note = 41; // F2 (root note played during bounce)
    double bpm = 172.0;
    double sampleRate = 48000.0;
    int oversampling = 8; // Studio/Ultra oversampled offline bounce
};

// Renders an exact bar-locked seamless stereo loop (interleaved L/R floats) matching `params.loopBars`.
// Pre-warms the engine by 1 full bar so Shepard partials, filters, and smoothers are steady-state at sample 0!
std::vector<float> renderOfflineLoop(const RenderRequest& request);

// Writes an IEEE-float 32-bit stereo WAV file cleanly (zero external dependencies).
bool writeWavFloat(const std::string& path, const std::vector<float>& interleavedStereo, int sampleRate);

// Exports a valid 960-PPQ Type-0 MPE Standard MIDI File (.mid) capturing all 5 polyphonic voices,
// Scale/JI Gravity bends, Pedal-Steel Stagger, and Möbius glides across `params.loopBars` bars!
std::vector<std::uint8_t> exportMpeSmf(const Params& params, const LaneCurves& curves, const SceneMatrix& matrix,
                                       int rootNote, double bpm);

} // namespace tg
