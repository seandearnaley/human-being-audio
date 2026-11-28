// SPDX-License-Identifier: GPL-3.0-or-later
// Shared Human Being Audio Visage design tokens, typography, and drawing primitives.
#pragma once

#include <embedded/hba_fonts.h>
#include <visage/graphics.h>

#include <algorithm>
#include <span>
#include <string>

namespace hba::ui {

struct Live {
    virtual ~Live() = default;
    virtual void tick() = 0;
};

namespace colour {
inline constexpr unsigned window = 0xff0a0d12;
inline constexpr unsigned panel = 0xff10151c;
inline constexpr unsigned raised = 0xff161d26;
inline constexpr unsigned edge = 0xff1e2732;
inline constexpr unsigned track = 0xff26313e;

inline constexpr unsigned text = 0xffe6ebf0;
inline constexpr unsigned textDim = 0xff8a95a3;
inline constexpr unsigned textFaint = 0xff566170;

constexpr unsigned withAlpha(unsigned argb, float alpha) {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    return (argb & 0x00ffffffu) | (static_cast<unsigned>(a * 255.0f + 0.5f) << 24);
}
} // namespace colour

namespace metric {
inline constexpr float margin = 12.0f;
inline constexpr float pad = 12.0f;
inline constexpr float gap = 8.0f;
inline constexpr float radius = 10.0f;
inline constexpr float radiusSmall = 6.0f;
inline constexpr float row = 22.0f;
inline constexpr float hairline = 1.0f;
inline constexpr float stroke = 1.5f;
} // namespace metric

enum class Weight { regular, medium, semibold };

namespace type {
inline constexpr float label = 11.5f;
inline constexpr float title = 12.0f;
inline constexpr float small = 10.0f;
inline constexpr float wordmark = 17.0f;
} // namespace type

inline visage::Font fontPx(float pixelSize, Weight weight = Weight::regular) {
    const auto& file = weight == Weight::semibold ? fonts::Inter_SemiBold_ttf
                       : weight == Weight::medium ? fonts::Inter_Medium_ttf
                                                  : fonts::Inter_Regular_ttf;
    return visage::Font(pixelSize, file);
}

inline void drawPanel(visage::Canvas& canvas, float scale, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    const float radius = designRadius * scale;
    canvas.setColor(fill);
    canvas.roundedRectangle(x, y, width, height, radius);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(x, y, width, height, radius, metric::hairline * scale);
}

inline void drawLabel(visage::Canvas& canvas, float scale, const std::string& text, float x, float y, float width,
                      float height, unsigned colourArgb = colour::textDim,
                      visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
                      Weight weight = Weight::medium) {
    canvas.setColor(colourArgb);
    canvas.text(text, fontPx(designSize * scale, weight), justification, x, y, width, height);
}

inline void drawCurve(visage::Canvas& canvas, float scale, std::span<const float> values, float x, float y, float width,
                      float height, unsigned colourArgb, float low = -1.0f, float high = 1.0f, float fillAlpha = 0.0f,
                      float baseline = 0.0f, float designThickness = metric::stroke) {
    if (values.size() < 2 || width <= 0.0f || height <= 0.0f || !(high > low))
        return;

    const auto toY = [&](float value) {
        return height * (1.0f - std::clamp((value - low) / (high - low), 0.0f, 1.0f));
    };
    const float step = width / static_cast<float>(values.size() - 1);

    visage::Path line;
    line.moveTo(0.0f, toY(values[0]));
    for (std::size_t i = 1; i < values.size(); ++i)
        line.lineTo(step * static_cast<float>(i), toY(values[i]));

    if (fillAlpha > 0.0f) {
        visage::Path area = line;
        area.lineTo(width, toY(baseline));
        area.lineTo(0.0f, toY(baseline));
        area.close();
        canvas.setColor(colour::withAlpha(colourArgb, fillAlpha));
        canvas.fill(area, x, y, width, height);
    }

    canvas.setColor(colourArgb);
    canvas.stroke(line, x, y, width, height, designThickness * scale);
}

inline void drawChip(visage::Canvas& canvas, float scale, const std::string& text, float width, float height,
                     unsigned textArgb) {
    drawPanel(canvas, scale, 0.0f, 0.0f, width, height, colour::raised, metric::radiusSmall);
    drawLabel(canvas, scale, text, 0.0f, 0.0f, width, height, textArgb, visage::Font::kCenter, type::small);
}

} // namespace hba::ui
