// SPDX-License-Identifier: AGPL-3.0-or-later
// Five 60fps sculptural organic GPU worlds for Lineage's PLAY canvas.
#include "strata_art.h"

#include "theme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ln::ui {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline unsigned tintWhite(unsigned base, float mixW, float alpha) {
    return colour::withAlpha(colour::lerpArgb(base, 0xffffffffu, std::clamp(mixW, 0.0f, 1.0f)),
                             std::clamp(alpha, 0.0f, 1.0f));
}

// Volcanic obsidian atmosphere illuminated by a soft radial aura tracking the live X/Y puck.
void drawAtmosphere(visage::Canvas& canvas, float x, float y, float w, float h, unsigned accent, float px, float py,
                    float energy, float alpha) {
    const unsigned bg = colour::withAlpha(colour::lerpArgb(0xff06080du, colour::scaleRgb(accent, 0.14f), 0.72f), alpha);
    canvas.setColor(bg);
    canvas.rectangle(x, y, w, h);

    const float gx = x + px * w;
    const float gy = y + (1.0f - py) * h;
    const float baseR = std::max(w, h) * (0.44f + 0.14f * energy);
    for (int i = 3; i >= 0; --i) {
        const float r = baseR * (0.26f + 0.24f * static_cast<float>(i));
        const float a = (0.052f + 0.032f * energy) * (1.0f - 0.19f * static_cast<float>(i)) * alpha;
        canvas.setColor(colour::withAlpha(accent, a));
        canvas.circle(gx - r, gy - r, 2.0f * r);
    }

    // Subtle geological coordinate hairlines
    constexpr int kCols = 12;
    constexpr int kRows = 6;
    canvas.setColor(colour::withAlpha(accent, 0.042f * alpha));
    for (int c = 1; c < kCols; ++c) {
        const float lx = x + (static_cast<float>(c) / static_cast<float>(kCols)) * w;
        canvas.rectangle(lx, y, 1.0f, h);
    }
    for (int r = 1; r < kRows; ++r) {
        const float ly = y + (static_cast<float>(r) / static_cast<float>(kRows)) * h;
        canvas.rectangle(x, ly, w, 1.0f);
    }
}

