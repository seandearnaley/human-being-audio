// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad_art.h"

#include "theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace lc::ui {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float mixf(float a, float b, float t) { return a + (b - a) * t; }
float ease(float t) { return t * t * (3.0f - 2.0f * t); }
float frac(float value) { return value - std::floor(value); }
float turn(float beat, float beats) { return frac(beat / beats); }
float sway(float beat, float beats, float offset = 0.0f) { return std::sin(2.0f * kPi * turn(beat, beats) + offset); }
float pulse(float beat) { return std::pow(1.0f - frac(beat), 3.0f); }

float chance(int a, int b, int c = 0) {
    auto h = static_cast<std::uint32_t>(a) * 374761393u + static_cast<std::uint32_t>(b) * 668265263u +
             static_cast<std::uint32_t>(c) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffffu) / 16777216.0f;
}

struct Hsv {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
};

Hsv rgbToHsv(unsigned argb) {
    const float r = static_cast<float>((argb >> 16) & 0xffu) / 255.0f;
    const float g = static_cast<float>((argb >> 8) & 0xffu) / 255.0f;
    const float b = static_cast<float>(argb & 0xffu) / 255.0f;
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 1.0e-6f) {
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
    const float s = mx > 1.0e-6f ? d / mx : 0.0f;
    return {h, s, mx};
}

unsigned hsvToArgb(float h, float s, float v, float alpha = 1.0f) {
    h = frac(h);
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    const int sector = static_cast<int>(h * 6.0f) % 6;
    switch (sector) {
    case 0:
        r = c;
        g = x;
        b = 0.0f;
        break;
    case 1:
        r = x;
        g = c;
        b = 0.0f;
        break;
    case 2:
        r = 0.0f;
        g = c;
        b = x;
        break;
    case 3:
        r = 0.0f;
        g = x;
        b = c;
        break;
    case 4:
        r = x;
        g = 0.0f;
        b = c;
        break;
    default:
        r = c;
        g = 0.0f;
        b = x;
        break;
    }
    const auto byte = [](float f) { return static_cast<unsigned>(std::clamp(f * 255.0f + 0.5f, 0.0f, 255.0f)); };
    return (byte(alpha) << 24) | (byte(r + m) << 16) | (byte(g + m) << 8) | byte(b + m);
}

// Faithful port of Livecut's rich HSV card toning:
// darker warm shades lean ember-red, darker cool shades lean deep-teal/blue, never muddy brown.
struct Card {
    explicit Card(unsigned argb, float op) : base(argb), hsv(rgbToHsv(argb)), opacity(op) {}

    unsigned tone(float amount, float alphaScale = 1.0f) const {
        const float a = std::clamp(opacity * alphaScale, 0.0f, 1.0f);
        if (amount >= 0.0f) {
            const unsigned mixed = colour::lerpArgb(base, 0xffffffffu, amount * 0.55f);
            return colour::withAlpha(mixed, a);
        }
        const float rich = hsv.h < 0.2f ? 0.0f : hsv.h < 5.0f / 6.0f ? 2.0f / 3.0f : 1.0f;
        const float depth = std::clamp(-amount, 0.0f, 1.0f);
        const float s = std::min(1.0f, hsv.s + (1.0f - hsv.s) * depth);
        const float v = hsv.v * mixf(1.0f, 0.12f, depth * depth * (3.0f - 2.0f * depth));
        return hsvToArgb(hsv.h + (rich - hsv.h) * 0.6f * depth, s, v, a);
    }

    unsigned shadow(float alpha = 0.30f) const { return colour::withAlpha(0xff000000u, alpha * opacity); }

    unsigned base = 0xffff6b1au;
    Hsv hsv{};
    float opacity = 1.0f;
};

struct Pt {
    float x = 0.0f;
    float y = 0.0f;
};

void layEllipse(visage::Canvas& canvas, float cx, float cy, float rx, float ry, unsigned fill, unsigned shadow,
                float lift) {
    const float w = 2.0f * rx;
    const float h = 2.0f * ry;
    canvas.setColor(shadow);
    canvas.superEllipse(cx - rx + lift * 0.4f, cy - ry + lift, w, h, 2.0f);
    canvas.setColor(fill);
    canvas.superEllipse(cx - rx, cy - ry, w, h, 2.0f);
}

