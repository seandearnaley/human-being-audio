// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>
#include <string>

namespace dt::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind>;
using Knob = hba::ui::StandardKnob<ParamFrame>;
using Row = hba::ui::StandardRow<ParamFrame, ParamKind>;
using Chip = hba::ui::StandardChip<ParamFrame>;

class PillButton final : public hba::ui::ActionButtonBase<Context> {
  public:
    using ActionButtonBase::ActionButtonBase;
    void setFontSize(float size) {
        fontSize_ = size;
        redraw();
    }
    void draw(visage::Canvas& canvas) override {
        const float px = context_.scale;
        const unsigned fill =
            active_ ? colour::withAlpha(accent_, pressed_ ? 0.92f : 0.86f)
                    : (pressed_ ? colour::track : (hovered_ ? colour::withAlpha(accent_, 0.16f) : colour::raised));
        const unsigned ink = active_ ? colour::onColour : (hovered_ ? colour::text : accent_);
        hba::ui::drawPanel(canvas, px, 0.0f, 0.0f, width(), height(), fill, metric::radiusSmall);
        if (active_ || hovered_) {
            canvas.setColor(colour::withAlpha(accent_, active_ ? 0.96f : 0.50f));
            canvas.roundedRectangleBorder(0.0f, 0.0f, width(), height(), metric::radiusSmall * px, 1.0f * px);
        }
        hba::ui::drawLabel(canvas, px, label_, 4.0f * px, 0.0f, width() - 8.0f * px, height(), ink,
                           visage::Font::kCenter, fontSize_, active_ ? Weight::semibold : Weight::medium);
    }

  private:
    float fontSize_ = type::label;
};

// Large sculptural luminous dial for the 6 hero macros in SCULPT.
class HeroKnob final : public ParamFrame {
  public:
    using ParamFrame::ParamFrame;

    void setMainTextProvider(std::function<std::string(float)> fn) {
        mainProvider_ = std::move(fn);
        redraw();
    }

    void setSubTextProvider(std::function<std::string(float)> fn) {
        subProvider_ = std::move(fn);
        redraw();
    }

  private:
    void paint(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        const float w = width();
        const float h = height();
        const float titleH = 18.0f * sc;
        const float dialMax = std::min(w - 10.0f * sc, h - titleH - 4.0f * sc);
        const float d = std::max(36.0f * sc, dialMax);
        const float cx = w * 0.5f;
        const float cy = d * 0.5f + 2.0f * sc;
        const float rx = cx - d * 0.5f;
        const float ry = cy - d * 0.5f;

        // Subtle dark well inside dial
        canvas.setColor(colour::withAlpha(colour::window, 0.72f));
        canvas.circle(rx + 6.0f * sc, ry + 6.0f * sc, d - 12.0f * sc);
        canvas.setColor(colour::withAlpha(colour::edge, active() ? 0.95f : 0.65f));
        canvas.ring(rx + 6.0f * sc, ry + 6.0f * sc, d - 12.0f * sc, 1.0f * sc);

        constexpr float sweep = 1.5f * hba::ui::kUiPi;
        constexpr float start = 0.75f * hba::ui::kUiPi;
        const float pos = std::clamp(normalised(), 0.0f, 1.0f);
        const float origin = spec().low < 0.0f ? 0.5f : 0.0f;
        const float from = std::min(origin, pos);
        const float to = std::max(origin, pos);
        const float thick = 4.2f * sc;

        // Base track
        canvas.setColor(colour::track);
        canvas.roundedArc(rx, ry, d, thick, start + sweep * 0.5f, sweep * 0.5f);

        // Soft outer halo + crisp luminous arc
        if (to - from > 0.002f) {
            canvas.setColor(colour::withAlpha(accent_, active() ? 0.24f : 0.14f));
            canvas.roundedArc(rx - 1.5f * sc, ry - 1.5f * sc, d + 3.0f * sc, thick + 3.0f * sc,
                              start + sweep * (from + to) * 0.5f, sweep * (to - from) * 0.5f);
            canvas.setColor(accent_);
            canvas.roundedArc(rx, ry, d, thick, start + sweep * (from + to) * 0.5f, sweep * (to - from) * 0.5f);
        }

        // Crisp indicator dot on arc tip
        const float angle = start + sweep * pos;
        const float radius = d * 0.5f - thick * 0.5f;
        const float dot = 5.4f * sc;
        canvas.setColor(colour::text);
        canvas.circle(cx + radius * std::cos(angle) - dot * 0.5f, cy + radius * std::sin(angle) - dot * 0.5f, dot);

        // Center readout (with optional second line for musical note on Tune)
        const std::string valText = mainProvider_ ? mainProvider_(value()) : text();
        const std::string sub = subProvider_ ? subProvider_(value()) : std::string{};
        if (!sub.empty()) {
            hba::ui::drawLabel(canvas, sc, valText, 0.0f, ry + d * 0.30f, w, 17.0f * sc, colour::text,
                               visage::Font::kCenter, 12.6f, Weight::semibold);
            hba::ui::drawLabel(canvas, sc, sub, 0.0f, ry + d * 0.53f, w, 14.0f * sc, accent_, visage::Font::kCenter,
                               10.4f, Weight::semibold);
        } else {
            hba::ui::drawLabel(canvas, sc, valText, 0.0f, ry, w, d, colour::text, visage::Font::kCenter, 12.6f,
                               Weight::semibold);
        }

        // Uppercase macro title
        std::string title(spec().name);
        for (char& ch : title) {
            if (ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 'a' + 'A');
        }
        hba::ui::drawLabel(canvas, sc, title, 0.0f, h - titleH, w, titleH, active() ? colour::text : colour::textDim,
                           visage::Font::kCenter, 10.2f, Weight::semibold);
    }

