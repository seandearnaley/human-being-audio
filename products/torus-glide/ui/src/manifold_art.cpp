// SPDX-License-Identifier: AGPL-3.0-or-later
#include "manifold_art.h"

#include "theme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tg::ui {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline unsigned tone(unsigned base, float lightness, float alpha) {
    return colour::withAlpha(colour::scaleRgb(base, lightness), std::clamp(alpha, 0.0f, 1.0f));
}

inline unsigned tintWhite(unsigned base, float mixW, float alpha) {
    return colour::withAlpha(colour::lerpArgb(base, 0xffffffffu, std::clamp(mixW, 0.0f, 1.0f)),
                             std::clamp(alpha, 0.0f, 1.0f));
}

// Rich deep obsidian-tinted backdrop tailored to each jewel program so luminous geometry pops like glowing hardware.
void drawAtmosphere(visage::Canvas& canvas, float x, float y, float w, float h, unsigned accent, float px, float py,
                    float energy, float alpha) {
    const unsigned bg = colour::withAlpha(colour::lerpArgb(0xff06090fu, colour::scaleRgb(accent, 0.13f), 0.72f), alpha);
    canvas.setColor(bg);
    canvas.rectangle(x, y, w, h);

    // Soft radial aura tracking the live X/Y coordinate
    const float gx = x + px * w;
    const float gy = y + (1.0f - py) * h;
    const float baseR = std::max(w, h) * (0.42f + 0.14f * energy);
    for (int i = 3; i >= 0; --i) {
        const float r = baseR * (0.28f + 0.24f * static_cast<float>(i));
        const float a = (0.048f + 0.028f * energy) * (1.0f - 0.18f * static_cast<float>(i)) * alpha;
        canvas.setColor(colour::withAlpha(accent, a));
        canvas.circle(gx - r, gy - r, 2.0f * r);
    }

    // Subtle precision coordinate hairline grid
    constexpr int kCols = 12;
    constexpr int kRows = 6;
    canvas.setColor(colour::withAlpha(accent, 0.045f * alpha));
    for (int c = 1; c < kCols; ++c) {
        const float lx = x + (static_cast<float>(c) / static_cast<float>(kCols)) * w;
        canvas.rectangle(lx, y, 1.0f, h);
    }
    for (int r = 1; r < kRows; ++r) {
        const float ly = y + (static_cast<float>(r) / static_cast<float>(kRows)) * h;
        canvas.rectangle(x, ly, w, 1.0f);
    }
}