void layPath(visage::Canvas& canvas, const visage::Path& path, float x, float y, float w, float h, unsigned fill,
             unsigned shadow, float lift) {
    canvas.setColor(shadow);
    canvas.fill(path, x + lift * 0.4f, y + lift, w, h);
    canvas.setColor(fill);
    canvas.fill(path, x, y, w, h);
}

// 0: CUT — Diagonal strips of tape shunting against each other on the beat.
void drawCut(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
             float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    constexpr int rows = 7;
    const float rowHeight = h / static_cast<float>(rows);
    const int pieces = 2 + static_cast<int>(std::lround(m.x * 5.0f));
    const float settled = ease(1.0f - m.kick);
    const float lean = rowHeight * 0.55f;
    const float shunt = pulse(m.beat) * rowHeight * 0.10f;

    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    constexpr float angle = -0.06f;
    const float ca = std::cos(angle) * 1.14f;
    const float sa = std::sin(angle) * 1.14f;
    const auto xform = [&](float lx, float ly) -> Pt { return {cx + lx * ca - ly * sa, cy + lx * sa + ly * ca}; };

    for (int row = 0; row < rows; ++row) {
        const float top = (static_cast<float>(row) - rows * 0.5f) * rowHeight;
        const auto edge = [&](int index) {
            if (index <= 0 || index >= pieces)
                return (index <= 0 ? -0.5f : 0.5f) * w * 1.3f;
            return (static_cast<float>(index) / static_cast<float>(pieces) - 0.5f +
                    (chance(row, index) - 0.5f) * 0.4f / static_cast<float>(pieces)) *
                   w;
        };

        for (int piece = 0; piece < pieces; ++piece) {
            const auto slide = [&](int cut) {
                return (chance(row * 31 + piece, cut, 1) - 0.5f) * 2.8f * rowHeight * m.y;
            };
            const float dx =
                mixf(slide(m.cuts - 1), slide(m.cuts), settled) + (row % 2 == 0 ? shunt : -shunt) +
                sway(m.beat, 8.0f, static_cast<float>(row) * 1.3f + static_cast<float>(piece) * 2.1f) * 3.0f * scale;
            const float dy = (chance(row, piece, 2) - 0.5f) * rowHeight * 0.14f;
            const float left = edge(piece) + 2.0f * scale;
            const float right = edge(piece + 1) - 2.0f * scale;

            const Pt a = xform(left + lean + dx, top + 1.5f * scale + dy);
            const Pt b = xform(right + lean + dx, top + 1.5f * scale + dy);
            const Pt c = xform(right - lean + dx, top + rowHeight - 1.5f * scale + dy);
            const Pt d = xform(left - lean + dx, top + rowHeight - 1.5f * scale + dy);

            visage::Path strip;
            strip.moveTo(a.x, a.y);
            strip.lineTo(b.x, b.y);
            strip.lineTo(c.x, c.y);
            strip.lineTo(d.x, d.y);
            strip.close();
            layPath(canvas, strip, x, y, w, h, card.tone(mixf(-0.38f, 0.22f, chance(row, piece, 3))), card.shadow(),
                    3.0f * scale);
        }
    }
}

