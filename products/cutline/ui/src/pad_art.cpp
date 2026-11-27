// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pad_art.h"

#include "theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cl::ui {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

float mixf(float a, float b, float t) { return a + (b - a) * t; }
float frac(float v) { return v - std::floor(v); }

struct Hsv {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
};

Hsv rgbToHsv(unsigned argb) {
    const float r = static_cast<float>((argb >> 16) & 0xffu) / 255.0f;
    const float g = static_cast<float>((argb >> 8) & 0xffu) / 255.0f;
    const float b = static_cast<float>(argb & 0xffu) / 255.0f;
    const float mx = std::max(r, std::max(g, b));
    const float mn = std::min(r, std::min(g, b));
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 1e-6f) {
        if (mx == r)
            h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g)
            h = (b - r) / d + 2.0f;
        else
            h = (r - g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f)
            h += 1.0f;
    }
    return {h, mx > 1e-6f ? d / mx : 0.0f, mx};
}

unsigned hsvToArgb(float h, float s, float v, float alpha = 1.0f) {
    h = frac(h);
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (static_cast<int>(h * 6.0f) % 6) {
    case 0:
        r = c;
        g = x;
        break;
    case 1:
        r = x;
        g = c;
        break;
    case 2:
        g = c;
        b = x;
        break;
    case 3:
        g = x;
        b = c;
        break;
    case 4:
        r = x;
        b = c;
        break;
    default:
        r = c;
        b = x;
        break;
    }
    const auto byte = [](float f) { return static_cast<unsigned>(std::clamp(f * 255.0f + 0.5f, 0.0f, 255.0f)); };
    return (byte(alpha) << 24) | (byte(r + m) << 16) | (byte(g + m) << 8) | byte(b + m);
}

// Rich saturated HSV card toning (deep obsidian-jewel darks + luminous bright highlights)
struct Card {
    Card(unsigned argb, float op) : base(argb), hsv(rgbToHsv(argb)), opacity(op) {}

    unsigned tone(float amount, float alphaScale = 1.0f) const {
        const float a = std::clamp(opacity * alphaScale, 0.0f, 1.0f);
        if (amount >= 0.0f)
            return colour::withAlpha(colour::lerpArgb(base, 0xffffffffu, amount * 0.60f), a);
        const float rich = hsv.h < 0.2f ? 0.0f : (hsv.h < 5.0f / 6.0f ? 2.0f / 3.0f : 1.0f);
        const float depth = std::clamp(-amount, 0.0f, 1.0f);
        const float s = std::min(1.0f, hsv.s + (1.0f - hsv.s) * depth * 0.85f);
        const float v = hsv.v * mixf(1.0f, 0.11f, depth * depth * (3.0f - 2.0f * depth));
        return hsvToArgb(hsv.h + (rich - hsv.h) * 0.45f * depth, s, v, a);
    }

    unsigned shadow(float alpha = 0.34f) const { return colour::withAlpha(0xff000000u, alpha * opacity); }

    unsigned base = 0xff936effu;
    Hsv hsv{};
    float opacity = 1.0f;
};

// 1: Roll — Exponentially compressing vertical cadence shockwaves & escalating velocity terraces
void drawRoll(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
              float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const int bars = 8 + static_cast<int>(std::lround(m.x * 28.0f));
    const float barW = w / static_cast<float>(bars);
    const float pulseWave = frac(m.beat * (1.0f + 3.0f * m.x));

    for (int i = 0; i < bars; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(bars - 1);
        const float distWave = std::abs(u - pulseWave);
        const float crest = std::exp(-distWave * distWave * 18.0f);
        const float rise = std::pow(u, 1.35f) * (0.28f + 0.72f * m.y);
        const float bh =
            std::max(14.0f * scale, h * std::clamp(rise + 0.24f * crest + 0.16f * m.impulse, 0.08f, 0.96f));
        const float bx = x + static_cast<float>(i) * barW + 1.8f * scale;
        const float bw = std::max(3.0f * scale, barW - 3.6f * scale);
        const float by = y + h - bh;

        canvas.setColor(card.tone(-0.45f + 0.55f * u + 0.35f * crest + 0.25f * m.y));
        canvas.roundedRectangle(bx, by, bw, bh, 3.0f * scale);

        // Hot incandescent crest tip
        canvas.setColor(card.tone(0.45f + 0.45f * crest, 0.90f));
        canvas.roundedRectangle(bx, by, bw, std::min(bh, 6.0f * scale), 2.0f * scale);
    }

    // Subtle rising tension arc sweeping across the top
    const float lineY = y + h * (1.0f - m.y);
    canvas.setColor(card.tone(0.55f, 0.28f));
    canvas.rectangle(x, lineY, w, 2.0f * scale);
}

