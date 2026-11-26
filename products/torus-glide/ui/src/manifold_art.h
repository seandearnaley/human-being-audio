// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/params.h"

#include <visage/graphics.h>

#include <array>

namespace tg::ui {

struct ManifoldMotion {
    float x = 0.5f;
    float y = 0.5f;
    float phase = 0.0f;     // smooth unwrapped animation phase
    float spiralPos = 0.0f; // live audio spiral position
    float morphPos = 0.42f; // live 0..1 morph
    float barPhase = 0.0f;  // 0..1 bar loop phase
    float gateLevel = 1.0f; // live Risset pulse gate envelope
    float rms = 0.35f;      // smoothed audio RMS
    float subPeak = 0.50f;  // smoothed sub-anchor level
    float padAmt = 0.0f;    // smoothed X/Y engagement 0..1
    float spiralVel = 1.0f; // effective spiral speed (-2..+3)
    std::array<float, kOctaveCount> partialU{};
    std::array<float, kOctaveCount> partialGain{};
    std::array<float, kVoiceCount> voicePitches{41.0f, 48.0f, 54.0f, 56.0f, 63.0f};
};

void drawManifoldArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, const ManifoldMotion& m,
                     float scale, float alpha);

} // namespace tg::ui