// 1: BOUNCE — Arcing hops across hills and draped card with a squashing beat-synced ball.
void drawBounce(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
                float scale) {
    canvas.setColor(card.tone(-0.90f));
    canvas.rectangle(x, y, w, h);

    const float floorY = h * 0.76f;

    for (int drape = 0; drape < 2; ++drape) {
        visage::Path edge;
        edge.moveTo(0.0f, -8.0f * scale);
        for (int step = 0; step <= 25; ++step) {
            const float u = static_cast<float>(step) / 25.0f;
            const float py = h * (0.02f + (0.20f - 0.09f * static_cast<float>(drape)) * u * u) +
                             std::sin(u * 6.0f + static_cast<float>(drape) * 1.4f) * h * 0.03f +
                             sway(m.beat, 16.0f, u * 3.0f) * 3.0f * scale;
            edge.lineTo(u * w, py);
        }
        edge.lineTo(w, -8.0f * scale);
        edge.close();
        layPath(canvas, edge, x, y, w, h, card.tone(drape == 0 ? -0.80f : -0.68f), card.shadow(), 4.0f * scale);
    }

    for (int hill = 0; hill < 2; ++hill) {
        visage::Path ground;
        ground.moveTo(0.0f, h + 8.0f * scale);
        for (int step = 0; step <= 25; ++step) {
            const float u = static_cast<float>(step) / 25.0f;
            const float py =
                floorY + 16.0f * scale + static_cast<float>(hill) * h * 0.09f +
                std::sin(u * (5.0f + static_cast<float>(hill) * 2.0f) + static_cast<float>(hill) * 2.0f) * h * 0.035f;
            ground.lineTo(u * w, py);
        }
        ground.lineTo(w, h + 8.0f * scale);
        ground.close();
        layPath(canvas, ground, x, y, w, h, card.tone(hill == 0 ? -0.72f : -0.55f), card.shadow(), -3.0f * scale);
    }

    const int hops = 3 + static_cast<int>(std::lround(m.x * 3.0f));
    constexpr float shrink = 0.76f;
    const float firstSpan = w * 0.86f * (1.0f - shrink) / (1.0f - std::pow(shrink, static_cast<float>(hops)));
    const auto heightOf = [&](int hop) {
        const float i = static_cast<float>(hop);
        return h * 0.50f * mixf(std::pow(0.70f, i), 0.42f * std::pow(1.16f, i), m.y);
    };

    float cx = x + w * 0.07f;
    float span = firstSpan;
    std::array<float, 8> starts{};
    std::array<float, 8> spans{};

    const float arcThickness = 11.0f * scale;
    const float absFloor = y + floorY;
    for (int hop = 0; hop < hops; ++hop) {
        starts[static_cast<std::size_t>(hop)] = cx;
        spans[static_cast<std::size_t>(hop)] = span;
        const float peakY = absFloor - 2.0f * heightOf(hop);

        canvas.setColor(card.shadow());
        canvas.quadratic(cx + 1.2f * scale, absFloor + 3.0f * scale, cx + span * 0.5f + 1.2f * scale,
                         peakY + 3.0f * scale, cx + span + 1.2f * scale, absFloor + 3.0f * scale, arcThickness);
        canvas.setColor(card.tone(-0.10f));
        canvas.quadratic(cx, absFloor, cx + span * 0.5f, peakY, cx + span, absFloor, arcThickness);

        const float lr =
            mixf(11.0f, 6.0f, static_cast<float>(hop) / static_cast<float>(hops)) * (1.0f + 0.45f * m.kick) * scale;
        layEllipse(canvas, cx + span, absFloor, lr, lr, card.tone(0.30f), card.shadow(), 3.0f * scale);

        cx += span;
        span *= shrink;
    }

    const float travelled = std::fmod(m.beat, static_cast<float>(hops));
    const int hop = std::clamp(static_cast<int>(travelled), 0, hops - 1);
    const float along = travelled - static_cast<float>(hop);
    const float landing = pulse(m.beat + 0.06f);
    const float bx = starts[static_cast<std::size_t>(hop)] + spans[static_cast<std::size_t>(hop)] * along;
    const float by = absFloor - 4.0f * heightOf(hop) * along * (1.0f - along) - 16.0f * scale;
    const float br = 15.0f * scale;
    layEllipse(canvas, bx, by, br * (1.0f + 0.25f * landing), br * (1.0f - 0.20f * landing), card.tone(1.0f),
               card.shadow(), 5.0f * scale);
}

