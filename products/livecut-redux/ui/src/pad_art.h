// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <visage/graphics.h>

namespace lc::ui {

struct ArtMotion {
    static constexpr float kBeatsBeforeWrap = 64.0f;

    float x = 0.5f;
    float y = 0.5f;
    float beat = 0.0f;
    float spin = 0.0f;
    float kick = 0.0f;
    int cuts = 0;
};

// Draws one of Livecut's 8 iconic procedural GPU cut-card artworks inside (x, y, width, height)
// clipped to that rectangle. `opacity` in [0, 1] supports smooth crossfading between programs.
void drawProgramArt(visage::Canvas& canvas, int program, float x, float y, float width, float height,
                    const ArtMotion& motion, float scale, float opacity = 1.0f);

// Draws the Livecut Redux diagonal-sliced wordmark inside (x, y, width, height).
void drawWordmark(visage::Canvas& canvas, float x, float y, float width, float height, float scale,
                  bool focused = false);

} // namespace lc::ui
