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

float hash01(int a, int b) {
    auto h = static_cast<std::uint32_t>(a) * 374761393u + static_cast<std::uint32_t>(b) * 668265263u;
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

// 0: SLICE — Kinetic shearing 8-lane beat-slice tape ribbons
void drawSlice(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
               float scale) {
    canvas.setColor(card.tone(-0.90f));
    canvas.rectangle(x, y, w, h);

    constexpr int rows = 8;
    const float rowH = h / static_cast<float>(rows);
    const int cols = 4 + static_cast<int>(std::lround(m.x * 6.0f));
    const float slant = (18.0f + 44.0f * m.x) * scale;
    const float gap = (3.5f + 2.5f * (1.0f - m.y)) * scale;

    for (int r = 0; r < rows; ++r) {
        const float ry = y + static_cast<float>(r) * rowH;
        const float flash = m.laneFlash[static_cast<std::size_t>(r)];
        const float dir = (r & 1) ? -1.0f : 1.0f;
        const float drift = dir * (m.beat * (0.28f + 0.55f * m.x) + static_cast<float>(r) * 0.19f);
        const float cellW = (w + slant * 2.0f) / static_cast<float>(cols);

        for (int c = -1; c <= cols + 1; ++c) {
            const float phase = frac(drift) * cellW;
            const float x0 = x - slant + static_cast<float>(c) * cellW + phase;
            const float wSlice = cellW - gap;
            const float activeWeight = hash01(r * 19 + c, 7);
            if (activeWeight > (0.28f + 0.72f * m.y))
                continue;

            // Path coordinates MUST be local to (0, 0)..(w, h) because canvas.fill(path, x, y, w, h) translates by (x,
            // y)!
            const float lx0 = x0 - x;
            const float lry = ry - y;
            visage::Path p;
            p.moveTo(lx0 + slant * 0.45f, lry + gap * 0.5f);
            p.lineTo(lx0 + wSlice + slant * 0.45f, lry + gap * 0.5f);
            p.lineTo(lx0 + wSlice - slant * 0.45f, lry + rowH - gap * 0.5f);
            p.lineTo(lx0 - slant * 0.45f, lry + rowH - gap * 0.5f);
            p.close();

            const float tileTone = mixf(-0.58f, 0.24f, hash01(r * 31 + c, 13));
            const float shade = std::clamp(tileTone + 0.38f * flash + 0.10f * m.engage, -0.70f, 0.65f);
            const float lift = 3.5f * scale;
            canvas.setColor(card.shadow(0.42f));
            canvas.fill(p, x + lift * 0.5f, y + lift, w, h);
            canvas.setColor(card.tone(shade));
            canvas.fill(p, x, y, w, h);

            if (flash > 0.22f) {
                canvas.setColor(colour::withAlpha(0xffffffffu, 0.42f * flash * card.opacity));
                canvas.segment(x0 + slant * 0.45f, ry + gap * 0.5f, x0 + wSlice + slant * 0.45f, ry + gap * 0.5f,
                               2.2f * scale, true);
            }
        }
    }

    // Crisp diagonal shear laser lines reacting to X chop depth
    const int cuts = 3 + static_cast<int>(std::lround(m.x * 5.0f));
    for (int k = 1; k < cuts; ++k) {
        const float cx0 = x + (static_cast<float>(k) / static_cast<float>(cuts)) * w;
        canvas.setColor(card.tone(0.55f, 0.18f + 0.24f * m.engage));
        canvas.segment(cx0 + slant * 1.4f, y, cx0 - slant * 1.4f, y + h, 1.4f * scale, true);
    }
}

// 1: ROLL — Exponentially compressing vertical cadence shockwaves & escalating velocity terraces
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

// 2: BOUNCE — Physical gravity bouncing-ball parabolic trajectories & luminous spheres
void drawBounce(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
                float scale) {
    canvas.setColor(card.tone(-0.90f));
    canvas.rectangle(x, y, w, h);

    // Warm terraced floor gradient bands
    for (int band = 0; band < 5; ++band) {
        const float fracB = static_cast<float>(band) / 5.0f;
        canvas.setColor(card.tone(-0.82f + 0.18f * fracB));
        canvas.rectangle(x, y + h * (0.58f + fracB * 0.42f), w, h * 0.09f);
    }

    const int hops = 5 + static_cast<int>(std::lround(m.y * 5.0f));
    const float accel = (m.x - 0.5f) * 2.0f; // -1 decel .. +1 accel
    float cursorX = x + 36.0f * scale;
    const float usableW = w - 72.0f * scale;
    const float baseY = y + h * 0.84f;

    // Normalize hop widths so they span nicely across the canvas
    float sumW = 0.0f;
    std::array<float, 12> weights{};
    for (int i = 0; i < hops; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(std::max(1, hops - 1));
        const float wgt = accel >= 0.0f ? std::pow(1.0f - 0.68f * accel, u * 3.0f)
                                        : std::pow(1.0f + 0.68f * accel, (1.0f - u) * 3.0f);
        weights[static_cast<std::size_t>(i)] = std::max(0.14f, wgt);
        sumW += weights[static_cast<std::size_t>(i)];
    }

    const float phase = frac(m.beat * 0.5f);
    for (int i = 0; i < hops; ++i) {
        const float segW = usableW * (weights[static_cast<std::size_t>(i)] / sumW);
        const float u = static_cast<float>(i) / static_cast<float>(std::max(1, hops - 1));
        const float peakH = h * (0.18f + 0.56f * m.y) * (accel >= 0.0f ? (1.0f - 0.58f * u) : (0.42f + 0.58f * u));

        // Draw parabolic arc ribbon
        constexpr int kSteps = 18;
        for (int s = 0; s < kSteps; ++s) {
            const float t0 = static_cast<float>(s) / static_cast<float>(kSteps);
            const float t1 = static_cast<float>(s + 1) / static_cast<float>(kSteps);
            const float x0 = cursorX + t0 * segW;
            const float y0 = baseY - 4.0f * peakH * t0 * (1.0f - t0);
            const float x1 = cursorX + t1 * segW;
            const float y1 = baseY - 4.0f * peakH * t1 * (1.0f - t1);
            canvas.setColor(card.tone(0.15f + 0.25f * m.impulse, 0.72f));
            canvas.segment(x0, y0, x1, y1, (2.4f + 1.5f * m.engage) * scale, true);
        }

        // Luminous orb riding each bounce apex!
        const float orbT = frac(phase + static_cast<float>(i) * 0.14f);
        const float ox = cursorX + orbT * segW;
        const float oy = baseY - 4.0f * peakH * orbT * (1.0f - orbT);
        const float rad =
            (9.0f + 14.0f * (weights[static_cast<std::size_t>(i)] / weights[0]) + 5.0f * m.impulse) * scale;
        canvas.setColor(card.tone(0.35f, 0.22f));
        canvas.circle(ox - rad * 1.6f, oy - rad * 1.6f, rad * 3.2f);
        canvas.setColor(card.tone(0.28f + 0.35f * m.impulse));
        canvas.circle(ox - rad, oy - rad, rad * 2.0f);
        canvas.setColor(card.tone(0.75f));
        canvas.circle(ox - rad * 0.38f, oy - rad * 0.38f, rad * 0.76f);

        // Floor impact flash ring
        canvas.setColor(card.tone(0.50f, 0.55f));
        canvas.roundedRectangle(cursorX - 6.0f * scale, baseY - 2.0f * scale, 12.0f * scale, 4.0f * scale,
                                2.0f * scale);
        cursorX += segW;
    }
}

// 3: SWING — Magnetic gravitational pocket meridians curving toward the swing attractor + ghost diamonds
void drawSwing(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
               float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const float pull = (m.x - 0.5f) * 2.0f; // -1..+1
    constexpr int kLines = 16;
    constexpr int kSegs = 22;

    for (int i = 0; i <= kLines; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kLines);
        const bool isMajor = (i % 4) == 0;
        const bool isOdd = (i & 1) == 1;
        const float baseX = x + u * w;

        for (int s = 0; s < kSegs; ++s) {
            const float v0 = static_cast<float>(s) / static_cast<float>(kSegs);
            const float v1 = static_cast<float>(s + 1) / static_cast<float>(kSegs);
            const float env0 = std::sin(kPi * v0);
            const float env1 = std::sin(kPi * v1);
            const float wave0 = std::sin(kTwoPi * (v0 * 1.5f + m.beat * 0.25f));
            const float wave1 = std::sin(kTwoPi * (v1 * 1.5f + m.beat * 0.25f));
            const float shift = (isOdd ? 1.0f : 0.25f) * pull * 42.0f * scale;

            const float px0 = baseX + shift * env0 + wave0 * 6.0f * scale * m.y;
            const float py0 = y + v0 * h;
            const float px1 = baseX + shift * env1 + wave1 * 6.0f * scale * m.y;
            const float py1 = y + v1 * h;

            canvas.setColor(card.tone(isMajor ? 0.35f : (isOdd ? 0.05f : -0.35f), isMajor ? 0.82f : 0.52f));
            canvas.segment(px0, py0, px1, py1, (isMajor ? 2.4f : 1.4f) * scale, true);
        }
    }

    // Shimmering floating ghost-note diamonds scattered organically across the magnetic pocket!
    const int ghosts = 8 + static_cast<int>(std::lround(m.y * 20.0f));
    for (int g = 0; g < ghosts; ++g) {
        const float gu = frac(hash01(g, 3) + m.beat * (0.02f + 0.02f * hash01(g, 9)));
        const float gv = hash01(g, 17);
        const float gx = x + (0.05f + 0.90f * gu) * w + pull * 28.0f * scale * std::sin(kPi * gv);
        const float gy = y + (0.10f + 0.80f * gv) * h;
        const float flash = m.laneFlash[static_cast<std::size_t>(g % kDrumLaneCount)];
        const float sz = (6.0f + 11.0f * m.y * (0.5f + 0.5f * hash01(g, 29)) + 7.0f * flash) * scale;

        const float lgx = gx - x;
        const float lgy = gy - y;
        visage::Path d;
        d.moveTo(lgx, lgy - sz);
        d.lineTo(lgx + sz * 0.72f, lgy);
        d.lineTo(lgx, lgy + sz);
        d.lineTo(lgx - sz * 0.72f, lgy);
        d.close();
        canvas.setColor(card.shadow(0.30f));
        canvas.fill(d, x + 1.5f * scale, y + 2.5f * scale, w, h);
        canvas.setColor(card.tone(0.14f + 0.58f * flash, 0.34f + 0.56f * m.y));
        canvas.fill(d, x, y, w, h);
    }
}

