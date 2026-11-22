// Design tokens for Pole Field. Mirrors Tract Form's graphite dark chrome with
// Pole Field's signature phosphor magenta (poles), cyan (sub lock & zeros),
// amber (resonance & inter-stage drive), and violet (movement lanes).
#pragma once

#include "pf/ui/model.h"

#include <embedded/pf_fonts.h>
#include <visage/graphics.h>

#include <vector>

namespace pf::ui {

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

inline constexpr unsigned sub = 0xff3fd9e8;     // sub lock bypass & zeros
inline constexpr unsigned poles = 0xffe85bc4;   // 14-pole Z-plane morph & response
inline constexpr unsigned drive = 0xfff5a742;   // resonance & inter-stage ADAA drive
inline constexpr unsigned motion = 0xffa98bff;  // 4 tempo-locked movement lanes
inline constexpr unsigned alert = 0xffff5d5d;   // limiter / clipping

constexpr unsigned withAlpha(unsigned argb, float alpha) {
    return (argb & 0x00ffffffu) | (static_cast<unsigned>(alpha * 255.0f + 0.5f) << 24);
}
}

namespace metric {
inline constexpr float margin = 12.0f;
inline constexpr float pad = 12.0f;
inline constexpr float gap = 8.0f;
inline constexpr float radius = 10.0f;
inline constexpr float radiusSmall = 6.0f;
inline constexpr float row = 22.0f;
inline constexpr float hairline = 1.0f;
inline constexpr float stroke = 1.5f;
}

enum class Weight { regular, medium, semibold };

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    const auto& file = weight == Weight::semibold ? fonts::Inter_SemiBold_ttf
                     : weight == Weight::medium   ? fonts::Inter_Medium_ttf
                                                  : fonts::Inter_Regular_ttf;
    return visage::Font(context.px(designSize), file);
}

namespace type {
inline constexpr float label = 11.5f;
inline constexpr float title = 12.0f;
inline constexpr float small = 10.0f;
inline constexpr float wordmark = 17.0f;
}

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    const float radius = context.px(designRadius);
    canvas.setColor(fill);
    canvas.roundedRectangle(x, y, width, height, radius);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(x, y, width, height, radius, context.px(metric::hairline));
}

}