// 2: GLITCH — Tiled slip grid stairing with Y and strobing on sixteenths.
void drawGlitch(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
                float scale) {
    canvas.setColor(card.tone(-0.95f));
    canvas.rectangle(x, y, w, h);

    const int columns = 10 + static_cast<int>(std::lround(m.x * 10.0f));
    const float tile = w / static_cast<float>(columns);
    const int rows = static_cast<int>(std::ceil(h / tile)) + 2;
    const float settled = ease(std::min(1.0f, (1.0f - m.kick) * 3.0f));
    const int sixteenth = static_cast<int>(m.beat * 4.0f) % 64;
    const float glow = pulse(m.beat * 4.0f);
    const float pad = 1.0f * scale;

    for (int row = -1; row < rows; ++row) {
        const float fromMiddle = std::abs(static_cast<float>(row) - static_cast<float>(rows) * 0.5f + 1.0f) /
                                 (static_cast<float>(rows) * 0.5f);
        const auto slip = [&](int cut) {
            return chance(row, cut) < 0.4f ? 0.0f
                                           : (chance(row, cut, 1) - 0.5f) * tile * 7.0f * mixf(0.15f, 1.0f, fromMiddle);
        };
        const float dx = mixf(slip(m.cuts - 1), slip(m.cuts), settled);

        for (int col = -4; col < columns + 4; ++col) {
            const float stair = std::floor(static_cast<float>(col) / 3.0f - static_cast<float>(columns) / 6.0f) *
                                (m.y - 0.5f) * tile * 0.5f;
            const float rx = x + static_cast<float>(col) * tile + dx + pad;
            const float ry = y + static_cast<float>(row) * tile + stair + pad;
            if (rx + tile < x || rx > x + w || ry + tile < y || ry > y + h)
                continue;
            const bool lit = chance(col * 7 + row, sixteenth, 5) > 0.975f;
            canvas.setColor(card.tone(lit ? mixf(0.10f, 1.0f, glow) : mixf(-0.72f, 0.05f, chance(col, row, 9))));
            canvas.rectangle(rx, ry, tile - 2.0f * pad, tile - 2.0f * pad);
        }
    }
}

// 3: SUCTION — Nine nested tunnel rings pulsing inward every two beats.
void drawSuction(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
                 float scale) {
    canvas.setColor(card.tone(0.10f));
    canvas.rectangle(x, y, w, h);

    constexpr int rings = 9;
    const float mx = x + w * mixf(0.26f, 0.74f, m.x);
    const float my = y + h * 0.56f;
    const float cx0 = x + w * 0.5f;
    const float cy0 = y + h * 0.5f;
    const float reach = std::hypot(w, h) * 0.80f * (1.0f + 0.22f * ease(m.kick));
    const float squeeze = mixf(0.80f, 0.34f, m.y);
    const float flow = frac(m.beat * 0.5f);
    const float drop = 5.0f * scale;

    for (int ring = rings + 1; ring >= 1; --ring) {
        const float depth = (static_cast<float>(ring) - flow) / static_cast<float>(rings);
        const float radius = reach * std::pow(depth, 1.7f);
        const float rcx = mx + (cx0 - mx) * std::min(1.0f, depth) * 0.55f;
        const float rcy = my + (cy0 - my) * std::min(1.0f, depth) * 0.55f;
        const unsigned col = card.tone(mixf(-1.0f, 0.10f, std::pow(std::min(1.0f, depth), 0.6f)));
        const unsigned rim = colour::withAlpha(colour::lerpArgb(col, 0xff000000u, 0.42f), card.opacity);

        canvas.setColor(rim);
        canvas.superEllipse(rcx - radius, rcy - radius * squeeze, 2.0f * radius, 2.0f * radius * squeeze, 2.0f);

        const float irx = std::max(0.0f, radius - drop);
        const float iry = std::max(0.0f, radius * squeeze - drop);
        if (irx > 0.5f && iry > 0.5f) {
            canvas.setColor(col);
            canvas.superEllipse(rcx + drop * 0.3f - irx, rcy + drop * 0.7f - iry, 2.0f * irx, 2.0f * iry, 2.0f);
        }
    }
}