// 0. SPIRAL — 3D Perspective-Projected Rotating Torus Helix + 7 Glowing Shepard Octave Nodes
void drawSpiralWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                     float alpha) {
    const unsigned col = colour::padAccent(0);
    const float energy = std::clamp(m.rms * 1.4f + m.padAmt * 0.35f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float cx = w * 0.5f;
    const float cy = h * 0.50f;
    const float minDim = std::min(w, h);
    const float majorR = minDim * (0.36f + 0.08f * m.y);
    const float minorR = minDim * (0.14f + 0.07f * m.y + 0.02f * energy);
    const float tiltX = 0.52f + (m.y - 0.5f) * 0.65f;
    const float tiltZ = (m.x - 0.5f) * 0.55f;
    const float cosTX = std::cos(tiltX);
    const float sinTX = std::sin(tiltX);
    const float cosTZ = std::cos(tiltZ);
    const float sinTZ = std::sin(tiltZ);
    const float camD = minDim * 1.95f;

    const auto project = [&](float u, float v, float& sx, float& sy, float& depth) {
        const float rr = majorR + minorR * std::cos(v);
        const float px3 = rr * std::cos(u);
        const float py3 = rr * std::sin(u);
        const float pz3 = minorR * std::sin(v);
        // Rotate around X then Z
        const float ry = py3 * cosTX - pz3 * sinTX;
        const float rz = py3 * sinTX + pz3 * cosTX;
        const float rx2 = px3 * cosTZ - ry * sinTZ;
        const float ry2 = px3 * sinTZ + ry * cosTZ;
        const float persp = camD / std::max(minDim * 0.5f, camD - rz);
        sx = cx + rx2 * persp * 1.18f;
        sy = cy + ry2 * persp * 0.92f;
        depth = std::clamp((rz + majorR + minorR) / (2.0f * (majorR + minorR)), 0.0f, 1.0f);
    };

    // Subtle glowing meridian rings sculpting the torus volume
    constexpr int kRings = 16;
    constexpr int kRingPts = 28;
    const float spinU = m.phase * 0.45f;
    for (int r = 0; r < kRings; ++r) {
        const float u = (static_cast<float>(r) / static_cast<float>(kRings)) * kTwoPi + spinU;
        visage::Path ring;
        float depthSum = 0.0f;
        for (int p = 0; p <= kRingPts; ++p) {
            const float v = (static_cast<float>(p) / static_cast<float>(kRingPts)) * kTwoPi;
            float sx = 0.0f, sy = 0.0f, d = 0.0f;
            project(u, v, sx, sy, d);
            depthSum += d;
            if (p == 0)
                ring.moveTo(sx, sy);
            else
                ring.lineTo(sx, sy);
        }
        const float avgD = depthSum / static_cast<float>(kRingPts + 1);
        canvas.setColor(tone(col, 0.55f + 0.55f * avgD, (0.09f + 0.16f * avgD) * alpha));
        canvas.stroke(ring, x, y, w, h, (0.9f + 0.8f * avgD) * scale);
    }

    // 3 braided luminous Shepard pitch helices coiling endlessly around the torus!
    constexpr int kHelices = 3;
    constexpr int kHelixSteps = 160;
    for (int hIdx = 0; hIdx < kHelices; ++hIdx) {
        visage::Path helix;
        const float phaseOff = static_cast<float>(hIdx) * (kTwoPi / static_cast<float>(kHelices));
        for (int s = 0; s <= kHelixSteps; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(kHelixSteps);
            const float u = t * kTwoPi * 2.0f + spinU + phaseOff;
            const float v = t * kTwoPi * 7.0f + m.spiralPos * kTwoPi + phaseOff;
            float sx = 0.0f, sy = 0.0f, d = 0.0f;
            project(u, v, sx, sy, d);
            if (s == 0)
                helix.moveTo(sx, sy);
            else
                helix.lineTo(sx, sy);
        }
        const bool primary = (hIdx == 0);
        canvas.setColor(primary ? tintWhite(col, 0.25f, 0.78f * alpha) : tone(col, 0.85f, 0.34f * alpha));
        canvas.stroke(helix, x, y, w, h, (primary ? 2.4f : 1.4f) * scale);
    }

    // Central emerald Sub-Anchor Floor core pulsing with live subPeak!
    const float subR = minDim * (0.09f + 0.055f * std::clamp(m.subPeak, 0.0f, 1.0f));
    canvas.setColor(colour::withAlpha(colour::mobius, (0.14f + 0.18f * m.subPeak) * alpha));
    canvas.circle(x + cx - subR * 1.45f, y + cy - subR * 1.45f, subR * 2.9f);
    canvas.setColor(colour::withAlpha(colour::mobius, (0.58f + 0.32f * m.subPeak) * alpha));
    canvas.ring(x + cx - subR, y + cy - subR, 2.0f * subR, 2.0f * scale);

    // 7 live glowing Shepard-Risset octave partial orbs riding the primary helix
    for (int k = 0; k < kOctaveCount; ++k) {
        const auto ki = static_cast<std::size_t>(k);
        const float uFrac = (m.partialGain[ki] > 1e-4f)
                                ? (m.partialU[ki] / static_cast<float>(kOctaveCount))
                                : std::fmod(std::fmod((static_cast<float>(k) + m.spiralPos) / 7.0f, 1.0f) + 1.0f, 1.0f);
        const float gain = (m.partialGain[ki] > 1e-4f) ? m.partialGain[ki]
                                                       : std::pow(std::sin(kPi * std::clamp(uFrac, 0.0f, 1.0f)), 2.0f);
        const float u = uFrac * kTwoPi * 2.0f + spinU;
        const float v = uFrac * kTwoPi * 7.0f + m.spiralPos * kTwoPi;
        float sx = 0.0f, sy = 0.0f, d = 0.0f;
        project(u, v, sx, sy, d);
        const bool inSub = (uFrac >= 0.08f && uFrac <= 0.30f);
        const unsigned orbCol = inSub ? colour::mobius : col;
        const float r = (4.0f + 9.5f * gain * (0.65f + 0.45f * d)) * scale;
        canvas.setColor(colour::withAlpha(orbCol, (0.20f + 0.35f * gain) * alpha));
        canvas.circle(x + sx - r * 1.85f, y + sy - r * 1.85f, r * 3.7f);
        canvas.setColor(tintWhite(orbCol, 0.45f * gain, (0.45f + 0.55f * gain) * alpha));
        canvas.circle(x + sx - r, y + sy - r, 2.0f * r);
    }
}

// 1. MÖBIUS — Twisted 3D Lemniscate Infinity Ribbon with 5 Orbiting Voice Beads
void drawMobiusWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                     float alpha) {
    const unsigned col = colour::padAccent(1);
    const float energy = std::clamp(m.rms * 1.4f + m.padAmt * 0.35f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float cx = w * 0.5f;
    const float cy = h * 0.50f;
    const float rx = w * 0.38f;
    const float ry = h * (0.27f + 0.08f * m.y);
    const float bandHalf = std::min(w, h) * (0.065f + 0.045f * m.y);
    const float twistShift = m.phase * 0.55f + (m.x - 0.5f) * 1.8f;

    const auto mobiusPt = [&](float t, float sNorm, float& pxOut, float& pyOut, float& depth) {
        // Lemniscate figure-8 spine with genuine Mobius half-twist normal!
        const float denom = 1.0f + std::sin(t) * std::sin(t);
        const float baseX = rx * std::cos(t) / denom * 1.06f;
        const float baseY = ry * std::sin(t) * std::cos(t) / denom * 2.05f;
        const float twist = 0.5f * t + twistShift;
        const float nx = -std::sin(t) * 0.35f;
        const float ny = std::cos(t);
        const float off = sNorm * bandHalf * std::cos(twist);
        pxOut = cx + baseX + nx * off;
        pyOut = cy + baseY + ny * off;
        depth = 0.5f + 0.5f * std::sin(twist) * sNorm;
    };

    // Draw 44 illuminated Mobius cross-rungs showing the surface half-twist
    constexpr int kRungs = 48;
    for (int i = 0; i < kRungs; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(kRungs)) * kTwoPi;
        float x0 = 0.0f, y0 = 0.0f, d0 = 0.0f;
        float x1 = 0.0f, y1 = 0.0f, d1 = 0.0f;
        mobiusPt(t, -1.0f, x0, y0, d0);
        mobiusPt(t, 1.0f, x1, y1, d1);
        const float pulse = 0.5f + 0.5f * std::cos(t - m.morphPos * kTwoPi);
        canvas.setColor(tone(col, 0.65f + 0.45f * pulse, (0.14f + 0.26f * pulse) * alpha));
        canvas.segment(x + x0, y + y0, x + x1, y + y1, (1.1f + 0.8f * pulse) * scale, true);
    }

    // Draw 5 glowing parallel voice strands weaving continuously along the twisted Mobius strip
    constexpr int kSteps = 140;
    for (int v = 0; v < kVoiceCount; ++v) {
        const float sNorm = -1.0f + 2.0f * (static_cast<float>(v) / static_cast<float>(kVoiceCount - 1));
        const unsigned vCol = colour::voices[static_cast<std::size_t>(v)];
        visage::Path strand;
        for (int i = 0; i <= kSteps; ++i) {
            const float t = (static_cast<float>(i) / static_cast<float>(kSteps)) * kTwoPi;
            float sx = 0.0f, sy = 0.0f, d = 0.0f;
            mobiusPt(t, sNorm, sx, sy, d);
            if (i == 0)
                strand.moveTo(sx, sy);
            else
                strand.lineTo(sx, sy);
        }
        canvas.setColor(colour::withAlpha(vCol, (v == 0 || v == 4 ? 0.82f : 0.58f) * alpha));
        canvas.stroke(strand, x, y, w, h, (v == 0 || v == 4 ? 2.2f : 1.5f) * scale);

        // Orbiting glowing voice pitch bead on each strand
        const float beadT = m.morphPos * kTwoPi + static_cast<float>(v) * 0.24f;
        float bx = 0.0f, by = 0.0f, bd = 0.0f;
        mobiusPt(beadT, sNorm, bx, by, bd);
        const float r = (5.5f + 3.5f * energy + 2.0f * bd) * scale;
        canvas.setColor(colour::withAlpha(vCol, 0.26f * alpha));
        canvas.circle(x + bx - r * 2.0f, y + by - r * 2.0f, r * 4.0f);
        canvas.setColor(tintWhite(vCol, 0.40f, 0.96f * alpha));
        canvas.circle(x + bx - r, y + by - r, 2.0f * r);
    }
}

