// Design tokens. Every colour, size and font in the interface comes from here.
#pragma once

#include "tf/ui/model.h"

#include <embedded/tf_fonts.h>
#include <visage/graphics.h>

#include <vector>

namespace tf::ui {

// Frames that need a look at the model every frame.
struct Live {
    virtual ~Live() = default;
    virtual void tick() = 0;
};

// Shared by every frame. Layout is authored at 1280x720 and multiplied by `scale`.
struct Context {
    explicit Context(Model& m) : model(m) {}

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;

    float px(float designPixels) const { return designPixels * scale; }
};

namespace colour {
// Surfaces, darkest first.
inline constexpr unsigned window = 0xff0a0d12;
inline constexpr unsigned panel = 0xff10151c;
inline constexpr unsigned raised = 0xff161d26;
inline constexpr unsigned edge = 0xff1e2732;
inline constexpr unsigned track = 0xff26313e;

inline constexpr unsigned text = 0xffe6ebf0;
inline constexpr unsigned textDim = 0xff8a95a3;
inline constexpr unsigned textFaint = 0xff566170;

// Each hue means one thing.
inline constexpr unsigned master = 0xff3fd9e8;  // the master sine and the sub: the clean path
inline constexpr unsigned growl = 0xfff5a742;   // reese, tract, fold: the processed path
inline constexpr unsigned motion = 0xffa98bff;  // movement lanes and what they modulate
inline constexpr unsigned alert = 0xffff5d5d;   // limiter working, clipping

constexpr unsigned withAlpha(unsigned argb, float alpha) {
    return (argb & 0x00ffffffu) | (static_cast<unsigned>(alpha * 255.0f + 0.5f) << 24);
}
}

namespace metric {
inline constexpr float margin = 12.0f;      // window edge to panels, and between panels
inline constexpr float pad = 12.0f;         // panel edge to content
inline constexpr float gap = 8.0f;          // between controls
inline constexpr float radius = 10.0f;      // panel corners
inline constexpr float radiusSmall = 6.0f;  // nodes, buttons, chips
inline constexpr float row = 22.0f;         // one label/value row
inline constexpr float hairline = 1.0f;
inline constexpr float stroke = 1.5f;       // wires and curves
}

enum class Weight { regular, medium, semibold };

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    const auto& file = weight == Weight::semibold ? fonts::Inter_SemiBold_ttf
                     : weight == Weight::medium   ? fonts::Inter_Medium_ttf
                                                  : fonts::Inter_Regular_ttf;
    return visage::Font(context.px(designSize), file);
}

namespace type {
inline constexpr float label = 11.5f;  // labels and values
inline constexpr float title = 12.0f;  // panel and node titles
inline constexpr float small = 10.0f;  // axis ticks
inline constexpr float wordmark = 17.0f;
}

// Panel chrome: a filled, hairline-edged rounded rectangle.
inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    const float radius = context.px(designRadius);
    canvas.setColor(fill);
    canvas.roundedRectangle(x, y, width, height, radius);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(x, y, width, height, radius, context.px(metric::hairline));
}

}
