// SPDX-License-Identifier: GPL-3.0-or-later
// Shared Visage parameter control bases, buttons, toggle pills, and stereo output meter primitives.
#pragma once

#include <hba/ui/theme.h>
#include <visage/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace hba::ui {

inline constexpr float kUiPi = 3.14159265f;

inline std::string withoutUnit(std::string text) {
    const auto space = text.find(' ');
    return space == std::string::npos ? text : text.substr(0, space);
}

inline std::size_t utf8CodePoints(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(
        text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xc0u) != 0x80u; }));
}

inline void drawTogglePill(visage::Canvas& canvas, float scale, float w, float h, bool on, unsigned accent,
                           bool highlightOffTrack = false) {
    const float pillWidth = 24.0f * scale;
    const float pillHeight = 13.0f * scale;
    const float inset = 2.0f * scale;
    const float x = w - pillWidth;
    const float y = (h - pillHeight) * 0.5f;
    canvas.setColor(on ? accent : (highlightOffTrack ? colour::textDim : colour::track));
    canvas.roundedRectangle(x, y, pillWidth, pillHeight, pillHeight * 0.5f);
    canvas.setColor(on ? colour::window : colour::textDim);
    const float dot = pillHeight - 2.0f * inset;
    canvas.circle(on ? x + pillWidth - inset - dot : x + inset, y + inset, dot);
}

namespace meter {
inline constexpr float kHeight = 5.0f;
inline constexpr float kPitch = 11.0f;
inline constexpr float kReductionHeight = 3.0f;
inline constexpr float kReadoutWidth = 54.0f;
inline constexpr float kFloorDb = -48.0f;
inline constexpr float kReductionRangeDb = 12.0f;
inline constexpr float kReductionVisibleDb = 0.1f;
inline constexpr float kTotalDesignHeight = 2.0f * kPitch + kReductionHeight;

inline float position(float db) { return std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f); }
} // namespace meter

inline void drawStereoOutputMeter(visage::Canvas& canvas, float scale, float pad, float panelWidth, float peakDbLeft,
                                  float peakDbRight, float gainReductionDb, float ceilingDb, unsigned alertArgb) {
    const float barWidth = panelWidth - 2.0f * pad - meter::kReadoutWidth * scale;
    const float barHeight = meter::kHeight * scale;
    const float radius = 0.5f * barHeight;

    const auto drawBar = [&](float y, float db) {
        canvas.setColor(colour::track);
        canvas.roundedRectangle(pad, y, barWidth, barHeight, radius);

        const float level = barWidth * meter::position(db);
        if (level <= 0.0f)
            return;
        canvas.setColor(colour::withAlpha(colour::text, 0.85f));
        canvas.roundedRectangle(pad, y, level, barHeight, radius);

        const float limit = barWidth * meter::position(ceilingDb);
        if (db > ceilingDb && level > limit) {
            canvas.setColor(alertArgb);
            canvas.rightRoundedRectangle(pad + limit, y, level - limit, barHeight, radius);
        }
    };

    drawBar(pad, peakDbLeft);
    drawBar(pad + meter::kPitch * scale, peakDbRight);

    const float reduction = std::clamp(gainReductionDb, 0.0f, meter::kReductionRangeDb);
    if (reduction > meter::kReductionVisibleDb) {
        const float thickness = meter::kReductionHeight * scale;
        const float length = barWidth * reduction / meter::kReductionRangeDb;
        canvas.setColor(alertArgb);
        canvas.roundedRectangle(pad + barWidth - length, pad + 2.0f * meter::kPitch * scale, length, thickness,
                                0.5f * thickness);
    }

    const float peak = std::max(peakDbLeft, peakDbRight);
    char text[16];
    if (peak <= meter::kFloorDb * 2.0f)
        std::snprintf(text, sizeof text, "-inf dB");
    else
        std::snprintf(text, sizeof text, "%.1f dB", static_cast<double>(peak));
    drawLabel(canvas, scale, text, pad + barWidth, pad, meter::kReadoutWidth * scale,
              (meter::kPitch + meter::kHeight) * scale, peak > ceilingDb ? alertArgb : colour::text,
              visage::Font::kRight, type::label, Weight::regular);
}