// 2. COPEDENT — 5-String Pedal-Steel Tension Harp & Magnetic Scale-Gravity Rails
void drawCopedentWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                       float alpha) {
    const unsigned col = colour::padAccent(2);
    const float energy = std::clamp(m.rms * 1.4f + m.padAmt * 0.35f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    // 4 Scene columns (A, B, C, D) with subtle magnetic pillars
    for (int s = 0; s < kSceneCount; ++s) {
        const float sx = x + w * (0.12f + 0.76f * (static_cast<float>(s) / static_cast<float>(kSceneCount - 1)));
        const float dist = std::abs(m.morphPos * 3.0f - static_cast<float>(s));
        const float lit = std::clamp(1.0f - dist, 0.0f, 1.0f);
        canvas.setColor(colour::withAlpha(col, (0.08f + 0.24f * lit) * alpha));
        canvas.rectangle(sx - 0.75f * scale, y + 18.0f * scale, 1.5f * scale, h - 36.0f * scale);
    }

    // Horizontal magnetic scale-gravity rails
    constexpr int kRails = 11;
    for (int r = 0; r < kRails; ++r) {
        const float ry = y + h * (0.12f + 0.76f * (static_cast<float>(r) / static_cast<float>(kRails - 1)));
        canvas.setColor(colour::withAlpha(col, (0.05f + 0.08f * m.y) * alpha));
        canvas.rectangle(x + 24.0f * scale, ry, w - 48.0f * scale, 1.0f * scale);
    }

    // 5 luminous pedal-steel voice strings bending fluidly across the playhead & X/Y puck
    const float playX = w * (0.12f + 0.76f * std::clamp(m.morphPos, 0.0f, 1.0f));
    constexpr int kPts = 80;
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const unsigned vCol = colour::voices[vi];
        const float baseNorm = 0.80f - 0.62f * (static_cast<float>(v) / static_cast<float>(kVoiceCount - 1));
        const float pitchShift = std::clamp((m.voicePitches[vi] - 41.0f) / 30.0f, -0.35f, 0.85f);
        const float anchorY = h * std::clamp(baseNorm - 0.14f * (pitchShift - 0.35f), 0.12f, 0.88f);

        // Luminous shadow echo + crisp primary string
        for (int pass = 0; pass < 2; ++pass) {
            visage::Path stringPath;
            const float staggerPhase = static_cast<float>(v) * (0.55f + 0.8f * m.y);
            for (int i = 0; i <= kPts; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kPts);
                const float sx = t * w;
                const float env = std::sin(kPi * t);
                const float pullDist = (t - m.x) / 0.24f;
                const float fingerPull = std::exp(-0.5f * pullDist * pullDist) * ((1.0f - m.y) * h - anchorY) * 0.32f;
                const float vib =
                    std::sin(t * kTwoPi * (2.0f + 0.5f * static_cast<float>(v)) - m.phase * 2.6f + staggerPhase) *
                    (8.0f + 16.0f * energy) * scale * env * (pass == 0 ? 1.55f : 0.65f);
                const float sy = std::clamp(anchorY + fingerPull * env + vib, 10.0f * scale, h - 10.0f * scale);
                if (i == 0)
                    stringPath.moveTo(sx, sy);
                else
                    stringPath.lineTo(sx, sy);
            }
            if (pass == 0) {
                canvas.setColor(colour::withAlpha(vCol, 0.24f * alpha));
                canvas.stroke(stringPath, x, y, w, h, 5.5f * scale);
            } else {
                canvas.setColor(tintWhite(vCol, 0.25f, 0.92f * alpha));
                canvas.stroke(stringPath, x, y, w, h, 2.2f * scale);
            }
        }

        // Glowing magnetic bridge bead at live morph position
        const float beadR = (6.0f + 3.0f * energy) * scale;
        canvas.setColor(colour::withAlpha(vCol, 0.28f * alpha));
        canvas.circle(x + playX - beadR * 1.9f, y + anchorY - beadR * 1.9f, beadR * 3.8f);
        canvas.setColor(tintWhite(vCol, 0.42f, 0.96f * alpha));
        canvas.circle(x + playX - beadR, y + anchorY - beadR, 2.0f * beadR);
    }

    // Playhead bar
    canvas.setColor(colour::withAlpha(0xffffffffu, 0.34f * alpha));
    canvas.rectangle(x + playX - 0.75f * scale, y + 12.0f * scale, 1.5f * scale, h - 24.0f * scale);
}