// 4: FILTER — Seven layered 2-pole resonant low-pass filter response silhouettes.
void drawFilter(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
                float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    constexpr int layers = 7;
    const float resonance = mixf(0.75f, 7.0f, m.y) * (1.0f + 0.60f * m.kick) * (1.0f + 0.18f * pulse(m.beat));

    for (int layer = 0; layer < layers; ++layer) {
        const float depth = static_cast<float>(layer) / static_cast<float>(layers - 1);
        const float cutoff = mixf(0.16f, 0.84f, m.x) + depth * 0.03f;
        const float level = h * (0.30f + 0.09f * static_cast<float>(layer));

        visage::Path edge;
        float lastY = level;
        for (int step = 0; step <= 84; ++step) {
            const float u = static_cast<float>(step) / 84.0f;
            const float ratio = std::pow(2.0f, (u - cutoff) * 7.0f);
            const float denom =
                std::sqrt((1.0f - ratio * ratio) * (1.0f - ratio * ratio) + (ratio / resonance) * (ratio / resonance));
            const float mag = 1.0f / std::max(denom, 1.0e-5f);
            const float db = std::clamp(20.0f * std::log10(mag), -60.0f, 30.0f);
            const float py = level - db * h * 0.0085f;
            lastY = py;
            if (step == 0)
                edge.moveTo(u * w, py);
            else
                edge.lineTo(u * w, py);
        }

        canvas.setColor(card.shadow(0.32f));
        canvas.stroke(edge, x, y - 1.5f * scale, w, h, 5.5f * scale);

        visage::Path response = edge;
        response.lineTo(w + 8.0f * scale, lastY);
        response.lineTo(w + 8.0f * scale, h + 8.0f * scale);
        response.lineTo(0.0f, h + 8.0f * scale);
        response.close();
        canvas.setColor(card.tone(mixf(-0.70f, 0.15f, depth)));
        canvas.fill(response, x, y, w, h);
    }
}

// 5: TAPE — Twin rotating reels with three cutout windows and sagging tape loops.
void drawTape(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
              float scale) {
    canvas.setColor(card.tone(-0.90f));
    canvas.rectangle(x, y, w, h);

    const float radius = h * 0.30f;
    const std::array<Pt, 2> hubs{{{x + w * 0.25f, y + h * 0.43f}, {x + w * 0.75f, y + h * 0.43f}}};

    const float slack = h * mixf(0.07f, 0.24f, m.x) * mixf(0.60f, 1.0f, ease(m.kick));
    const int loops = 1 + static_cast<int>(std::lround(m.y * 3.0f));
    const Pt from{hubs[0].x - radius * 0.10f, hubs[0].y + radius * 0.82f};
    const Pt to{hubs[1].x + radius * 0.10f, hubs[1].y + radius * 0.82f};
    const float tapeThick = 10.0f * scale;

    for (int loop = 0; loop < loops; ++loop) {
        const float t0 = static_cast<float>(loop) / static_cast<float>(loops);
        const float t1 = static_cast<float>(loop + 1) / static_cast<float>(loops);
        const Pt p0{mixf(from.x, to.x, t0), mixf(from.y, to.y, t0)};
        const Pt p1{mixf(from.x, to.x, t1), mixf(from.y, to.y, t1)};
        const Pt mid{(p0.x + p1.x) * 0.5f,
                     (p0.y + p1.y) * 0.5f + radius * 0.40f +
                         2.0f * slack * (1.0f + 0.06f * sway(m.beat, 4.0f, static_cast<float>(loop)))};

        canvas.setColor(card.shadow());
        canvas.quadratic(p0.x + 2.0f * scale, p0.y + 5.0f * scale, mid.x + 2.0f * scale, mid.y + 5.0f * scale,
                         p1.x + 2.0f * scale, p1.y + 5.0f * scale, tapeThick);
        canvas.setColor(card.tone(0.35f));
        canvas.quadratic(p0.x, p0.y, mid.x, mid.y, p1.x, p1.y, tapeThick);

        if (loop + 1 < loops)
            layEllipse(canvas, p1.x, p1.y + 7.0f * scale, 8.5f * scale, 8.5f * scale, card.tone(0.90f), card.shadow(),
                       2.0f * scale);
    }

    for (std::size_t reel = 0; reel < hubs.size(); ++reel) {
        const Pt hub = hubs[reel];
        const float angle = (2.0f * kPi / 3.0f) * turn(m.spin, 1.0f) + static_cast<float>(reel) * 0.4f;
        const float packR = radius * (reel == 0 ? 0.93f : 0.86f);
        layEllipse(canvas, hub.x, hub.y, packR, packR, card.tone(-0.72f), card.shadow(), 3.0f * scale);

        // Outer flange with 3 cutout windows (drawn cleanly by overlaying the flange then punching dark tape windows)
        layEllipse(canvas, hub.x, hub.y, radius, radius, card.tone(reel == 0 ? -0.05f : 0.08f), card.shadow(),
                   4.0f * scale);
        for (int win = 0; win < 3; ++win) {
            const float dir = angle + static_cast<float>(win) * 2.0f * kPi / 3.0f;
            const float wx = hub.x + std::cos(dir) * radius * 0.55f;
            const float wy = hub.y + std::sin(dir) * radius * 0.55f;
            const float wr = radius * 0.27f;
            canvas.setColor(card.tone(-0.78f));
            canvas.superEllipse(wx - wr, wy - wr, 2.0f * wr, 2.0f * wr, 2.0f);
            canvas.setColor(card.shadow(0.25f));
            canvas.ring(wx - wr, wy - wr, 2.0f * wr, 1.5f * scale);
        }
        layEllipse(canvas, hub.x, hub.y, radius * 0.15f, radius * 0.15f, card.tone(0.90f), card.shadow(), 2.0f * scale);
    }
}