namespace detail {
template <typename SpecT> constexpr float specMin(const SpecT& s) noexcept {
    if constexpr (requires { s.min; })
        return s.min;
    else
        return s.low;
}

template <typename SpecT> constexpr float specMax(const SpecT& s) noexcept {
    if constexpr (requires { s.max; })
        return s.max;
    else
        return s.high;
}

template <typename ParamIdT> const auto& lookupParamSpec(ParamIdT id) {
    if constexpr (requires { spec(id); })
        return spec(id);
    else
        return specOf(id);
}

template <typename SpecT> float clampParamValue(const SpecT& s, float value) {
    if constexpr (requires { clampToRange(s, value); })
        return clampToRange(s, value);
    else
        return clampValue(s, value);
}
} // namespace detail

struct ParamControlMetrics {
    float dragRange = 180.0f;
    float fineScale = 0.1f;
    float wheelStep = 0.01f;
};

template <typename ContextT, typename ParamIdT, typename ParamSpecT, typename ParamKindT,
          ParamControlMetrics Metrics = ParamControlMetrics{}>
class ParamFrameBase : public visage::Frame, public Live {
  public:
    ParamFrameBase(ContextT& context, ParamIdT id, unsigned accent)
        : context_(context), accent_(accent), id_(id), spec_(detail::lookupParamSpec(id)) {
        context_.live.push_back(this);
    }

    ~ParamFrameBase() override { std::erase(context_.live, static_cast<Live*>(this)); }

    ParamIdT id() const { return id_; }
    const ParamSpecT& spec() const { return spec_; }
    float value() const { return context_.model.get(id_); }
    float normalised() const { return toNormalised(spec_, value()); }
    std::string text() const { return formatValue(spec_, value()); }

    void setAccent(unsigned accent) {
        if (accent == accent_)
            return;
        accent_ = accent;
        redraw();
    }

    void tick() override {
        if (value() != drawn_)
            redraw();
    }

    void draw(visage::Canvas& canvas) final {
        drawn_ = value();
        paint(canvas);
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }

    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        if (e.repeatClickCount() >= 2)
            return commit(spec_.def);
        if (spec_.kind == ParamKindT::toggle)
            return commit(value() >= 0.5f ? 0.0f : 1.0f);
        if (spec_.kind == ParamKindT::choice)
            return showMenu();

        dragging_ = true;
        dragPosition_ = normalised();
        lastY_ = e.position.y;
        context_.model.begin(id_);
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!dragging_)
            return;
        const float step =
            (lastY_ - e.position.y) / context_.px(Metrics.dragRange) * (e.isShiftDown() ? Metrics.fineScale : 1.0f);
        lastY_ = e.position.y;
        dragPosition_ = std::clamp(dragPosition_ + step, 0.0f, 1.0f);
        context_.model.set(id_, fromNormalised(spec_, dragPosition_));
        redraw();
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (!dragging_)
            return;
        dragging_ = false;
        context_.model.end(id_);
        redraw();
    }

    bool mouseWheel(const visage::MouseEvent& e) override {
        if (e.wheel_delta_y == 0.0f)
            return false;
        const float direction = e.wheel_delta_y > 0.0f ? 1.0f : -1.0f;
        nudge(direction * Metrics.wheelStep * (e.isShiftDown() ? Metrics.fineScale : 1.0f));
        return true;
    }

  protected:
    virtual void paint(visage::Canvas& canvas) = 0;

    bool active() const { return hovered_ || dragging_; }

    void commit(float real) {
        context_.model.begin(id_);
        context_.model.set(id_, detail::clampParamValue(spec_, real));
        context_.model.end(id_);
        redraw();
    }

    void nudge(float normalisedDelta) {
        if (spec_.kind == ParamKindT::continuous)
            commit(fromNormalised(spec_, std::clamp(normalised() + normalisedDelta, 0.0f, 1.0f)));
        else
            commit(value() + (normalisedDelta > 0.0f ? 1.0f : -1.0f));
    }

    void showMenu() {
        visage::PopupMenu menu;
        for (std::size_t i = 0; i < spec_.choices.size(); ++i)
            menu.addOption(static_cast<int>(i), std::string(spec_.choices[i]));
        menu.onSelection() = [this](int index) { commit(static_cast<float>(index)); };
        menu.show(this);
    }

    ContextT& context_;
    unsigned accent_;

  private:
    ParamIdT id_;
    const ParamSpecT& spec_;
    float drawn_ = -1.0e30f;
    float dragPosition_ = 0.0f;
    float lastY_ = 0.0f;
    bool hovered_ = false;
    bool dragging_ = false;
};