// 3. ABYSS — Gravitational Event Horizon & Sub-Anchor Singularity
void drawAbyssWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                    float alpha) {
    const unsigned col = colour::padAccent(3);
    const float energy = std::clamp(m.rms * 1.3f + m.subPeak * 0.55f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    const float wellX = w * (0.35f + 0.30f * m.x);
    const float wellY = h * (0.62f - 0.24f * m.y);
    const float maxR = std::hypot(w, h) * 0.58f;

    // 14 concentric warped spacetime rings plunging toward the Sub-Anchor singularity!
    constexpr int kRings = 14;
    constexpr int kSegs = 72;
    const float diveFlow = std::fmod(std::fmod(m.phase * 0.55f, 1.0f) + 1.0f, 1.0f);
    for (int r = 0; r < kRings; ++r) {
        const float rawT = std::fmod((static_cast<float>(r) / static_cast<float>(kRings)) + (1.0f - diveFlow), 1.0f);
        const float radius = maxR * std::pow(rawT, 1.45f) + 18.0f * scale;
        const float fade = std::sin(kPi * rawT);
        visage::Path ring;
        for (int s = 0; s <= kSegs; ++s) {
            const float ang = (static_cast<float>(s) / static_cast<float>(kSegs)) * kTwoPi;
            const float warp = 1.0f + (0.06f + 0.12f * m.y) * std::sin(3.0f * ang - m.phase * 1.8f + rawT * 6.0f);
            const float sx = wellX + std::cos(ang) * radius * warp * 1.15f;
            const float sy = wellY + std::sin(ang) * radius * warp * 0.72f;
            if (s == 0)
                ring.moveTo(sx, sy);
            else
                ring.lineTo(sx, sy);
        }
        const unsigned rCol = rawT < 0.38f ? colour::lerpArgb(colour::mobius, col, rawT / 0.38f) : col;
        canvas.setColor(colour::withAlpha(rCol, (0.14f + 0.54f * fade) * alpha));
        canvas.stroke(ring, x, y, w, h, (1.2f + 1.6f * (1.0f - rawT)) * scale);
    }

    // Radial gravitational lensing rays
    constexpr int kRays = 20;
    for (int i = 0; i < kRays; ++i) {
        const float ang = (static_cast<float>(i) / static_cast<float>(kRays)) * kTwoPi + m.phase * 0.18f;
        const float rIn = (28.0f + 10.0f * m.subPeak) * scale;
        const float rOut = maxR * 0.92f;
        canvas.setColor(colour::withAlpha(col, (0.07f + 0.10f * m.y) * alpha));
        canvas.segment(x + wellX + std::cos(ang) * rIn * 1.15f, y + wellY + std::sin(ang) * rIn * 0.72f,
                       x + wellX + std::cos(ang) * rOut * 1.15f, y + wellY + std::sin(ang) * rOut * 0.72f, 1.0f * scale,
                       true);
    }

    // Emerald-rimmed Sub-Anchor event horizon core
    const float coreR = (26.0f + 18.0f * m.subPeak + 10.0f * m.x) * scale;
    canvas.setColor(colour::withAlpha(colour::mobius, (0.24f + 0.22f * m.subPeak) * alpha));
    canvas.circle(x + wellX - coreR * 1.6f, y + wellY - coreR * 1.6f, coreR * 3.2f);
    canvas.setColor(colour::withAlpha(0xff05070cu, 0.95f * alpha));
    canvas.circle(x + wellX - coreR, y + wellY - coreR, 2.0f * coreR);
    canvas.setColor(tintWhite(colour::mobius, 0.32f, 0.92f * alpha));
    canvas.ring(x + wellX - coreR, y + wellY - coreR, 2.0f * coreR, 2.6f * scale);
}

// 4. RISSET — Accelerating Risset Chronometer Clockwork & Strobe Petals
void drawRissetWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                     float alpha) {
    const unsigned col = colour::padAccent(4);
    const float pulse = std::clamp(m.gateLevel, 0.15f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, pulse * 0.8f, alpha);

    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float maxR = std::min(w, h) * 0.46f;

    // 6 concentric logarithmic Risset chronometer rings rotating at 1x, 2x, 4x ratios
    constexpr int kRings = 6;
    for (int r = 0; r < kRings; ++r) {
        const float u = static_cast<float>(r + 1) / static_cast<float>(kRings);
        const float rad = maxR * (0.20f + 0.80f * u);
        const float speed = std::pow(1.45f, static_cast<float>(r)) * (0.35f + 1.15f * m.x);
        const float angBase = m.phase * speed * (r % 2 == 0 ? 1.0f : -1.0f);
        const int ticks = 8 + r * 6;
        canvas.setColor(colour::withAlpha(col, (0.14f + 0.22f * pulse) * alpha));
        canvas.ring(cx - rad, cy - rad, 2.0f * rad, (1.0f + 0.6f * m.y) * scale);

        for (int t = 0; t < ticks; ++t) {
            const float a = angBase + (static_cast<float>(t) / static_cast<float>(ticks)) * kTwoPi;
            const bool accentTick = (t % 4 == 0);
            const float len = (accentTick ? 16.0f : 8.0f) * (0.65f + 0.55f * pulse) * scale;
            const float r0 = rad - len * 0.45f;
            const float r1 = rad + len * 0.55f;
            canvas.setColor(accentTick ? tintWhite(col, 0.35f * pulse, (0.55f + 0.40f * pulse) * alpha)
                                       : tone(col, 0.85f, (0.22f + 0.25f * pulse) * alpha));
            canvas.segment(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * r1, cy + std::sin(a) * r1,
                           (accentTick ? 2.4f : 1.2f) * scale, true);
        }
    }

    // Sweeping logarithmic chronometer arms
    for (int arm = 0; arm < 2; ++arm) {
        const float a = m.barPhase * kTwoPi * (arm == 0 ? 1.0f : 2.0f) - 0.5f * kPi;
        const float rArm = maxR * (arm == 0 ? 0.96f : 0.68f);
        canvas.setColor(tintWhite(col, arm == 0 ? 0.55f : 0.25f, (0.75f * pulse + 0.15f) * alpha));
        canvas.segment(cx, cy, cx + std::cos(a) * rArm, cy + std::sin(a) * rArm, (arm == 0 ? 2.6f : 1.7f) * scale,
                       true);
    }

    // Center pulse hub
    const float hubR = (10.0f + 12.0f * pulse * (0.4f + 0.6f * m.x)) * scale;
    canvas.setColor(colour::withAlpha(col, 0.28f * pulse * alpha));
    canvas.circle(cx - hubR * 2.0f, cy - hubR * 2.0f, hubR * 4.0f);
    canvas.setColor(tintWhite(col, 0.50f, 0.94f * alpha));
    canvas.circle(cx - hubR * 0.55f, cy - hubR * 0.55f, hubR * 1.1f);
}