    std::function<std::string(float)> mainProvider_;
    std::function<std::string(float)> subProvider_;
};

// Direct-drag H2..H6 harmonic bar editor for ANATOMY 01 CORE.
class HarmonicBars final : public visage::Frame, public Live {
  public:
    HarmonicBars(Context& ctx, unsigned accent) : context_(ctx), accent_(accent) {
        context_.live.push_back(this);
        syncCache();
    }

    ~HarmonicBars() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        bool changed = false;
        for (int i = 0; i < 5; ++i) {
            const float v = context_.model.get(paramAt(i));
            if (std::abs(v - cached_[static_cast<std::size_t>(i)]) > 1e-4f)
                changed = true;
        }
        if (changed) {
            syncCache();
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        syncCache();
        const float sc = context_.scale;
        const float w = width();
        const float h = height();
        hba::ui::drawPanel(canvas, sc, 0.0f, 0.0f, w, h, colour::window, metric::radiusSmall);

        const float padX = 8.0f * sc;
        const float padTop = 6.0f * sc;
        const float labelH = 14.0f * sc;
        const float plotH = std::max(10.0f * sc, h - padTop - labelH - 4.0f * sc);
        const float slotW = (w - 2.0f * padX) / 5.0f;
        const float barW = std::max(8.0f * sc, slotW - 10.0f * sc);
        const char* labels[5] = {"H2", "H3", "H4", "H5", "H6"};

        for (int i = 0; i < 5; ++i) {
            const float v = std::clamp(cached_[static_cast<std::size_t>(i)], 0.0f, 1.0f);
            const float bx = padX + static_cast<float>(i) * slotW + (slotW - barW) * 0.5f;
            const bool hot = (i == hovered_ || i == dragging_);

            canvas.setColor(colour::track);
            canvas.roundedRectangle(bx, padTop, barW, plotH, 3.0f * sc);

            const float fh = std::max(2.0f * sc, plotH * v);
            if (v > 0.002f) {
                canvas.setColor(colour::withAlpha(accent_, hot ? 0.96f : 0.78f));
                canvas.roundedRectangle(bx, padTop + plotH - fh, barW, fh, 3.0f * sc);
            }

            hba::ui::drawLabel(canvas, sc, labels[i], padX + static_cast<float>(i) * slotW, h - labelH - 1.0f * sc,
                               slotW, labelH, hot ? colour::text : colour::textDim, visage::Font::kCenter, 9.2f,
                               Weight::semibold);
        }
    }

    void mouseMove(const visage::MouseEvent& e) override {
        const int idx = hitIndex(e.position.x);
        if (idx != hovered_) {
            hovered_ = idx;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = -1;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const int idx = hitIndex(e.position.x);
        if (idx < 0)
            return;
        const ParamId id = paramAt(idx);
        if (e.repeatClickCount() >= 2) {
            context_.model.begin(id);
            context_.model.set(id, spec(id).def);
            context_.model.end(id);
            syncCache();
            redraw();
            return;
        }
        dragging_ = idx;
        context_.model.begin(id);
        applyY(idx, e.position.y);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragging_ < 0)
            return;
        applyY(dragging_, e.position.y);
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (dragging_ < 0)
            return;
        context_.model.end(paramAt(dragging_));
        dragging_ = -1;
        redraw();
    }

  private:
    static constexpr ParamId paramAt(int idx) noexcept {
        constexpr std::array<ParamId, 5> ids{ParamId::harm2, ParamId::harm3, ParamId::harm4, ParamId::harm5,
                                             ParamId::harm6};
        return ids[static_cast<std::size_t>(std::clamp(idx, 0, 4))];
    }

    int hitIndex(float x) const {
        const float padX = context_.px(8.0f);
        const float inner = std::max(1.0f, width() - 2.0f * padX);
        const float norm = (x - padX) / inner;
        if (norm < 0.0f || norm > 1.0f)
            return -1;
        return std::clamp(static_cast<int>(norm * 5.0f), 0, 4);
    }

    void applyY(int idx, float y) {
        const float padTop = context_.px(6.0f);
        const float labelH = context_.px(18.0f);
        const float plotH = std::max(10.0f, height() - padTop - labelH);
        const float v = std::clamp(1.0f - (y - padTop) / plotH, 0.0f, 1.0f);
        context_.model.set(paramAt(idx), v);
        cached_[static_cast<std::size_t>(idx)] = v;
        redraw();
    }

    void syncCache() {
        for (int i = 0; i < 5; ++i)
            cached_[static_cast<std::size_t>(i)] = context_.model.get(paramAt(i));
    }

    Context& context_;
    unsigned accent_;
    std::array<float, 5> cached_{};
    int hovered_ = -1;
    int dragging_ = -1;
};

inline void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y,
                      float width, float height, unsigned colourArgb = colour::textDim,
                      visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
                      Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, context.scale, text, x, y, width, height, colourArgb, justification, designSize, weight);
}

inline void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
                      float width, float height, unsigned colourArgb, float low = -1.0f, float high = 1.0f,
                      float fillAlpha = 0.0f, float baseline = 0.0f, float designThickness = metric::stroke) {
    hba::ui::drawCurve(canvas, context.scale, values, x, y, width, height, colourArgb, low, high, fillAlpha, baseline,
                       designThickness);
}

} // namespace dt::ui