template <typename ParamFrameT> class StandardKnob final : public ParamFrameT {
  public:
    struct Face {
        std::string text;
        float position = 0.0f;
        bool operator==(const Face&) const = default;
    };
    using FaceProvider = std::function<Face(const StandardKnob&)>;
    using ModProvider = std::function<float()>;

    using ParamFrameT::ParamFrameT;

    Face valueFace() const { return {withoutUnit(this->text()), this->normalised()}; }

    void setFace(FaceProvider provider) {
        provider_ = std::move(provider);
        this->redraw();
    }

    void setModProvider(ModProvider provider) {
        modProvider_ = std::move(provider);
        this->redraw();
    }

    void tick() override {
        if (provider_ && face() != drawnFace_)
            this->redraw();
        if (modProvider_) {
            const float live = std::clamp(modProvider_(), 0.0f, 1.0f);
            if (std::abs(live - drawnMod_) > 0.002f)
                this->redraw();
        }
        ParamFrameT::tick();
    }

  private:
    Face face() const { return provider_ ? provider_(*this) : valueFace(); }

    void paint(visage::Canvas& canvas) override {
        drawnFace_ = face();
        const float scale = this->context_.scale;
        const float labelHeight = 16.0f * scale;
        const float diameter = std::min(this->width(), this->height() - labelHeight);
        const float x = (this->width() - diameter) * 0.5f;
        const float thickness = 3.0f * scale;

        constexpr float sweep = 1.5f * kUiPi;
        constexpr float start = 0.75f * kUiPi;
        const float position = std::clamp(drawnFace_.position, 0.0f, 1.0f);
        const float origin = detail::specMin(this->spec()) < 0.0f ? 0.5f : 0.0f;
        const float from = std::min(origin, position);
        const float to = std::max(origin, position);

        canvas.setColor(colour::track);
        canvas.roundedArc(x, 0.0f, diameter, thickness, start + sweep * 0.5f, sweep * 0.5f);
        if (to - from > 0.001f) {
            canvas.setColor(this->accent_);
            canvas.roundedArc(x, 0.0f, diameter, thickness, start + sweep * (from + to) * 0.5f,
                              sweep * (to - from) * 0.5f);
        }

        if (modProvider_) {
            const float modPos = std::clamp(modProvider_(), 0.0f, 1.0f);
            drawnMod_ = modPos;
            if (std::abs(modPos - position) > 0.004f) {
                const float mFrom = std::min(position, modPos);
                const float mTo = std::max(position, modPos);
                const float ghostThick = 2.0f * scale;
                canvas.setColor(colour::withAlpha(colour::text, 0.82f));
                canvas.roundedArc(x, 0.0f, diameter, ghostThick, start + sweep * (mFrom + mTo) * 0.5f,
                                  sweep * (mTo - mFrom) * 0.5f);
                const float angle = start + sweep * modPos;
                const float radius = diameter * 0.5f - thickness * 0.5f;
                const float cx = x + diameter * 0.5f + radius * std::cos(angle);
                const float cy = diameter * 0.5f + radius * std::sin(angle);
                const float dot = 4.0f * scale;
                canvas.setColor(colour::text);
                canvas.circle(cx - dot * 0.5f, cy - dot * 0.5f, dot);
            }
        }

        const float textSize = utf8CodePoints(drawnFace_.text) > 3 ? type::small : type::title;
        drawLabel(canvas, scale, drawnFace_.text, 0.0f, 0.0f, this->width(), diameter, colour::text,
                  visage::Font::kCenter, textSize, Weight::medium);
        drawLabel(canvas, scale, std::string(this->spec().name), 0.0f, this->height() - labelHeight, this->width(),
                  labelHeight, this->active() ? colour::text : colour::textDim, visage::Font::kCenter);
    }

    FaceProvider provider_;
    Face drawnFace_;
    ModProvider modProvider_;
    float drawnMod_ = -1.0f;
};