// 0. SCULPT — 3D Perspective-Projected Molten Topographic Frequency Terrain
void drawSculptWorld(visage::Canvas& canvas, float x, float y, float w, float h, const StrataMotion& m, float scale,
                     float alpha) {
    const unsigned col = colour::padAccent(0); // Molten Amber
    const float energy = std::clamp(m.rms * 1.45f + m.drive * 0.35f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    // Emerald Sub-Lock floor glow on the left bass shelf
    const float subW = w * 0.18f;
    canvas.setColor(colour::withAlpha(colour::sub, (0.055f + 0.055f * m.subPeak) * alpha));
    canvas.rectangle(x, y, subW, h);
    canvas.setColor(colour::withAlpha(colour::sub, (0.28f + 0.22f * m.subPeak) * alpha));
    canvas.rectangle(x + subW, y, 1.5f * scale, h);

    // 18 perspective topographic ridges sculpting Filter 1 + Filter 2 + ADAA Drive peaks!
    constexpr int kRidges = 18;
    constexpr int kPts = 88;
    const float f1Pos = std::clamp(0.16f + 0.68f * m.filt1Norm, 0.14f, 0.88f);
    const float f2Pos = std::clamp(0.22f + 0.66f * m.filt2Norm, 0.18f, 0.92f);

    for (int r = 0; r < kRidges; ++r) {
        const float depth = static_cast<float>(r) / static_cast<float>(kRidges - 1); // 0 = far back, 1 = foreground
        const float persp = 0.62f + 0.38f * depth;
        const float baseY = h * (0.26f + 0.64f * std::pow(depth, 1.18f));
        const float amp = h * (0.14f + 0.16f * m.drive + 0.09f * energy) * persp;
        const float xSpan = w * (0.76f + 0.24f * depth);
        const float xOff = (w - xSpan) * 0.5f;

        visage::Path ridge;
        for (int i = 0; i <= kPts; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kPts);
            const float d1 = (u - f1Pos) / (0.11f + 0.06f * (1.0f - m.y));
            const float peak1 = std::exp(-0.5f * d1 * d1) * (0.72f + 0.55f * m.y);
            const float d2 = (u - f2Pos) / 0.085f;
            const float notch2 = std::exp(-0.5f * d2 * d2) * (0.48f + 0.35f * m.density);
            const float wave = 0.14f * std::sin(u * kTwoPi * 4.0f - m.phase * 1.4f + depth * 4.8f) +
                               0.08f * std::cos(u * kTwoPi * 7.5f + m.phase * 0.9f - depth * 3.1f);
            const float scopeIdx = u * static_cast<float>(TelemetrySnapshot::kScopePoints - 1);
            const float sc = m.scope[static_cast<std::size_t>(std::clamp(static_cast<int>(scopeIdx), 0,
                                                                          TelemetrySnapshot::kScopePoints - 1))];
            const float elev = (peak1 - 0.58f * notch2 + wave + 0.22f * sc * depth) * std::sin(kPi * u);
            const float sx = xOff + u * xSpan;
            const float sy = std::clamp(baseY - elev * amp, 14.0f * scale, h - 10.0f * scale);
            if (i == 0)
                ridge.moveTo(sx, sy);
            else
                ridge.lineTo(sx, sy);
        }

        // Blend from volcanic magenta-violet in back to molten amber-white at foreground crest
        const unsigned rCol = depth < 0.45f ? colour::lerpArgb(colour::strata, col, depth / 0.45f)
                                            : colour::lerpArgb(col, 0xffffe5a8u, (depth - 0.45f) * 0.45f);
        const float a = (0.14f + 0.72f * depth * depth) * alpha;
        if (r >= kRidges - 4) {
            canvas.setColor(colour::withAlpha(col, 0.18f * depth * alpha));
            canvas.stroke(ridge, x, y, w, h, 5.2f * scale);
        }
        canvas.setColor(colour::withAlpha(rCol, a));
        canvas.stroke(ridge, x, y, w, h, (0.95f + 1.55f * depth) * scale);
    }

    // Two luminous resonant mineral beacons floating above Filter 1 (Amber) and Filter 2 (Cyan)
    const float b1x = x + f1Pos * w;
    const float b1y = y + h * (0.48f - 0.24f * m.y);
    const float b2x = x + f2Pos * w;
    const float b2y = y + h * (0.58f - 0.16f * m.density);
    const float r1 = (8.0f + 6.0f * energy) * scale;
    const float r2 = (6.5f + 4.5f * energy) * scale;

    canvas.setColor(colour::withAlpha(col, 0.26f * alpha));
    canvas.circle(b1x - r1 * 2.2f, b1y - r1 * 2.2f, r1 * 4.4f);
    canvas.setColor(tintWhite(col, 0.45f, 0.94f * alpha));
    canvas.circle(b1x - r1, b1y - r1, 2.0f * r1);

    canvas.setColor(colour::withAlpha(colour::reese, 0.24f * alpha));
    canvas.circle(b2x - r2 * 2.0f, b2y - r2 * 2.0f, r2 * 4.0f);
    canvas.setColor(tintWhite(colour::reese, 0.35f, 0.88f * alpha));
    canvas.circle(b2x - r2, b2y - r2, 2.0f * r2);
}

