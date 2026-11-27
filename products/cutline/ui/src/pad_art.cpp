// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pad_art.h"

#include "controls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace cl::ui {

namespace {

constexpr unsigned kSurfaceBase = 0xff0a0d12u;
constexpr unsigned kVeil = 0xff05070au;

// A mark's or outline's height in its row: louder is taller, and both stand on the row's floor.
float barHeight(float rowH, int velocity, float scale) {
    const float v = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
    return std::max(4.0f * scale, rowH * (0.26f + 0.60f * v));
}

// A small solid tag naming a window (FILL, LOOP), legible over whatever marks are under it.
void drawTag(visage::Canvas& canvas, float scale, const char* text, float x, float y, unsigned fill) {
    const float w = 38.0f * scale;
    const float h = 14.0f * scale;
    canvas.setColor(fill);
    canvas.roundedRectangle(x, y, w, h, 3.0f * scale);
    hba::ui::drawLabel(canvas, scale, text, x, y, w, h, colour::onColour, visage::Font::kCenter, 8.5f,
                       Weight::semibold);
}

struct Placed {
    float x = 0.0f;
    float w = 0.0f;
    int row = 0;
    float life = 0.0f;
    int velocity = 0;
    unsigned hue = 0;
};

} // namespace

void drawPlaySurface(visage::Canvas& canvas, const SurfaceScene& s) {
    if (s.w <= 4.0f || s.h <= 4.0f)
        return;
    const float px = s.scale;
    const int rows = s.drum ? kDrumLaneCount : std::max(1, static_cast<int>(s.pitches.size()));
    const float rowH = s.h / static_cast<float>(rows);
    const float stepW = s.w / static_cast<float>(kStepsPerBar);
    const float inset = std::min(5.0f * px, rowH * 0.12f);
    const float markW = std::clamp(stepW * 0.40f, 3.0f * px, 30.0f * px);
    const auto beatX = [&](double beat) { return s.x + static_cast<float>(beat / 4.0) * s.w; };
    const auto rowTop = [&](int row) { return s.y + static_cast<float>(row) * rowH; };
    const auto rowOfPitch = [&](int note) {
        for (std::size_t i = 0; i < s.pitches.size(); ++i)
            if (s.pitches[i] == note)
                return static_cast<int>(i);
        return -1;
    };

    // 1. The well, banded by row.
    canvas.setColor(colour::lerpArgb(kSurfaceBase, s.accent, 0.07f));
    canvas.rectangle(s.x, s.y, s.w, s.h);
    for (int row = 1; row < rows; row += 2) {
        canvas.setColor(0x07ffffffu);
        canvas.rectangle(s.x, rowTop(row), s.w, rowH);
    }

    // 2. Where a pad is playing a lane itself (Roll), and the windows that are live: the fill, the Repeat loop.
    if (s.drum) {
        for (int lane = 0; lane < kDrumLaneCount; ++lane) {
            const float g = s.glow[static_cast<std::size_t>(lane)];
            if (g <= 0.0f)
                continue;
            canvas.setColor(colour::withAlpha(s.accent, 0.16f * g));
            canvas.rectangle(s.x, rowTop(surfaceRowOfLane(lane)), s.w, rowH);
        }
    }
    if (s.fillFromStep >= 0 && s.fillFromStep < kStepsPerBar) {
        const float fx = s.x + static_cast<float>(s.fillFromStep) * stepW;
        canvas.setColor(colour::withAlpha(colour::amber, 0.09f));
        canvas.rectangle(fx, s.y, s.x + s.w - fx, s.h);
        canvas.setColor(colour::withAlpha(colour::amber, 0.85f));
        canvas.rectangle(fx, s.y, s.x + s.w - fx, 2.0f * px);
    }
    if (s.repeatFromBeat >= 0.0f && s.repeatBeats > 0.0f) {
        const float rx = beatX(s.repeatFromBeat);
        const float rw = std::min(beatX(s.repeatFromBeat + s.repeatBeats), s.x + s.w) - rx;
        canvas.setColor(colour::withAlpha(s.accent, 0.12f));
        canvas.rectangle(rx, s.y, rw, s.h);
        canvas.setColor(colour::withAlpha(s.accent, 0.9f));
        canvas.rectangle(rx, s.y, 2.0f * px, s.h);
        canvas.rectangle(rx + rw - 2.0f * px, s.y, 2.0f * px, s.h);
        canvas.rectangle(rx, s.y + s.h - 2.0f * px, rw, 2.0f * px);
    }

    // 3. The grid: faint 16ths, firmer beats.
    for (int step = 1; step < kStepsPerBar; ++step) {
        const bool beat = step % 4 == 0;
        canvas.setColor(beat ? 0x24ffffffu : 0x0bffffffu);
        canvas.rectangle(s.x + static_cast<float>(step) * stepW - 0.5f * px, s.y, (beat ? 1.2f : 1.0f) * px, s.h);
    }

    // 4. Faint outlines of what the bar resolves to: the pattern as written, at the velocity it will be sent.
    if (s.drum && s.outline != nullptr) {
        for (int lane = 0; lane < kDrumLaneCount; ++lane) {
            const int row = surfaceRowOfLane(lane);
            for (int step = 0; step < kStepsPerBar; ++step) {
                const ResolvedStep& r =
                    s.outline->lanes[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)];
                if (r.velocity == 0)
                    continue;
                const unsigned hue = r.fromFill ? colour::amber : colour::lane[static_cast<std::size_t>(lane)];
                const float oh = barHeight(rowH, r.velocity, px);
                const float ox = s.x + static_cast<float>(step) * stepW + 1.0f * px;
                const float oy = rowTop(row) + rowH - inset - oh;
                canvas.setColor(colour::withAlpha(hue, 0.07f));
                canvas.roundedRectangle(ox, oy, markW, oh, 3.0f * px);
                canvas.setColor(colour::withAlpha(hue, 0.34f));
                canvas.roundedRectangleBorder(ox, oy, markW, oh, 3.0f * px, 1.2f * px);
            }
        }
    } else if (!s.drum && s.plan != nullptr) {
        for (int i = 0; i < s.plan->noteCount; ++i) {
            const SlicerNote& n = s.plan->notes[static_cast<std::size_t>(i)];
            const double at = n.beat - s.planBarStart;
            const int row = rowOfPitch(n.note);
            if (at < 0.0 || at >= 4.0 || row < 0)
                continue;
            const float ox = beatX(at) + 1.0f * px;
            const float ow = std::max(markW, static_cast<float>(n.durBeats / 4.0) * s.w - 2.0f * px);
            const float oh = barHeight(rowH, n.velocity, px);
            const float oy = rowTop(row) + rowH - inset - oh;
            canvas.setColor(colour::withAlpha(colour::cyan, 0.06f));
            canvas.roundedRectangle(ox, oy, ow, oh, 3.0f * px);
            canvas.setColor(colour::withAlpha(colour::cyan, 0.30f));
            canvas.roundedRectangleBorder(ox, oy, ow, oh, 3.0f * px, 1.2f * px);
        }
    }

    // 5. The notes that were sent, at their real time and velocity, fading over one bar. Marks that crowd each
    // other in a row (ratchets, fast rolls) narrow so each hit stays visible.
    if (s.history != nullptr) {
        std::array<Placed, SentHistory::kCapacity> placed{};
        int count = 0;
        s.history->forEachLive(s.nowPpq, [&](const SentNote& m, float life) {
            const int row = s.drum ? (m.lane >= 0 ? surfaceRowOfLane(m.lane) : -1) : rowOfPitch(m.note);
            if (row < 0 || count >= static_cast<int>(placed.size()))
                return;
            Placed& p = placed[static_cast<std::size_t>(count++)];
            p.x = beatX(static_cast<double>(m.beatInBar)) + 1.0f * px;
            p.w = markW;
            p.row = row;
            p.life = life;
            p.velocity = m.velocity;
            p.hue = s.drum ? colour::lane[static_cast<std::size_t>(std::clamp(static_cast<int>(m.lane), 0, 7))]
                           : colour::cyan;
        });
        std::sort(placed.begin(), placed.begin() + count,
                  [](const Placed& a, const Placed& b) { return a.row != b.row ? a.row < b.row : a.x < b.x; });
        for (int i = 0; i + 1 < count; ++i) {
            Placed& a = placed[static_cast<std::size_t>(i)];
            const Placed& b = placed[static_cast<std::size_t>(i + 1)];
            if (a.row == b.row && b.x - a.x < a.w + 2.0f * px)
                a.w = std::max(2.0f * px, b.x - a.x - 2.0f * px);
        }
        // Oldest first, so the newest hit of a crowded row is on top.
        std::sort(placed.begin(), placed.begin() + count,
                  [](const Placed& a, const Placed& b) { return a.life < b.life; });
        for (int i = 0; i < count; ++i) {
            const Placed& p = placed[static_cast<std::size_t>(i)];
            const float mh = barHeight(rowH, p.velocity, px);
            const float my = rowTop(p.row) + rowH - inset - mh;
            const float alpha = std::pow(std::clamp(p.life, 0.0f, 1.0f), 1.25f);
            const float fresh = std::clamp((p.life - 0.82f) / 0.18f, 0.0f, 1.0f);
            if (fresh > 0.0f) {
                canvas.setColor(colour::withAlpha(p.hue, 0.32f * fresh));
                canvas.roundedRectangle(p.x - 3.0f * px, my - 3.0f * px, p.w + 6.0f * px, mh + 6.0f * px, 5.0f * px);
            }
            canvas.setColor(colour::withAlpha(colour::lerpArgb(p.hue, 0xffffffffu, 0.35f * fresh), alpha));
            canvas.roundedRectangle(p.x, my, p.w, mh, 3.0f * px);
            canvas.setColor(colour::withAlpha(0xffffffffu, 0.20f * alpha + 0.65f * fresh));
            canvas.roundedRectangle(p.x, my, p.w, std::min(mh, 3.0f * px), 1.5f * px);
        }
    }

    // 6. Lanes a pad is keeping quiet (Drop's zones, Rate's kit, the lane Roll plays over) go dark.
    if (s.drum) {
        for (int lane = 0; lane < kDrumLaneCount; ++lane) {
            const float v = s.veil[static_cast<std::size_t>(lane)];
            if (v <= 0.0f)
                continue;
            canvas.setColor(colour::withAlpha(kVeil, 0.70f * v));
            canvas.rectangle(s.x, rowTop(surfaceRowOfLane(lane)), s.w, rowH);
        }
    }

    // 7. The windows' names, over the marks.
    if (s.fillFromStep >= 0 && s.fillFromStep < kStepsPerBar)
        drawTag(canvas, px, "FILL", s.x + static_cast<float>(s.fillFromStep) * stepW + 4.0f * px, s.y + 4.0f * px,
                colour::amber);
    if (s.repeatFromBeat >= 0.0f && s.repeatBeats > 0.0f)
        drawTag(canvas, px, "LOOP", beatX(s.repeatFromBeat) + 4.0f * px, s.y + s.h - 18.0f * px, s.accent);

    // 8. The playhead.
    if (s.playhead >= 0.0f) {
        const float hx = beatX(static_cast<double>(std::clamp(s.playhead, 0.0f, 4.0f)));
        canvas.setColor(colour::withAlpha(s.accent, 0.16f));
        canvas.rectangle(hx - 7.0f * px, s.y, 7.0f * px, s.h);
        canvas.setColor(colour::withAlpha(0xffffffffu, 0.92f));
        canvas.rectangle(hx - 1.0f * px, s.y, 2.0f * px, s.h);
        canvas.triangle(hx - 5.0f * px, s.y, hx + 5.0f * px, s.y, hx, s.y + 6.0f * px);
    }
}

} // namespace cl::ui
