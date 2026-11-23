// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline Visage 1.0 design tokens: dark studio slate chrome + signature 262°
// electric iris (phrase statement/structure), oscillograph cyan (answer & glide),
// splice vermilion (ratchets & microtiming shift), amber (mutation), emerald (turnaround).
#pragma once

#include "cl/ui/model.h"

#include <embedded/cl_fonts.h>
#include <visage/graphics.h>

#include <vector>

namespace cl::ui {

struct Live {
    virtual ~Live() = default;
    virtual void tick() = 0;
};

struct Context {
    explicit Context(Model& m) : model(m) {}

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;

    float px(float designPixels) const { return designPixels * scale; }
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

inline constexpr unsigned iris = 0xff9b7eff;      // 262° electric iris (statement & core accent)
inline constexpr unsigned cyan = 0xff3fd9e8;      // answer, glide curves & CC74
inline constexpr unsigned amber = 0xfff5a742;     // mutation & density
inline constexpr unsigned vermilion = 0xffff6e5e; // ratchet bursts & microtiming stems
inline constexpr unsigned emerald = 0xff49d193;   // turnaround & active audition

constexpr unsigned withAlpha(unsigned argb, float alpha) {
    return (argb & 0x00ffffffu) | (static_cast<unsigned>(alpha * 255.0f + 0.5f) << 24);
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

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    const auto& file = weight == Weight::semibold ? fonts::Inter_SemiBold_ttf
                       : weight == Weight::medium ? fonts::Inter_Medium_ttf
                                                  : fonts::Inter_Regular_ttf;
    return visage::Font(context.px(designSize), file);
}

namespace type {
inline constexpr float label = 11.5f;
inline constexpr float title = 12.0f;
inline constexpr float small = 10.0f;
inline constexpr float wordmark = 17.0f;
} // namespace type

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    const float radius = context.px(designRadius);
    canvas.setColor(fill);
    canvas.roundedRectangle(x, y, width, height, radius);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(x, y, width, height, radius, context.px(metric::hairline));
}

} // namespace cl::ui