// 6: LOOPS — Concentric rotating rings inside rings with cut start-slots.
void drawLoops(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
               float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const auto nest = [&](float cx, float cy, float outer, int levels, float beats) {
        for (int level = 0; level < levels; ++level) {
            const float r = outer * std::pow(0.72f, static_cast<float>(level)) * (1.0f + 0.05f * m.kick);
            layEllipse(canvas, cx, cy, r, r, card.tone(mixf(-0.60f, 0.45f, static_cast<float>(level) / 5.0f)),
                       card.shadow(), 4.0f * scale);

            const float angle = 2.0f * kPi * turn(m.beat, beats / std::pow(2.0f, static_cast<float>(level))) +
                                static_cast<float>(level) * 0.9f;
            const float slotOuter = r * 0.97f;
            const float slotThick = r * 0.22f;
            canvas.setColor(card.tone(-1.0f));
            canvas.flatArc(cx - slotOuter, cy - slotOuter, 2.0f * slotOuter, slotThick, angle, 0.18f);
        }
    };

    const int levels = 2 + static_cast<int>(std::lround(m.y * 4.0f));
    nest(x + w * 0.90f, y + h * 0.14f, h * 0.34f, 4, -16.0f);
    nest(x + w * 0.10f, y + h * 0.92f, h * 0.42f, 4, 32.0f);
    nest(x + w * 0.52f, y + h * 0.50f, h * mixf(0.60f, 0.40f, m.x), levels, 4.0f);
}