// 4: POLY — ICONIC 8 CONCENTRIC ROTATING POLYMETRIC CLOCKWORK RINGS + HARMONIC RADAR WINGS!!
void drawPoly(visage::Canvas& canvas, float x, float y, float w, float h, const Card& card, const PadMotion& m,
              float scale) {
    canvas.setColor(card.tone(-0.92f));
    canvas.rectangle(x, y, w, h);

    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float maxR = std::min(w, h) * 0.46f;

    // Symmetrical widescreen harmonic phase arcs framing the central polymetric planetarium!
    for (int wv = 0; wv < 4; ++wv) {
        const float amp = (28.0f + static_cast<float>(wv) * 26.0f) * scale * (0.45f + 0.55f * m.y);
        const float freq = 1.5f + static_cast<float>(wv) * 0.75f + m.x * 1.5f;
        const float ph = m.beat * 0.35f * ((wv & 1) ? -1.0f : 1.0f) + m.y * kTwoPi;
        constexpr int kSegs = 40;
        for (int s = 0; s < kSegs; ++s) {
            const float u0 = static_cast<float>(s) / static_cast<float>(kSegs);
            const float u1 = static_cast<float>(s + 1) / static_cast<float>(kSegs);
            const float env0 = std::sin(kPi * u0);
            const float env1 = std::sin(kPi * u1);
            const float y0 = cy + std::sin(u0 * kTwoPi * freq + ph) * amp * env0;
            const float y1 = cy + std::sin(u1 * kTwoPi * freq + ph) * amp * env1;
            canvas.setColor(card.tone(-0.18f + 0.14f * static_cast<float>(wv), 0.16f + 0.10f * m.engage));
            canvas.segment(x + u0 * w, y0, x + u1 * w, y1, 1.3f * scale, true);
        }
    }

    // Subtle deep radial backdrop glow
    for (int g = 3; g >= 1; --g) {
        const float gr = maxR * (0.42f + 0.28f * static_cast<float>(g));
        canvas.setColor(card.tone(-0.42f + 0.16f * m.engage, 0.11f));
        canvas.circle(cx - gr, cy - gr, gr * 2.0f);
    }

    // Outer calibrated compass ticks around the planetarium bezel
    constexpr int kTicks = 48;
    for (int t = 0; t < kTicks; ++t) {
        const float a = -kPi * 0.5f + kTwoPi * (static_cast<float>(t) / static_cast<float>(kTicks));
        const bool maj = (t % 4) == 0;
        const float r0 = maxR * 1.03f;
        const float r1 = maxR * (maj ? 1.09f : 1.06f);
        canvas.setColor(card.tone(maj ? 0.32f : -0.25f, maj ? 0.55f : 0.28f));
        canvas.segment(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * r1, cy + std::sin(a) * r1,
                       (maj ? 1.6f : 1.0f) * scale, true);
    }

    // Dynamic polygon vertex counts per lane reflecting live polymeter twist X!
    std::array<int, kDrumLaneCount> sides = m.laneLens;
    if (m.x >= 0.80f)
        sides = {13, 15, 11, 9, 7, 11, 5, 3};
    else if (m.x >= 0.52f)
        sides = {16, 16, 11, 12, 12, 15, 7, 5};
    else if (m.x >= 0.24f)
        sides = {16, 16, 15, 12, 14, 16, 9, 7};

    // Draw 8 concentric glowing polymetric rings + active polygon chords + jewel-coded hit nodes
    for (int l = kDrumLaneCount - 1; l >= 0; --l) {
        const auto li = static_cast<std::size_t>(l);
        const float rFrac = 0.24f + 0.76f * (static_cast<float>(l) / static_cast<float>(kDrumLaneCount - 1));
        const float rad = maxR * rFrac;
        const float flash = m.laneFlash[li];
        const unsigned laneHue = colour::lerpArgb(card.base, colour::lane[li], 0.62f);
        const int n = std::clamp(sides[li], 3, 16);
        const int polyVerts = std::clamp(n > 10 ? (n / 2) : n, 3, 9);
        const float rot = -kPi * 0.5f + m.y * kTwoPi * (0.25f + 0.12f * static_cast<float>(l)) +
                          m.beat * (kTwoPi / static_cast<float>(std::max(3, n))) * ((l & 1) ? 0.5f : -0.35f);

        // Orbit ring border
        canvas.setColor(colour::withAlpha(laneHue, (0.20f + 0.48f * flash) * card.opacity));
        canvas.ring(cx - rad, cy - rad, rad * 2.0f, (1.1f + 1.6f * flash) * scale);

        // Draw crisp inscribed geometric polygon!
        float prevPx = 0.0f, prevPy = 0.0f;
        float firstPx = 0.0f, firstPy = 0.0f;
        for (int v = 0; v < polyVerts; ++v) {
            const float ang = rot + kTwoPi * (static_cast<float>(v) / static_cast<float>(polyVerts));
            const float vx = cx + std::cos(ang) * rad;
            const float vy = cy + std::sin(ang) * rad;
            if (v == 0) {
                firstPx = vx;
                firstPy = vy;
            } else {
                canvas.setColor(colour::withAlpha(laneHue, (0.34f + 0.54f * flash) * card.opacity));
                canvas.segment(prevPx, prevPy, vx, vy, (1.4f + 1.4f * flash) * scale, true);
            }
            prevPx = vx;
            prevPy = vy;

            // Radius-scaled jewel node; only strongly flashing beats bloom white!
            const bool hot = (flash > 0.72f) && (v == 0 || (v % 2) == 0);
            const float nr = (2.4f + 2.4f * rFrac + 2.2f * flash) * scale;
            if (hot) {
                canvas.setColor(colour::withAlpha(laneHue, 0.34f * card.opacity));
                canvas.circle(vx - nr * 1.9f, vy - nr * 1.9f, nr * 3.8f);
            }
            canvas.setColor(colour::withAlpha(hot ? 0xffffffffu : laneHue, (0.76f + 0.24f * flash) * card.opacity));
            canvas.circle(vx - nr, vy - nr, nr * 2.0f);
        }
        canvas.setColor(colour::withAlpha(laneHue, (0.34f + 0.54f * flash) * card.opacity));
        canvas.segment(prevPx, prevPy, firstPx, firstPy, (1.4f + 1.4f * flash) * scale, true);

        // Radial laser spoke when this lane triggers!
        if (flash > 0.40f) {
            const float hitAng = -kPi * 0.5f + kTwoPi * frac(m.beat * 0.25f);
            canvas.setColor(colour::withAlpha(laneHue, 0.45f * flash * card.opacity));
            canvas.segment(cx, cy, cx + std::cos(hitAng) * rad, cy + std::sin(hitAng) * rad, 1.8f * scale, true);
        }
    }

    // Rotating glowing master playhead sweep arm + bright central hub
    const float armAng = -kPi * 0.5f + kTwoPi * frac(m.beat * 0.25f);
    canvas.setColor(colour::withAlpha(0xffffffffu, 0.55f * card.opacity));
    canvas.segment(cx, cy, cx + std::cos(armAng) * maxR * 1.06f, cy + std::sin(armAng) * maxR * 1.06f, 1.8f * scale,
                   true);
    const float hubR = (13.0f + 8.0f * m.impulse) * scale;
    canvas.setColor(card.tone(0.38f, 0.30f));
    canvas.circle(cx - hubR * 1.6f, cy - hubR * 1.6f, hubR * 3.2f);
    canvas.setColor(card.tone(0.45f, 0.90f));
    canvas.circle(cx - hubR, cy - hubR, hubR * 2.0f);
    canvas.setColor(0xffffffffu);
    canvas.circle(cx - hubR * 0.36f, cy - hubR * 0.36f, hubR * 0.72f);
}

// 5: STUTTER — Fractured echoing mirror prism tiles subdividing dynamically with X
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

// 6: MORPH — Dual-horizon Sub-Bass Monoliths vs Top-End Crystalline Lattice with Time-Warp tilt
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

// 7: DROP — Dramatic converging tension chevrons & countdown aperture into beat 1!
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
        drawSlice(canvas, x, y, w, h, card, m, scale);
        break;
    case 1:
        drawRoll(canvas, x, y, w, h, card, m, scale);
        break;
    case 2:
        drawBounce(canvas, x, y, w, h, card, m, scale);
        break;
    case 3:
        drawSwing(canvas, x, y, w, h, card, m, scale);
        break;
    case 4:
        drawPoly(canvas, x, y, w, h, card, m, scale);
        break;
    case 5:
        drawStutter(canvas, x, y, w, h, card, m, scale);
        break;
    case 6:
        drawMorph(canvas, x, y, w, h, card, m, scale);
        break;
    default:
        drawDrop(canvas, x, y, w, h, card, m, scale);
        break;
    }
}

} // namespace cl::ui
