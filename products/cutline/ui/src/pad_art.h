// SPDX-License-Identifier: AGPL-3.0-or-later
// The PLAY surface: the current bar of notes as the engine actually sent them, each a mark at its real time and
// velocity that fades over one bar, over faint outlines of the resolved pattern. Whatever a program does, the
// surface shows it, because it draws what was played.
#pragma once

#include "cl/phrase.h"
#include "cl/resolve.h"
#include "sent_history.h"

#include <visage_graphics/canvas.h>

#include <array>
#include <span>

namespace cl::ui {

// What the surface draws, in panel pixels.
struct SurfaceScene {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float scale = 1.0f;
    unsigned accent = 0xffffffffu; // the program's colour
    bool drum = true;

    // Drums: the bar being played, resolved as the engine resolves it, and what the pads do to its lanes.
    const ResolvedBar* outline = nullptr;
    int fillFromStep = -1;                    // the live fill window's first step, or -1
    std::array<float, kDrumLaneCount> veil{}; // 0..1: how much a pad is keeping a lane quiet
    std::array<float, kDrumLaneCount> glow{}; // 0..1: how much a pad is playing a lane itself (Roll)
    float repeatFromBeat = -1.0f;             // the Repeat window inside the bar, in beats; < 0 when none
    float repeatBeats = 0.0f;

    // MIDI Slicer: one row per pitch, top to bottom, and the phrase's notes in this bar as outlines.
    std::span<const int> pitches{};
    const SlicerPlan* plan = nullptr;
    double planBarStart = 0.0; // the phrase beat where this bar starts

    float playhead = -1.0f; // beat in the bar, 0..4; < 0 when stopped
    const SentHistory* history = nullptr;
    double nowPpq = 0.0;
};

void drawPlaySurface(visage::Canvas& canvas, const SurfaceScene& scene);

} // namespace cl::ui