template <typename ParamFrameT, typename ParamKindT> class StandardRow final : public ParamFrameT {
  public:
    using ModProvider = std::function<float()>;

    template <typename ContextT, typename ParamIdT>
    StandardRow(ContextT& context, ParamIdT id, unsigned accent, std::string label = {}, bool bar = false)
        : ParamFrameT(context, id, accent), label_(label.empty() ? std::string(this->spec().name) : std::move(label)),
          bar_(bar) {}

    void setModProvider(ModProvider provider) {
        modProvider_ = std::move(provider);
        this->redraw();
    }

    void tick() override {
        if (modProvider_) {
            const float live = std::clamp(modProvider_(), 0.0f, 1.0f);
            if (std::abs(live - drawnMod_) > 0.002f)
                this->redraw();
        }
        ParamFrameT::tick();
    }

  private:
    void paint(visage::Canvas& canvas) override {
        const float scale = this->context_.scale;
        const float w = this->width();
        const float h = this->height();
        drawLabel(canvas, scale, label_, 0.0f, 0.0f, w, h, this->active() ? colour::text : colour::textDim);

        if (this->spec().kind == ParamKindT::toggle) {
            drawTogglePill(canvas, scale, w, h, this->value() >= 0.5f, this->accent_, false);
            return;
        }

        float valueRight = w;
        if (this->spec().kind == ParamKindT::choice) {
            const float half = 3.0f * scale;
            canvas.setColor(this->active() ? colour::text : colour::textFaint);
            canvas.triangleDown(w - 2.0f * half, (h - half) * 0.5f, half);
            valueRight = w - 2.0f * half - 5.0f * scale;
        }
        drawLabel(canvas, scale, this->text(), 0.0f, 0.0f, valueRight, h, colour::text, visage::Font::kRight,
                  type::label, Weight::regular);

        if (bar_) {
            const float thickness = 2.0f * scale;
            canvas.setColor(colour::track);
            canvas.roundedRectangle(0.0f, h - thickness, w, thickness, thickness * 0.5f);
            const float origin = detail::specMin(this->spec()) < 0.0f ? 0.5f : 0.0f;
            const float from = std::min(origin, this->normalised());
            const float to = std::max(origin, this->normalised());
            canvas.setColor(this->accent_);
            canvas.roundedRectangle(w * from, h - thickness, w * (to - from), thickness, thickness * 0.5f);
            if (modProvider_) {
                const float modPos = std::clamp(modProvider_(), 0.0f, 1.0f);
                drawnMod_ = modPos;
                if (std::abs(modPos - this->normalised()) > 0.004f) {
                    const float mFrom = std::min(this->normalised(), modPos);
                    const float mTo = std::max(this->normalised(), modPos);
                    canvas.setColor(colour::withAlpha(colour::text, 0.85f));
                    canvas.roundedRectangle(w * mFrom, h - thickness, w * (mTo - mFrom), thickness, thickness * 0.5f);
                    const float dot = 3.6f * scale;
                    canvas.setColor(colour::text);
                    canvas.circle(w * modPos - dot * 0.5f, h - thickness * 0.5f - dot * 0.5f, dot);
                }
            }
        }
    }

    std::string label_;
    bool bar_;
    ModProvider modProvider_;
    float drawnMod_ = -1.0f;
};

template <typename ParamFrameT> class StandardChip final : public ParamFrameT {
  public:
    using ParamFrameT::ParamFrameT;

    void setTextOverride(std::string text) {
        if (text == textOverride_)
            return;
        textOverride_ = std::move(text);
        this->redraw();
    }

  private:
    void paint(visage::Canvas& canvas) override {
        const std::string label = textOverride_.empty() ? this->text() : textOverride_;
        drawChip(canvas, this->context_.scale, label, this->width(), this->height(),
                 this->active() ? colour::text : this->accent_);
    }

    std::string textOverride_;
};