// 0: Repeat — Fractured echoing mirror prism tiles subdividing dynamically with X
void drawStutter(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
                 float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const int cols = m.x < 0.18f ? 2 : (m.x < 0.42f ? 4 : (m.x < 0.68f ? 8 : (m.x < 0.88f ? 12 : 16)));
    const int rows = 4;
    const float cellW = w / static_cast<float>(cols);
    const float cellH = h / static_cast<float>(rows);
    const float pad = (3.0f + 6.0f * (1.0f - m.y)) * scale;
    const int activeCol = static_cast<int>(std::floor(frac(m.beat * (1.0f + 3.0f * m.x)) * static_cast<float>(cols)));

    for (int c = 0; c < cols; ++c) {
        const bool isHot = (c == activeCol);
        for (int r = 0; r < rows; ++r) {
            const float cx = x + static_cast<float>(c) * cellW + pad * 0.5f;
            const float cy = y + static_cast<float>(r) * cellH + pad * 0.5f;
            const float cw = std::max(4.0f * scale, cellW - pad);
            const float ch = std::max(8.0f * scale, cellH - pad);
            const float flash = m.laneFlash[static_cast<std::size_t>((r * 2) % kDrumLaneCount)];
            const float gateFrac = 0.25f + 0.75f * m.y;

            canvas.setColor(card.tone(isHot ? 0.32f : (-0.52f + 0.35f * flash)));
            canvas.roundedRectangle(cx, cy, cw * gateFrac, ch, 4.0f * scale);
            canvas.setColor(card.tone(isHot ? 0.75f : 0.10f, 0.75f));
            canvas.roundedRectangleBorder(cx, cy, cw, ch, 4.0f * scale, 1.2f * scale);
        }
    }
}

// 2: Rate — Dual-horizon Sub-Bass Monoliths vs Top-End Crystalline Lattice with Time-Warp tilt
void drawMorph(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
               float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const float horizonY = y + h * (0.18f + 0.64f * (1.0f - m.y));
    const int cols = m.x < 0.33f ? 8 : (m.x > 0.67f ? 28 : 16);
    const float colW = w / static_cast<float>(cols);

    // Lower sub-bass monoliths (stronger when Y is low!)
    const float subWeight = std::clamp(1.25f - m.y, 0.15f, 1.0f);
    for (int i = 0; i < cols; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(cols);
        const float wave = 0.45f + 0.55f * std::sin(kTwoPi * (u * 2.0f - m.beat * 0.25f));
        const float bh = (y + h - horizonY) * (0.28f + 0.68f * wave * subWeight);
        canvas.setColor(card.tone(-0.32f + 0.42f * subWeight + 0.25f * m.laneFlash[0]));
        canvas.roundedRectangle(x + static_cast<float>(i) * colW + 2.0f * scale, y + h - bh,
                                std::max(3.0f * scale, colW - 4.0f * scale), bh, 3.0f * scale);
    }

    // Upper crystalline hi-hat shimmer bars (stronger when Y is high!)
    const float topWeight = std::clamp(m.y + 0.25f, 0.15f, 1.0f);
    const int topCols = cols * 2;
    const float topW = w / static_cast<float>(topCols);
    for (int i = 0; i < topCols; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(topCols);
        const float wave = 0.35f + 0.65f * std::abs(std::cos(kTwoPi * (u * 3.0f + m.beat * 0.5f)));
        const float th = (horizonY - y) * (0.20f + 0.72f * wave * topWeight);
        canvas.setColor(card.tone(0.18f + 0.45f * topWeight + 0.25f * m.laneFlash[4], 0.85f));
        canvas.roundedRectangle(x + static_cast<float>(i) * topW + 1.5f * scale, y,
                                std::max(2.0f * scale, topW - 3.0f * scale), th, 2.0f * scale);
    }

    // Glowing horizon divider line
    canvas.setColor(card.tone(0.68f, 0.85f));
    canvas.rectangle(x, horizonY - 1.5f * scale, w, 3.0f * scale);
}

// 3: Drop — Dramatic converging tension chevrons & countdown aperture into beat 1!
void drawDrop(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
              float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const float cx = x + w * (0.20f + 0.60f * m.x);
    const float cy = y + h * (0.80f - 0.60f * m.y);
    const int rings = 9;
    const float phase = frac(m.beat * (0.5f + 1.5f * m.x));

    for (int i = 0; i < rings; ++i) {
        const float t = frac(static_cast<float>(i) / static_cast<float>(rings) + phase);
        const float inv = 1.0f - t;
        const float rw = w * (0.08f + 0.92f * inv);
        const float rh = h * (0.08f + 0.92f * inv);
        const float rx = cx - rw * 0.5f;
        const float ry = cy - rh * 0.5f;

        canvas.setColor(card.tone(-0.40f + 0.85f * t * (0.4f + 0.6f * m.y), 0.16f + 0.65f * t));
        canvas.roundedRectangleBorder(rx, ry, rw, rh, (10.0f + 24.0f * inv) * scale, (1.6f + 3.0f * t * m.y) * scale);
    }

    // Glowing converging focal beacon
    const float coreR = (16.0f + 26.0f * m.y + 12.0f * m.impulse) * scale;
    canvas.setColor(card.tone(0.45f, 0.28f));
    canvas.circle(cx - coreR * 1.8f, cy - coreR * 1.8f, coreR * 3.6f);
    canvas.setColor(card.tone(0.70f, 0.90f));
    canvas.circle(cx - coreR * 0.55f, cy - coreR * 0.55f, coreR * 1.1f);
}

} // namespace

void drawCutlinePadArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, float opacity,
                       const PadMotion& m, float scale) {
    if (opacity <= 0.005f || w <= 2.0f || h <= 2.0f)
        return;
    const Card card(colour::programArgb(program), opacity);
    switch (std::clamp(program, 0, kPerfPadCount - 1)) {
    case 0:
        drawStutter(canvas, x, y, w, h, card, m, scale); // Repeat
        break;
    case 1:
        drawRoll(canvas, x, y, w, h, card, m, scale);
        break;
    case 2:
        drawMorph(canvas, x, y, w, h, card, m, scale); // Rate
        break;
    default:
        drawDrop(canvas, x, y, w, h, card, m, scale);
        break;
    }
}

} // namespace cl::ui