// 1. REESE — 7 Braided Bioluminescent Plasma Unison Ribbons + Sync Arcs + Sub Spine
void drawReeseWorld(visage::Canvas& canvas, float x, float y, float w, float h, const StrataMotion& m, float scale,
                    float alpha) {
    const unsigned col = colour::padAccent(1); // Glacial Cyan
    const float energy = std::clamp(m.rms * 1.45f + m.width * 0.30f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float cy = h * 0.50f;
    const float spreadH = h * (0.14f + 0.26f * m.width);

    // Central emerald Sub-Anchor spine running straight and rock-solid through the center
    const float subGlowH = (12.0f + 22.0f * m.subPeak) * scale;
    canvas.setColor(colour::withAlpha(colour::sub, (0.10f + 0.14f * m.subPeak) * alpha));
    canvas.rectangle(x, y + cy - subGlowH, w, subGlowH * 2.0f);
    canvas.setColor(colour::withAlpha(colour::sub, (0.55f + 0.35f * m.subPeak) * alpha));
    canvas.rectangle(x, y + cy - 1.2f * scale, w, 2.4f * scale);

    // 7 braided unison plasma ribbons driven by real live voicePhases[0..6]
    constexpr int kSteps = 110;
    constexpr std::array<unsigned, 7> kStrandCols{
        colour::abyss, colour::reese, colour::sub, 0xffffffffu, colour::sub, colour::reese, colour::strata};

    for (int v = 0; v < 7; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const float normV = (static_cast<float>(v) - 3.0f) / 3.0f; // -1..+1
        const float freqMult = 1.6f + 0.42f * static_cast<float>(std::abs(v - 3)) + 1.8f * m.tone;
        const float vPhase = m.voicePhases[vi] * kTwoPi + m.phase * (1.2f + 0.35f * normV);
        const unsigned sCol = kStrandCols[vi];

        visage::Path strand;
        for (int i = 0; i <= kSteps; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kSteps);
            const float env = std::sin(kPi * u);
            const float pull = std::exp(-0.5f * std::pow((u - m.x) / 0.22f, 2.0f));
            const float beat = std::sin(u * kTwoPi * freqMult + vPhase);
            const float syncHarm = 0.28f * m.tone * std::sin(u * kTwoPi * (freqMult * 2.5f) - vPhase * 1.3f);
            const float sy = cy + normV * spreadH * (0.45f + 0.55f * env) +
                             (beat + syncHarm) * (24.0f + 38.0f * energy + 28.0f * pull) * scale * env;
            const float sx = u * w;
            if (i == 0)
                strand.moveTo(sx, sy);
            else
                strand.lineTo(sx, sy);
        }

        canvas.setColor(colour::withAlpha(sCol, (0.14f + 0.12f * energy) * alpha));
        canvas.stroke(strand, x, y, w, h, (v == 3 ? 6.0f : 4.2f) * scale);
        canvas.setColor(tintWhite(sCol, v == 3 ? 0.55f : 0.25f, (v == 3 ? 0.92f : 0.68f) * alpha));
        canvas.stroke(strand, x, y, w, h, (v == 3 ? 2.4f : 1.55f) * scale);
    }

    // Interference resonance nodes flashing along the X focal column
    constexpr int kNodes = 5;
    for (int k = 0; k < kNodes; ++k) {
        const float u = 0.16f + 0.68f * (static_cast<float>(k) / static_cast<float>(kNodes - 1));
        const float nx = x + u * w;
        const float beat = 0.5f + 0.5f * std::sin(m.phase * 2.4f + static_cast<float>(k) * 1.4f);
        const float ny = y + cy + std::sin(u * kTwoPi * 2.0f + m.phase) * spreadH * 0.6f * (m.y - 0.5f) * 2.0f;
        const float r = (4.5f + 5.5f * beat * (0.4f + 0.6f * energy)) * scale;
        canvas.setColor(colour::withAlpha(col, 0.24f * beat * alpha));
        canvas.circle(nx - r * 2.2f, ny - r * 2.2f, r * 4.4f);
        canvas.setColor(tintWhite(col, 0.5f, (0.45f + 0.50f * beat) * alpha));
        canvas.circle(nx - r, ny - r, 2.0f * r);
    }
}

