// SPDX-License-Identifier: AGPL-3.0-or-later
// Bespoke 60fps generative interactive artwork for the four interim pad programs.
#pragma once

#include "cl/pattern.h"

#include <visage_graphics/canvas.h>

#include <array>

namespace cl::ui {

struct PadMotion {
    float x = 0.5f;
    float y = 0.5f;
    float beat = 0.0f;
    float impulse = 0.0f; // Hit impact flash (0..1)
    float engage = 0.0f;  // Smooth 0..1 engaged intensity
    std::array<float, kDrumLaneCount> laneFlash{};
    bool isSlicer = false;
    int heldCount = 0;
};

void drawCutlinePadArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, float opacity,
                       const PadMotion& m, float scale);

} // namespace cl::ui