// 5. VOID — Manifold Interference Halo & Stereo Shimmer Lattice
void drawVoidWorld(visage::Canvas& canvas, float x, float y, float w, float h, const ManifoldMotion& m, float scale,
                   float alpha) {
    const unsigned col = colour::padAccent(5);
    const float energy = std::clamp(m.rms * 1.3f + m.x * 0.45f, 0.0f, 1.0f);
    drawAtmosphere(canvas, x, y, w, h, col, m.x, m.y, energy, alpha);

    // Twin stereo diffusion foci widened horizontally by Y (Detune & Width)
    const float spread = w * (0.10f + 0.24f * m.y);
    const float f1x = x + w * 0.5f - spread;
    const float f2x = x + w * 0.5f + spread;
    const float fy = y + h * (0.56f - 0.12f * m.y);
    const float maxR = std::max(w, h) * 0.58f;

    constexpr int kRipples = 10;
    const float flow = std::fmod(std::fmod(m.phase * 0.32f, 1.0f) + 1.0f, 1.0f);
    for (int i = 0; i < kRipples; ++i) {
        const float u = std::fmod((static_cast<float>(i) / static_cast<float>(kRipples)) + flow, 1.0f);
        const float rad = (20.0f + u * maxR * (0.55f + 0.45f * m.x));
        const float env = std::sin(kPi * u);
        const float a = (0.10f + 0.34f * env * (0.4f + 0.6f * m.x)) * alpha;
        const unsigned cL = colour::lerpArgb(col, colour::spiral, 0.32f * u);
        const unsigned cR = colour::lerpArgb(col, colour::copedent, 0.35f * u);
        canvas.setColor(colour::withAlpha(cL, a));
        canvas.ring(f1x - rad, fy - rad, 2.0f * rad, (1.1f + 1.1f * env) * scale);
        canvas.setColor(colour::withAlpha(cR, a));
        canvas.ring(f2x - rad, fy - rad, 2.0f * rad, (1.1f + 1.1f * env) * scale);
    }

    // Drifting luminous shimmer constellation nodes connected by subtle harmonic threads
    constexpr int kStars = 14;
    std::array<float, kStars> sx{};
    std::array<float, kStars> sy{};
    for (int i = 0; i < kStars; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const float fi = static_cast<float>(i);
        const float ang = fi * 2.399963f + m.phase * (0.16f + 0.03f * (fi - 7.0f));
        const float rad = std::min(w, h) * (0.14f + 0.34f * (fi / static_cast<float>(kStars))) * (0.65f + 0.45f * m.x);
        sx[ii] = x + w * 0.5f + std::cos(ang) * rad * 1.45f;
        sy[ii] = y + h * 0.5f + std::sin(ang) * rad * 0.82f;
    }

    for (int i = 0; i < kStars - 1; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        canvas.setColor(colour::withAlpha(col, (0.10f + 0.16f * m.x) * alpha));
        canvas.segment(sx[ii], sy[ii], sx[ii + 1], sy[ii + 1], 1.0f * scale, true);
    }
    for (int i = 0; i < kStars; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const float glow = 0.55f + 0.45f * std::sin(m.phase * 2.1f + static_cast<float>(i));
        const float r = (3.0f + 4.5f * glow * (0.4f + 0.6f * m.x)) * scale;
        const unsigned sc = colour::voices[ii % kVoiceCount];
        canvas.setColor(colour::withAlpha(sc, 0.24f * glow * alpha));
        canvas.circle(sx[ii] - r * 2.0f, sy[ii] - r * 2.0f, r * 4.0f);
        canvas.setColor(tintWhite(sc, 0.45f, (0.55f + 0.45f * glow) * alpha));
        canvas.circle(sx[ii] - r, sy[ii] - r, 2.0f * r);
    }
}
} // namespace

void drawManifoldArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, const ManifoldMotion& m,
                     float scale, float alpha) {
    if (w <= 2.0f || h <= 2.0f || alpha <= 0.004f)
        return;
    canvas.saveState();
    canvas.trimClampBounds(x, y, w, h);
    switch (std::clamp(program, 0, kPadProgramCount - 1)) {
    case 0:
        drawSpiralWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 1:
        drawMobiusWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 2:
        drawCopedentWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 3:
        drawAbyssWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 4:
        drawRissetWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    case 5:
        drawVoidWorld(canvas, x, y, w, h, m, scale, alpha);
        break;
    }
    canvas.restoreState();
}

} // namespace tg::ui