// 2. STRATA — 3D Luminous Geological Waveform Strata & Gen-2 Laser Scrub Aperture
void drawStrataWorld(visage::Canvas& canvas, float x, float y, float w, float h, const StrataMotion& m, float scale,
                     float alpha) {
    const unsigned col = colour::padAccent(2); // Phosphor Magenta
    const float energy = std::clamp(m.rms * 1.4f + m.genMix * 0.4f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float padX = 28.0f * scale;
    const float waveW = std::max(20.0f, w - 2.0f * padX);
    const float loopX0 = x + padX + m.loopStart * waveW;
    const float loopX1 = x + padX + m.loopEnd * waveW;

    // Translucent glowing Gen-2 active loop aperture window
    canvas.setColor(colour::withAlpha(col, (0.08f + 0.08f * m.genMix) * alpha));
    canvas.rectangle(loopX0, y + 18.0f * scale, std::max(2.0f, loopX1 - loopX0), h - 36.0f * scale);
    canvas.setColor(colour::withAlpha(col, 0.45f * alpha));
    canvas.rectangle(loopX0, y + 18.0f * scale, 1.5f * scale, h - 36.0f * scale);
    canvas.rectangle(loopX1 - 1.5f * scale, y + 18.0f * scale, 1.5f * scale, h - 36.0f * scale);

    // Layer A (Bottom): Emerald Sub-Floor sedimentary sine strata
    {
        const float subY = h * 0.80f;
        visage::Path p;
        constexpr int kPts = 84;
        for (int i = 0; i <= kPts; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kPts);
            const float sy = subY + std::sin(u * kTwoPi * 3.0f - m.phase * 1.8f) * (10.0f + 16.0f * m.subPeak) * scale;
            if (i == 0)
                p.moveTo(padX + u * waveW, sy);
            else
                p.lineTo(padX + u * waveW, sy);
        }
        canvas.setColor(colour::withAlpha(colour::sub, 0.22f * alpha));
        canvas.stroke(p, x, y, w, h, 4.5f * scale);
        canvas.setColor(tintWhite(colour::sub, 0.25f, 0.78f * alpha));
        canvas.stroke(p, x, y, w, h, 1.8f * scale);
    }

    // Layer B (Middle): Glacial Cyan Gen-1 Live Oscilloscope Stratum
    {
        const float midY = h * 0.58f;
        visage::Path p;
        for (std::size_t i = 0; i < TelemetrySnapshot::kScopePoints; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(TelemetrySnapshot::kScopePoints - 1);
            const float sy = midY - m.scope[i] * h * 0.14f;
            if (i == 0)
                p.moveTo(padX + u * waveW, sy);
            else
                p.lineTo(padX + u * waveW, sy);
        }
        canvas.setColor(colour::withAlpha(colour::reese, 0.22f * alpha));
        canvas.stroke(p, x, y, w, h, 4.5f * scale);
        canvas.setColor(tintWhite(colour::reese, 0.22f, (0.45f + 0.45f * (1.0f - m.genMix * 0.5f)) * alpha));
        canvas.stroke(p, x, y, w, h, 1.8f * scale);
    }

    // Layer C (Foreground Hero): Sculptural Printed Gen-2 Waveform Strata (symmetric luminous canyon!)
    {
        const float topY = h * 0.34f;
        const float maxAmp = h * 0.20f;
        constexpr int kN = PrintBuffer::kWaveformPoints;
        visage::Path upper, lower;
        for (int i = 0; i < kN; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kN - 1);
            const float pk = m.hasPrint ? m.printPeaks[static_cast<std::size_t>(i)]
                                        : (0.14f + 0.12f * std::abs(std::sin(u * 15.0f + m.phase * 0.5f)));
            const bool inside = (u >= m.loopStart && u <= m.loopEnd);
            const float amp = pk * maxAmp * (inside ? 1.0f : 0.42f);
            const float sx = padX + u * waveW;
            if (i == 0) {
                upper.moveTo(sx, topY - amp);
                lower.moveTo(sx, topY + amp);
            } else {
                upper.lineTo(sx, topY - amp);
                lower.lineTo(sx, topY + amp);
            }
            // Crisp vertical mineral strata bars inside active loop
            if ((i & 1) == 0) {
                const unsigned barCol = inside ? colour::lerpArgb(col, colour::sculpt, pk * 0.45f)
                                               : colour::withAlpha(col, 0.25f);
                canvas.setColor(colour::withAlpha(barCol, (inside ? (0.28f + 0.42f * pk) : 0.14f) * alpha));
                canvas.rectangle(x + sx, y + topY - amp, std::max(1.2f * scale, waveW / static_cast<float>(kN) * 1.1f),
                                 std::max(1.5f * scale, 2.0f * amp));
            }
        }
        canvas.setColor(colour::withAlpha(col, 0.26f * alpha));
        canvas.stroke(upper, x, y, w, h, 4.4f * scale);
        canvas.stroke(lower, x, y, w, h, 4.4f * scale);
        canvas.setColor(tintWhite(col, 0.42f, 0.94f * alpha));
        canvas.stroke(upper, x, y, w, h, 1.9f * scale);
        canvas.stroke(lower, x, y, w, h, 1.6f * scale);
    }

    // Vertical luminous playhead laser cutting across all 3 strata layers
    const float phX = x + padX + m.playheadNorm * waveW;
    canvas.setColor(colour::withAlpha(col, 0.28f * alpha));
    canvas.rectangle(phX - 4.0f * scale, y + 14.0f * scale, 8.0f * scale, h - 28.0f * scale);
    canvas.setColor(colour::withAlpha(0xffffffffu, 0.90f * alpha));
    canvas.rectangle(phX - 1.0f * scale, y + 14.0f * scale, 2.0f * scale, h - 28.0f * scale);
}