template <typename ContextT> class StudioButton final : public visage::Frame {
  public:
    StudioButton(ContextT& context, std::string label, std::function<void()> onClick)
        : context_(context), label_(std::move(label)), onClick_(std::move(onClick)) {}

    void setLabel(std::string label) {
        if (label == label_)
            return;
        label_ = std::move(label);
        redraw();
    }

    void setAccent(unsigned accent) {
        if (accent == accent_)
            return;
        accent_ = accent;
        redraw();
    }

    void setOnDrag(std::function<void()> onDrag) { onDrag_ = std::move(onDrag); }
    void setOnRightClick(std::function<void()> onRightClick) { onRightClick_ = std::move(onRightClick); }

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_.scale, 0.0f, 0.0f, width(), height(), pressed_ ? colour::track : colour::raised,
                  metric::radiusSmall);
        drawLabel(canvas, context_.scale, label_, 0.0f, 0.0f, width(), height(), hovered_ ? colour::text : accent_,
                  visage::Font::kCenter);
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }

    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup()) {
            if (onRightClick_)
                onRightClick_();
            return;
        }
        pressed_ = true;
        dragged_ = false;
        redraw();
    }

    void mouseDrag(const visage::MouseEvent&) override {
        if (pressed_ && !dragged_ && onDrag_) {
            dragged_ = true;
            pressed_ = false;
            redraw();
            onDrag_();
        }
    }

    void mouseUp(const visage::MouseEvent& e) override {
        if (!pressed_)
            return;
        const bool inside = localBounds().contains(e.position);
        const bool wasDragged = dragged_;
        pressed_ = false;
        dragged_ = false;
        redraw();
        if (inside && !wasDragged && onClick_)
            onClick_();
    }

  private:
    ContextT& context_;
    std::string label_;
    std::function<void()> onClick_;
    std::function<void()> onDrag_;
    std::function<void()> onRightClick_;
    unsigned accent_ = colour::text;
    bool hovered_ = false;
    bool pressed_ = false;
    bool dragged_ = false;
};

template <typename ContextT> class ActionButtonBase : public visage::Frame {
  public:
    ActionButtonBase(ContextT& ctx, std::string label, std::function<void()> onClick = {})
        : context_(ctx), label_(std::move(label)), onClick_(std::move(onClick)) {}

    void setLabel(std::string next) {
        if (label_ != next) {
            label_ = std::move(next);
            redraw();
        }
    }

    void setAccent(unsigned argb) {
        if (accent_ != argb) {
            accent_ = argb;
            redraw();
        }
    }

    void setActive(bool on) {
        if (active_ != on) {
            active_ = on;
            redraw();
        }
    }

    void setOnRightClick(std::function<void()> cb) { onRightClick_ = std::move(cb); }
    void setOnDragOut(std::function<void()> cb) { onDragOut_ = std::move(cb); }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }

    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup()) {
            if (onRightClick_)
                onRightClick_();
            return;
        }
        pressed_ = true;
        draggedOut_ = false;
        pressPos_ = e.position;
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!pressed_ || draggedOut_ || !onDragOut_)
            return;
        const float dx = e.position.x - pressPos_.x;
        const float dy = e.position.y - pressPos_.y;
        const float thresh = context_.px(6.0f);
        if (dx * dx + dy * dy > thresh * thresh) {
            draggedOut_ = true;
            pressed_ = false;
            redraw();
            onDragOut_();
        }
    }

    void mouseUp(const visage::MouseEvent& e) override {
        const bool fire = pressed_ && !draggedOut_ && localBounds().contains(e.position);
        pressed_ = false;
        draggedOut_ = false;
        redraw();
        if (fire && onClick_)
            onClick_();
    }

  protected:
    ContextT& context_;
    std::string label_;
    unsigned accent_ = colour::text;
    bool active_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool draggedOut_ = false;
    visage::Point pressPos_{};
    std::function<void()> onClick_;
    std::function<void()> onRightClick_;
    std::function<void()> onDragOut_;
};

} // namespace hba::ui