// 7: RUSH — Fifteen wedges racing to a focal point with streaking darts and impact burst.
void drawRush(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const ArtMotion& m,
              float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    constexpr int wedges = 15;
    const Pt point{x + w - w * 0.035f, y + h * mixf(0.66f, 0.34f, m.x)};
    const float startX = x + w * mixf(0.50f, 0.0f, m.y) - 20.0f * scale;
    const auto rimAt = [&](int index) -> Pt {
        const float spread = static_cast<float>(index) / static_cast<float>(wedges) +
                             sway(m.beat, 4.0f, static_cast<float>(index) * 0.9f) * 0.004f;
        return {startX, y + h * mixf(-0.45f, 1.45f, spread)};
    };

    for (int wedge = 0; wedge < wedges; ++wedge) {
        const Pt a = rimAt(wedge);
        const Pt b = rimAt(wedge + 1);
        canvas.setColor(card.shadow(0.26f));
        canvas.triangle(a.x + 0.8f * scale, a.y + 2.0f * scale, b.x + 0.8f * scale, b.y + 2.0f * scale,
                        point.x + 0.8f * scale, point.y + 2.0f * scale);
        canvas.setColor(card.tone(wedge % 3 == 1 ? -0.90f : mixf(-0.30f, 0.30f, chance(wedge, 5))));
        canvas.triangle(a.x, a.y, b.x, b.y, point.x, point.y);
    }

    for (int seam = 1; seam < wedges; ++seam) {
        const float along = std::pow(frac(turn(m.beat, 4.0f) + chance(seam, 9)), 2.4f);
        const Pt from = rimAt(seam);
        const Pt at{mixf(from.x, point.x, along), mixf(from.y, point.y, along)};
        const float tailT = std::max(0.0f, along - 0.05f * (1.0f - along) - 0.01f);
        const Pt tail{mixf(from.x, point.x, tailT), mixf(from.y, point.y, tailT)};
        canvas.setColor(card.tone(1.0f, std::min(1.0f, 4.0f * along) * (1.0f - along)));
        canvas.segment(tail.x, tail.y, at.x, at.y, 2.8f * scale, true);
    }

    if (m.kick > 0.01f) {
        const float burstR = (8.0f + 46.0f * (1.0f - m.kick)) * scale;
        canvas.setColor(card.tone(1.0f, m.kick));
        canvas.circle(point.x - burstR, point.y - burstR, 2.0f * burstR);
    }
}

} // namespace

void drawProgramArt(visage::Canvas& canvas, int program, float x, float y, float width, float height,
                    const ArtMotion& motion, float scale, float opacity) {
    if (width <= 1.0f || height <= 1.0f || opacity <= 0.005f)
        return;

    const int idx = std::clamp(program, 0, 7);
    const Card card(colour::programArgb(idx), opacity);

    canvas.saveState();
    canvas.trimClampBounds(x, y, width, height);

    switch (idx) {
    case 0:
        drawCut(canvas, x, y, width, height, card, motion, scale);
        break;
    case 1:
        drawBounce(canvas, x, y, width, height, card, motion, scale);
        break;
    case 2:
        drawGlitch(canvas, x, y, width, height, card, motion, scale);
        break;
    case 3:
        drawSuction(canvas, x, y, width, height, card, motion, scale);
        break;
    case 4:
        drawFilter(canvas, x, y, width, height, card, motion, scale);
        break;
    case 5:
        drawTape(canvas, x, y, width, height, card, motion, scale);
        break;
    case 6:
        drawLoops(canvas, x, y, width, height, card, motion, scale);
        break;
    default:
        drawRush(canvas, x, y, width, height, card, motion, scale);
        break;
    }

    canvas.restoreState();
}

void drawWordmark(visage::Canvas& canvas, float x, float y, float width, float height, float scale, bool focused) {
    const float fontSize = 18.0f * scale;
    const auto font = hba::ui::fontPx(fontSize, Weight::semibold);
    const float liveW = 48.0f * scale;
    const float cutW = 46.0f * scale;
    const float slashX = x + liveW + 3.0f * scale;

    // "LIVE" crisp on baseline
    canvas.setColor(colour::text);
    canvas.text("LIVE", font, visage::Font::kRight, x, y - 1.5f * scale, liveW, height);

    // Signature diagonal orange blade cut between LIVE and CUT
    canvas.setColor(colour::accent);
    canvas.segment(slashX - 3.0f * scale, y + height * 0.78f, slashX + 4.5f * scale, y + height * 0.22f, 2.4f * scale,
                   true);

    // "CUT" slightly dropped like an offcut
    canvas.setColor(colour::text);
    canvas.text("CUT", font, visage::Font::kLeft, slashX + 5.0f * scale, y + 2.0f * scale, cutW, height);

    // Subtle "2.0" studio tag
    canvas.setColor(colour::textFaint);
    canvas.text("2.0", hba::ui::fontPx(9.5f * scale, Weight::semibold), visage::Font::kLeft, slashX + 46.0f * scale,
                y + 4.0f * scale, 26.0f * scale, height);

    if (focused) {
        canvas.setColor(colour::accent);
        canvas.roundedRectangleBorder(x, y, width, height, 4.0f * scale, 1.0f * scale);
    }
}

} // namespace lc::ui