// 3. DISPERSE — Fluid Elastic Phase Caustics & Chromatic Allpass Refraction
void drawDisperseWorld(visage::Canvas& canvas, float x, float y, float w, float h, const StrataMotion& m, float scale,
                       float alpha) {
    const unsigned col = colour::padAccent(3); // Sub Emerald
    const float energy = std::clamp(m.rms * 1.4f + m.disperse * 0.45f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float cx = w * (0.25f + 0.50f * m.x);
    const float cy = h * (0.65f - 0.30f * m.y);
    const float maxR = std::hypot(w, h) * 0.56f;

    constexpr int kRings = 14;
    constexpr int kSegs = 80;
    const float flow = std::fmod(std::fmod(m.phase * 0.42f, 1.0f) + 1.0f, 1.0f);

    for (int r = 0; r < kRings; ++r) {
        const float u = std::fmod(static_cast<float>(r) / static_cast<float>(kRings) + flow, 1.0f);
        const float rad = (22.0f + u * maxR);
        const float fade = std::sin(kPi * u);
        // Chromatic dispersion shift: emerald -> cyan -> amber as group delay stretches!
        const unsigned rCol = u < 0.5f ? colour::lerpArgb(col, colour::reese, u * 2.0f)
                                       : colour::lerpArgb(colour::reese, colour::sculpt, (u - 0.5f) * 2.0f);

        visage::Path caustic;
        for (int s = 0; s <= kSegs; ++s) {
            const float ang = (static_cast<float>(s) / static_cast<float>(kSegs)) * kTwoPi;
            const float smear = (0.04f + 0.22f * m.disperse) *
                                std::sin(4.0f * ang - m.phase * 2.0f + u * 8.0f * (0.5f + m.density));
            const float warp2 = 0.06f * m.density * std::cos(7.0f * ang + m.phase * 1.5f);
            const float rr = rad * (1.0f + smear + warp2);
            const float sx = cx + std::cos(ang) * rr * 1.24f;
            const float sy = cy + std::sin(ang) * rr * 0.72f;
            if (s == 0)
                caustic.moveTo(sx, sy);
            else
                caustic.lineTo(sx, sy);
        }
        canvas.setColor(colour::withAlpha(rCol, (0.14f + 0.52f * fade) * alpha));
        canvas.stroke(caustic, x, y, w, h, (1.1f + 1.6f * fade) * scale);
    }

    // Central prism lens
    const float coreR = (18.0f + 14.0f * energy) * scale;
    canvas.setColor(colour::withAlpha(col, 0.26f * alpha));
    canvas.circle(x + cx - coreR * 2.0f, y + cy - coreR * 2.0f, coreR * 4.0f);
    canvas.setColor(tintWhite(col, 0.45f, 0.90f * alpha));
    canvas.ring(x + cx - coreR, y + cy - coreR, 2.0f * coreR, 2.2f * scale);
}

// 4. ABYSS — Sub-Harmonic Gravitational Core & Phase-Locked Warmth Halos
void drawAbyssWorld(visage::Canvas& canvas, float x, float y, float w, float h, const StrataMotion& m, float scale,
                    float alpha) {
    const unsigned col = colour::padAccent(4); // Deep Violet
    const float energy = std::clamp(m.rms * 1.25f + m.subPeak * 0.65f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float cx = x + w * (0.35f + 0.30f * m.x);
    const float cy = y + h * (0.58f - 0.16f * m.y);
    const float minDim = std::min(w, h);

    // 10 deep sub-pressure concentric gravity rings
    constexpr int kRings = 10;
    for (int i = 0; i < kRings; ++i) {
        const float u = static_cast<float>(i + 1) / static_cast<float>(kRings);
        const float pulse = 1.0f + 0.08f * m.subPeak * std::sin(m.phase * 2.6f - u * 5.0f);
        const float rx = minDim * (0.10f + 0.48f * u) * pulse * 1.35f;
        const float ry = minDim * (0.10f + 0.48f * u) * pulse * 0.78f;
        const unsigned rCol = u < 0.42f ? colour::lerpArgb(colour::sub, col, u / 0.42f) : col;
        visage::Path ell;
        constexpr int kSegs = 64;
        for (int s = 0; s <= kSegs; ++s) {
            const float a = (static_cast<float>(s) / static_cast<float>(kSegs)) * kTwoPi;
            const float px = (cx - x) + std::cos(a) * rx;
            const float py = (cy - y) + std::sin(a) * ry;
            if (s == 0)
                ell.moveTo(px, py);
            else
                ell.lineTo(px, py);
        }
        canvas.setColor(colour::withAlpha(rCol, (0.10f + 0.44f * (1.0f - u * 0.65f)) * alpha));
        canvas.stroke(ell, x, y, w, h, (1.0f + 1.8f * (1.0f - u)) * scale);
    }

    // 2nd & 3rd Harmonic Speaker-Warmth Orbitals (glow warmer amber as subWarmth increases!)
    for (int hOrd = 2; hOrd <= 3; ++hOrd) {
        const float ang = m.phase * static_cast<float>(hOrd) * 0.9f;
        const float rad = minDim * (hOrd == 2 ? 0.24f : 0.36f);
        const float ox = cx + std::cos(ang) * rad * 1.3f;
        const float oy = cy + std::sin(ang) * rad * 0.75f;
        const float orR = (5.0f + 7.5f * m.subWarmth) * scale;
        canvas.setColor(colour::withAlpha(colour::sculpt, (0.18f + 0.36f * m.subWarmth) * alpha));
        canvas.circle(ox - orR * 2.2f, oy - orR * 2.2f, orR * 4.4f);
        canvas.setColor(tintWhite(colour::sculpt, 0.35f, (0.35f + 0.60f * m.subWarmth) * alpha));
        canvas.circle(ox - orR, oy - orR, 2.0f * orR);
    }

    // Emerald Sub-Anchor Singularity Center
    const float coreR = (22.0f + 18.0f * m.subPeak) * scale;
    canvas.setColor(colour::withAlpha(colour::sub, (0.24f + 0.26f * m.subPeak) * alpha));
    canvas.circle(cx - coreR * 1.7f, cy - coreR * 1.7f, coreR * 3.4f);
    canvas.setColor(colour::withAlpha(0xff05070cu, 0.94f * alpha));
    canvas.circle(cx - coreR, cy - coreR, 2.0f * coreR);
    canvas.setColor(tintWhite(colour::sub, 0.35f, 0.94f * alpha));
    canvas.ring(cx - coreR, cy - coreR, 2.0f * coreR, 2.6f * scale);
}

} // namespace

void drawStrataArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, const StrataMotion& m,
                   float scale, float alpha) {
    if (w <= 2.0f || h <= 2.0f || alpha <= 0.004f)
        return;
    canvas.saveState();
    canvas.trimClampBounds(x, y, w, h);
    switch (std::clamp(program, 0, kPadProgramCount - 1)) {
    case 0:
        drawSculptWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 1:
        drawReeseWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 2:
        drawStrataWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 3:
        drawDisperseWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 4:
        drawAbyssWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    }
    canvas.restoreState();
}

} // namespace ln::ui
