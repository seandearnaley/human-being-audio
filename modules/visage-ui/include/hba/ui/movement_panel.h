// SPDX-License-Identifier: GPL-3.0-or-later
// Shared 4-lane MovementPanel, CurveEditor, LaneSwitch, and FollowChip for Visage studio instruments.
#pragma once

#include <hba/dsp/curve.h>
#include <hba/ui/controls.h>
#include <hba/ui/movement_edit.h>
#include <hba/ui/theme.h>
#include <visage/ui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <functional>
#include <string>
#include <vector>

namespace hba::ui {

struct MovementColumnWidths {
    float switchW = 24.0f;
    float targetW = 76.0f;
    float followW = 56.0f;
    float rateW = 60.0f;
    float modeW = 0.0f;
    float phaseW = 52.0f;
    float shapeW = 84.0f;
    float depthW = 56.0f;
};

namespace detail {
inline constexpr float kMoveLine = 22.0f;
inline constexpr float kMoveChipHeight = 18.0f;
inline constexpr float kMoveSelected = 86.0f;
inline constexpr float kMoveEditorGap = 4.0f;
inline constexpr float kMoveEditorBelow = 2.0f;
inline constexpr float kMovePlotInset = 8.0f;
inline constexpr float kMoveEditorInsetY = 7.0f;
inline constexpr float kMoveDot = 5.0f;
inline constexpr float kMovePlayhead = 1.5f;
inline constexpr float kMoveHandle = 6.0f;
inline constexpr float kMoveHandleActive = 8.0f;
inline constexpr float kMoveHandleReach = 8.0f;
inline constexpr float kMoveLineReach = 5.0f;
inline constexpr float kMoveSlop = 3.0f;
inline constexpr float kMoveFillAlpha = 0.12f;
inline constexpr float kMoveFollowerAlpha = 0.6f;
inline constexpr float kMoveLeaderAlpha = 0.45f;
inline constexpr double kMoveWindowQuarters = 8.0;
inline constexpr double kMoveWindowSeconds = 4.0;
inline constexpr int kMoveQuartersPerBar = 4;
inline constexpr double kMoveAlmostOne = 1.0 - 1.0e-6;

template <typename T>
concept HasFreeHzTrait = requires {
    { T::kHasFreeHz } -> std::convertible_to<bool>;
};

template <typename T> constexpr bool hasFreeHz() {
    if constexpr (HasFreeHzTrait<T>)
        return T::kHasFreeHz;
    else
        return false;
}

inline float snapPx(float v) { return std::round(v); }
inline float hairlinePx(float scale) { return std::max(1.0f, snapPx(metric::hairline * scale)); }
} // namespace detail

template <typename ParamFrameT> class StudioLaneSwitch final : public ParamFrameT {
  public:
    using ParamFrameT::ParamFrameT;

  private:
    void paint(visage::Canvas& canvas) override {
        drawTogglePill(canvas, this->context_.scale, this->width(), this->height(), this->value() >= 0.5f,
                       this->accent_, this->active());
    }
};

template <typename ParamFrameT, typename ParamIdT> class StudioFollowChip final : public ParamFrameT {
  public:
    template <typename ContextT>
    StudioFollowChip(ContextT& context, ParamIdT followParam, int lane)
        : ParamFrameT(context, followParam, colour::textDim), lane_(lane) {}

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.repeatClickCount() >= 2)
            return ParamFrameT::mouseDown(e);
        visage::PopupMenu menu;
        for (std::size_t i = 0; i < this->spec().choices.size(); ++i)
            if (static_cast<int>(i) != lane_ + 1)
                menu.addOption(static_cast<int>(i), std::string(this->spec().choices[i]));
        menu.onSelection() = [this](int index) {
            this->context_.model.begin(this->id());
            this->context_.model.set(this->id(), static_cast<float>(index));
            this->context_.model.end(this->id());
            this->redraw();
        };
        menu.show(this);
    }

  private:
    void paint(visage::Canvas& canvas) override {
        const auto& choices = this->spec().choices;
        if (choices.empty())
            return;
        const int rawChoice = static_cast<int>(this->value() + 0.5f);
        const int choice = std::clamp(rawChoice == lane_ + 1 ? 0 : rawChoice, 0, static_cast<int>(choices.size()) - 1);
        const std::string label(choices[static_cast<std::size_t>(choice)]);
        drawChip(canvas, this->context_.scale, label, this->width(), this->height(),
                 this->active() ? colour::text : this->accent_);
    }

    int lane_;
};

template <typename Traits> class StudioCurveEditor final : public visage::Frame {
  public:
    using Context = typename Traits::Context;
    using Params = typename Traits::Params;
    using ParamId = typename Traits::ParamId;
    using LaneField = typename Traits::LaneField;
    using LaneShape = typename Traits::LaneShape;
    using Chip = typename Traits::Chip;

    explicit StudioCurveEditor(Context& context) : context_(context) {
        if constexpr (Traits::kHasEnvLane) {
            envSrc_ = std::make_unique<Chip>(context, Traits::envSourceParam(), Traits::envAccent());
            addChild(envSrc_.get());
            envSrc_->setVisible(false);
        }
    }

    std::function<void()> onEdit;

    void setLane(int lane) {
        if (lane == lane_)
            return;
        lane_ = lane;
        hover_ = -1;
        gesture_ = {};
        syncVisibility();
        redraw();
    }

    void syncVisibility() {
        if constexpr (Traits::kHasEnvLane) {
            const Params params = Traits::paramsOf(context_.model);
            const bool envMode = Traits::isEnvLane(params, lane_);
            envSrc_->setVisible(envMode);
            if (envMode)
                layoutEnvControls();
        }
    }

    void resized() override {
        if constexpr (Traits::kHasEnvLane)
            layoutEnvControls();
    }

    void draw(visage::Canvas& canvas) override {
        const float scale = context_.scale;
        drawPanel(canvas, scale, 0.0f, 0.0f, width(), height(), colour::raised, metric::radiusSmall);
        const Params params = Traits::paramsOf(context_.model);
        const edit::Area plot = area();
        drawGrid(canvas, plot);

        const bool on = params.on(Traits::field(lane_, LaneField::on));
        if constexpr (Traits::kHasEnvLane) {
            if (Traits::isEnvLane(params, lane_)) {
                const unsigned hue = on ? Traits::envAccent() : colour::textFaint;
                drawCurve(canvas, scale, Traits::envHistory(context_.model), plot.x, plot.y, plot.width, plot.height,
                          hue, 0.0f, 1.0f, on ? 0.18f : 0.0f, 0.0f);
                drawLabel(canvas, scale, "Envelope Follower", plot.x + 2.0f * scale, 4.0f * scale, 140.0f * scale,
                          16.0f * scale, on ? Traits::envAccent() : colour::textFaint, visage::Font::kLeft,
                          type::small);
                return;
            }
        }

        const bool editable = !Traits::following(params, lane_);
        const hba::dsp::Curve curve =
            editable && gesture_.active() ? working_ : Traits::cycleCurve(context_.model, params, lane_);
        const unsigned hue = on ? Traits::laneHue(params, lane_) : colour::textFaint;

        values_.resize(static_cast<std::size_t>(std::max(2.0f, std::ceil(plot.width) + 1.0f)));
        for (std::size_t i = 0; i < values_.size(); ++i) {
            const double x =
                std::min(static_cast<double>(i) / static_cast<double>(values_.size() - 1), detail::kMoveAlmostOne);
            values_[i] = hba::dsp::evaluate(curve, x);
        }
        drawCurve(canvas, scale, values_, plot.x, plot.y, plot.width, plot.height,
                  editable ? hue : colour::withAlpha(hue, detail::kMoveLeaderAlpha), -1.0f, 1.0f,
                  editable && on ? detail::kMoveFillAlpha : 0.0f);
        if (!editable)
            return;

        if (on) {
            const float phase = Traits::editorPhase(context_.model, params, lane_);
            const float x = detail::snapPx(edit::toX(plot, phase));
            canvas.setColor(colour::withAlpha(colour::text, 0.25f));
            canvas.fill(x, detail::snapPx(plot.y), detail::hairlinePx(scale), detail::snapPx(plot.height));
        }

        const int lit = gesture_.movingPoint() >= 0 ? gesture_.movingPoint() : hover_;
        for (int i = 0; i < curve.count; ++i) {
            const edit::Pos p = edit::pointPos(plot, curve.points[static_cast<std::size_t>(i)]);
            const bool active = i == lit;
            const float diameter = context_.px(active ? detail::kMoveHandleActive : detail::kMoveHandle);
            const float ring = diameter + 2.0f * context_.px(metric::stroke);
            canvas.setColor(colour::raised);
            canvas.circle(p.x - ring * 0.5f, p.y - ring * 0.5f, ring);
            canvas.setColor(active ? colour::text : hue);
            canvas.circle(p.x - diameter * 0.5f, p.y - diameter * 0.5f, diameter);
        }
    }

    void mouseMove(const visage::MouseEvent& e) override { setHover(pick(e)); }
    void mouseExit(const visage::MouseEvent&) override { setHover(-1); }

    void mouseDown(const visage::MouseEvent& e) override {
        if (!editable())
            return;
        if (e.shouldTriggerPopup())
            return showMenu(pick(e));
        working_ = editableCurve();
        const edit::Reach reach{context_.px(detail::kMoveHandleReach), context_.px(detail::kMoveLineReach),
                                context_.px(detail::kMoveSlop)};
        if (gesture_.press(working_, area(), at(e), e.repeatClickCount(), e.isAltDown(), reach))
            commit(working_);
        hover_ = pick(e);
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!gesture_.active())
            return;
        if (gesture_.drag(working_, area(), at(e), e.isAltDown()))
            commit(working_);
        redraw();
    }

    void mouseUp(const visage::MouseEvent& e) override {
        if (!gesture_.active())
            return;
        if (gesture_.release(working_, area(), e.isAltDown()))
            commit(working_);
        hover_ = pick(e);
        redraw();
    }

  private:
    edit::Area area() const {
        const float x = context_.px(detail::kMovePlotInset);
        const float y = context_.px(detail::kMoveEditorInsetY);
        return {x, y, width() - 2.0f * x, height() - 2.0f * y};
    }

    static edit::Pos at(const visage::MouseEvent& e) { return {e.position.x, e.position.y}; }

    bool editable() const {
        const Params params = Traits::paramsOf(context_.model);
        if constexpr (Traits::kHasEnvLane) {
            if (Traits::isEnvLane(params, lane_))
                return false;
        }
        return !Traits::following(params, lane_);
    }

    void layoutEnvControls() {
        if constexpr (Traits::kHasEnvLane) {
            if (envSrc_ == nullptr)
                return;
            const float w = context_.px(72.0f);
            const float h = context_.px(18.0f);
            const float inset = context_.px(4.0f);
            envSrc_->setBounds(detail::snapPx(width() - inset - w), detail::snapPx(inset), detail::snapPx(w),
                               detail::snapPx(h));
        }
    }

    hba::dsp::Curve editableCurve() const {
        return Traits::cycleCurve(context_.model, Traits::paramsOf(context_.model), lane_);
    }

    int pick(const visage::MouseEvent& e) const {
        if (!editable())
            return -1;
        return edit::pickPoint(editableCurve(), area(), at(e), context_.px(detail::kMoveHandleReach));
    }

    void setHover(int index) {
        if (index == hover_)
            return;
        hover_ = index;
        redraw();
    }

    void commit(const hba::dsp::Curve& curve) {
        context_.model.setLaneCurve(lane_, curve);
        const ParamId shape = Traits::field(lane_, LaneField::shape);
        if (static_cast<LaneShape>(static_cast<int>(context_.model.get(shape) + 0.5f)) != LaneShape::custom) {
            context_.model.begin(shape);
            context_.model.set(shape, static_cast<float>(LaneShape::custom));
            context_.model.end(shape);
        }
        redraw();
        if (onEdit)
            onEdit();
    }

    void showMenu(int index) {
        const hba::dsp::Curve curve = editableCurve();
        if (index < 0 || index >= curve.count)
            return;
        const hba::dsp::Segment current = curve.points[static_cast<std::size_t>(index)].segment;
        constexpr int kRemove = 3;
        visage::PopupMenu menu;
        menu.addOption(static_cast<int>(hba::dsp::Segment::line), "Line").select(current == hba::dsp::Segment::line);
        menu.addOption(static_cast<int>(hba::dsp::Segment::smooth), "Smooth")
            .select(current == hba::dsp::Segment::smooth);
        menu.addOption(static_cast<int>(hba::dsp::Segment::hold), "Hold").select(current == hba::dsp::Segment::hold);
        menu.addOption(kRemove, "Remove").enable(curve.count > 1);
        const int lane = lane_;
        menu.onSelection() = [this, lane, index](int option) {
            if (lane != lane_)
                return;
            hba::dsp::Curve edited = editableCurve();
            if (index >= edited.count)
                return;
            if (option == kRemove)
                hba::dsp::removePoint(edited, index);
            else
                edited.points[static_cast<std::size_t>(index)].segment = static_cast<hba::dsp::Segment>(option);
            hover_ = -1;
            commit(edited);
        };
        menu.show(this);
    }

    void drawGrid(visage::Canvas& canvas, const edit::Area& plot) const {
        const float line = detail::hairlinePx(context_.scale);
        for (int i = 1; i < edit::kSnapDivisions; ++i) {
            const float x = detail::snapPx(edit::toX(plot, static_cast<float>(i) / edit::kSnapDivisions));
            canvas.setColor(colour::withAlpha(colour::text, i % 4 == 0 ? 0.08f : 0.03f));
            canvas.fill(x, detail::snapPx(plot.y), line, detail::snapPx(plot.height));
        }
        canvas.setColor(colour::track);
        canvas.fill(detail::snapPx(plot.x), detail::snapPx(plot.y + (plot.height - line) * 0.5f),
                    detail::snapPx(plot.width), line);
    }

    Context& context_;
    std::unique_ptr<Chip> envSrc_;
    int lane_ = 0;
    int hover_ = -1;
    edit::Gesture gesture_;
    hba::dsp::Curve working_;
    std::vector<float> values_;
};

template <typename Traits> class StudioMovementPanel final : public Traits::Panel {
  public:
    using Context = typename Traits::Context;
    using Panel = typename Traits::Panel;
    using ParamFrame = typename Traits::ParamFrame;
    using Chip = typename Traits::Chip;
    using Params = typename Traits::Params;
    using ParamId = typename Traits::ParamId;
    using LaneField = typename Traits::LaneField;
    using LaneCurves = typename Traits::LaneCurves;
    using LaneSwitch = StudioLaneSwitch<ParamFrame>;
    using FollowChip = StudioFollowChip<ParamFrame, ParamId>;
    using CurveEditor = StudioCurveEditor<Traits>;

    static constexpr int kLaneCount = Traits::kLaneCount;
    static constexpr int kLaneFieldCount = Traits::kLaneFieldCount;

    static_assert(detail::kMoveSelected + (kLaneCount - 1) * detail::kMoveLine == 176.0f - 2.0f * metric::pad,
                  "the lanes fill the panel");

    explicit StudioMovementPanel(Context& context) : Panel(context) {
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& c = lanes_[static_cast<std::size_t>(lane)];
            c.on =
                &this->template add<LaneSwitch>(context, Traits::field(lane, LaneField::on), Traits::defaultAccent());
            c.target =
                &this->template add<Chip>(context, Traits::field(lane, LaneField::target), Traits::defaultAccent());
            c.follow = &this->template add<FollowChip>(context, Traits::field(lane, LaneField::follow), lane);
            c.rate = &this->template add<Chip>(context, Traits::field(lane, LaneField::rate), colour::textDim);
            if constexpr (detail::hasFreeHz<Traits>())
                c.hz = &this->template add<Chip>(context, Traits::field(lane, LaneField::hz), colour::textDim);
            c.ratio = &this->template add<Chip>(context, Traits::field(lane, LaneField::ratio), colour::textDim);
            if constexpr (Traits::kHasModeChip)
                c.mode = &this->template add<Chip>(context, Traits::field(lane, LaneField::mode), colour::textDim);
            c.phase = &this->template add<Chip>(context, Traits::field(lane, LaneField::phase), colour::textDim);
            if constexpr (Traits::kHasEnvLane) {
                c.envAtk = &this->template add<Chip>(context, Traits::envAttackParam(), Traits::envAccent());
                c.envRel = &this->template add<Chip>(context, Traits::envReleaseParam(), Traits::envAccent());
            }
            c.shape = &this->template add<Chip>(context, Traits::field(lane, LaneField::shape), colour::textDim);
            c.depth = &this->template add<Chip>(context, Traits::field(lane, LaneField::depth), colour::textDim);
        }
        editor_ = &this->template add<CurveEditor>(context);
        editor_->onEdit = [this] { this->redraw(); };
        updateControls();
    }

    void resized() override { layout(); }

    void tick() override {
        const State state = watch();
        const bool changed = state != shown_;
        if (changed) {
            shown_ = state;
            updateControls();
        }
        bool liveChanged = this->context_.model.visuals().ppq != drawnPpq_;
        if constexpr (Traits::kHasModeChip)
            liveChanged = liveChanged || this->context_.model.visuals().notePpq != drawnNotePpq_;
        if constexpr (detail::hasFreeHz<Traits>()) {
            liveChanged = liveChanged || Traits::freeSeconds(this->context_.model) != drawnFreeSec_ ||
                          Traits::noteSeconds(this->context_.model) != drawnNoteSec_;
        }
        if constexpr (Traits::kHasEnvLane) {
            const float envLvl = Traits::envLevel(this->context_.model);
            const auto envHist = Traits::envHistory(this->context_.model);
            const float envTail = envHist.empty() ? 0.0f : envHist.back();
            liveChanged =
                liveChanged || std::abs(envLvl - drawnEnv_) > 1.0e-3f || std::abs(envTail - drawnEnvTail_) > 1.0e-3f;
        }
        if (changed || liveChanged) {
            if constexpr (Traits::kHasEnvLane) {
                drawnEnv_ = Traits::envLevel(this->context_.model);
                const auto envHist = Traits::envHistory(this->context_.model);
                drawnEnvTail_ = envHist.empty() ? 0.0f : envHist.back();
            }
            this->redraw();
            editor_->redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        this->drawChrome(canvas);
        const float scale = this->context_.scale;
        drawnPpq_ = this->context_.model.visuals().ppq;
        if constexpr (Traits::kHasModeChip)
            drawnNotePpq_ = this->context_.model.visuals().notePpq;
        if constexpr (detail::hasFreeHz<Traits>()) {
            drawnFreeSec_ = Traits::freeSeconds(this->context_.model);
            drawnNoteSec_ = Traits::noteSeconds(this->context_.model);
        }
        if constexpr (Traits::kHasEnvLane) {
            drawnEnv_ = Traits::envLevel(this->context_.model);
            const auto envHist = Traits::envHistory(this->context_.model);
            drawnEnvTail_ = envHist.empty() ? 0.0f : envHist.back();
        }

        const Params params = Traits::paramsOf(this->context_.model);
        const LaneCurves curves = Traits::curvesOf(this->context_.model);
        const Columns c = columns();
        const float left = detail::snapPx(c.curve + this->px(detail::kMovePlotInset));
        const float plotWidth = detail::snapPx(c.curve + c.curveWidth - this->px(detail::kMovePlotInset)) - left;
        if (plotWidth <= 0.0f)
            return;

        const double windowStart = std::floor(drawnPpq_ / detail::kMoveWindowQuarters) * detail::kMoveWindowQuarters;
        const float syncPlayheadX =
            left + plotWidth * static_cast<float>((drawnPpq_ - windowStart) / detail::kMoveWindowQuarters);
        const float motion = params[ParamId::motion];

        values_.resize(static_cast<std::size_t>(std::max(2.0f, std::ceil(plotWidth) + 1.0f)));

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float top =
                detail::snapPx(lineTop(lane) + this->px((detail::kMoveLine - detail::kMoveChipHeight) * 0.5f));
            const float height =
                detail::snapPx(lineTop(lane) + this->px((detail::kMoveLine + detail::kMoveChipHeight) * 0.5f)) - top;

            if (lane == hovered_ && lane != selected_)
                drawPanel(canvas, scale, detail::snapPx(c.curve), top,
                          detail::snapPx(c.curve + c.curveWidth) - detail::snapPx(c.curve), height, colour::raised,
                          metric::radiusSmall);

            bool freeLane = false;
            if constexpr (detail::hasFreeHz<Traits>())
                freeLane = Traits::isFreeLane(params, lane);
            drawGrid(canvas, left, plotWidth, top, height, freeLane);

            const bool on = params.on(Traits::field(lane, LaneField::on));
            const float ampScale = params[Traits::field(lane, LaneField::depth)] * motion;
            const float inset = this->px(metric::stroke) * 0.5f;

            if constexpr (Traits::kHasEnvLane) {
                if (Traits::isEnvLane(params, lane)) {
                    const unsigned base = on ? Traits::envAccent() : colour::textFaint;
                    const unsigned hue =
                        Traits::following(params, lane) ? colour::withAlpha(base, detail::kMoveFollowerAlpha) : base;
                    const auto hist = Traits::envHistory(this->context_.model);
                    scaledEnv_.resize(hist.size());
                    for (std::size_t i = 0; i < hist.size(); ++i)
                        scaledEnv_[i] = std::clamp(hist[i] * ampScale, -1.0f, 1.0f);
                    drawCurve(canvas, scale, scaledEnv_, left, top + inset, plotWidth, height - 2.0f * inset, hue,
                              -1.0f, 1.0f, on ? 0.16f : 0.0f);
                    if (on) {
                        const float val = std::clamp(Traits::envLevel(this->context_.model) * ampScale, -1.0f, 1.0f);
                        const float y = top + inset + (height - 2.0f * inset) * (1.0f - (val + 1.0f) * 0.5f);
                        const float dotX = left + plotWidth;
                        const float ring = this->px(detail::kMoveDot) + 2.0f * this->px(metric::stroke);
                        canvas.setColor(lane == hovered_ && lane != selected_ ? colour::raised : colour::panel);
                        canvas.circle(dotX - ring * 0.5f, y - ring * 0.5f, ring);
                        canvas.setColor(hue);
                        canvas.circle(dotX - this->px(detail::kMoveDot) * 0.5f, y - this->px(detail::kMoveDot) * 0.5f,
                                      this->px(detail::kMoveDot));
                    }
                    continue;
                }
            }

            float playheadX = syncPlayheadX;
            double noteWindow = 0.0;
            double freeWindow = 0.0;
            if constexpr (detail::hasFreeHz<Traits>()) {
                if (freeLane) {
                    const double clk = Traits::isFreeRetrig(params, lane) ? drawnNoteSec_ : drawnFreeSec_;
                    freeWindow = std::floor(clk / detail::kMoveWindowSeconds) * detail::kMoveWindowSeconds;
                    playheadX = left + plotWidth * static_cast<float>(std::clamp(
                                                       (clk - freeWindow) / detail::kMoveWindowSeconds, 0.0, 1.0));
                }
            }
            if constexpr (Traits::kHasModeChip) {
                if (!freeLane) {
                    const auto timing = Traits::noteTiming(params, lane, drawnNotePpq_, detail::kMoveWindowQuarters);
                    noteWindow = timing.noteWindow;
                    if (timing.noteClock)
                        playheadX = left + plotWidth * timing.normPos;
                }
            }

            for (std::size_t i = 0; i < values_.size(); ++i) {
                const double t =
                    std::min(static_cast<double>(i) / static_cast<double>(values_.size() - 1), detail::kMoveAlmostOne);
                if constexpr (detail::hasFreeHz<Traits>()) {
                    if (freeLane) {
                        const double sampleSec = freeWindow + t * detail::kMoveWindowSeconds;
                        values_[i] =
                            static_cast<float>(Traits::sampleLaneFree(params, curves, lane, sampleSec, sampleSec)) *
                            ampScale;
                        continue;
                    }
                }
                const double samplePpq = windowStart + t * detail::kMoveWindowQuarters;
                const double sampleNotePpq = noteWindow + t * detail::kMoveWindowQuarters;
                values_[i] =
                    static_cast<float>(Traits::sampleLane(params, curves, lane, samplePpq, sampleNotePpq)) * ampScale;
            }
            const unsigned base = on ? Traits::laneHue(params, lane) : colour::textFaint;
            const unsigned hue =
                Traits::following(params, lane) ? colour::withAlpha(base, detail::kMoveFollowerAlpha) : base;
            drawCurve(canvas, scale, values_, left, top + inset, plotWidth, height - 2.0f * inset, hue, -1.0f, 1.0f,
                      on ? detail::kMoveFillAlpha : 0.0f);

            canvas.setColor(colour::withAlpha(colour::text, 0.4f));
            canvas.fill(playheadX - this->px(detail::kMovePlayhead) * 0.5f, top, this->px(detail::kMovePlayhead),
                        height);

            if (on) {
                float value = 0.0f;
                if constexpr (detail::hasFreeHz<Traits>()) {
                    if (freeLane) {
                        value = static_cast<float>(
                                    Traits::sampleLaneFree(params, curves, lane, drawnFreeSec_, drawnNoteSec_)) *
                                ampScale;
                    } else {
                        value = static_cast<float>(Traits::sampleLane(params, curves, lane, drawnPpq_, drawnNotePpq_)) *
                                ampScale;
                    }
                } else {
                    value = static_cast<float>(Traits::sampleLane(params, curves, lane, drawnPpq_, drawnNotePpq_)) *
                            ampScale;
                }
                const float y =
                    top + inset + (height - 2.0f * inset) * (1.0f - (std::clamp(value, -1.0f, 1.0f) + 1.0f) * 0.5f);
                const float ring = this->px(detail::kMoveDot) + 2.0f * this->px(metric::stroke);
                canvas.setColor(lane == hovered_ && lane != selected_ ? colour::raised : colour::panel);
                canvas.circle(playheadX - ring * 0.5f, y - ring * 0.5f, ring);
                canvas.setColor(hue);
                canvas.circle(playheadX - this->px(detail::kMoveDot) * 0.5f, y - this->px(detail::kMoveDot) * 0.5f,
                              this->px(detail::kMoveDot));
            }
        }
    }

    void mouseMove(const visage::MouseEvent& e) override { setHovered(stripAt(e)); }
    void mouseExit(const visage::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const visage::MouseEvent& e) override {
        const int lane = stripAt(e);
        if (lane < 0 || lane == selected_)
            return;
        selected_ = lane;
        hovered_ = -1;
        editor_->setLane(lane);
        layout();
        this->redraw();
    }

  private:
    struct LaneControls {
        LaneSwitch* on = nullptr;
        Chip* target = nullptr;
        FollowChip* follow = nullptr;
        Chip* rate = nullptr;
        Chip* hz = nullptr;
        Chip* ratio = nullptr;
        Chip* mode = nullptr;
        Chip* phase = nullptr;
        Chip* envAtk = nullptr;
        Chip* envRel = nullptr;
        Chip* shape = nullptr;
        Chip* depth = nullptr;
    };

    struct Columns {
        float on, target, follow, rate, mode, phase, curve, curveWidth, shape, depth;
    };

    struct State {
        std::array<float, static_cast<std::size_t>(kLaneCount* kLaneFieldCount + 1)> params{};
        LaneCurves curves{};
        bool operator==(const State&) const = default;
    };

    State watch() const {
        State state;
        std::size_t idx = 0;
        for (int lane = 0; lane < kLaneCount; ++lane)
            for (int f = 0; f < kLaneFieldCount; ++f)
                state.params[idx++] = this->context_.model.get(Traits::field(lane, static_cast<LaneField>(f)));
        state.params.back() = this->context_.model.get(ParamId::motion);
        state.curves = Traits::curvesOf(this->context_.model);
        return state;
    }

    Columns columns() const {
        constexpr MovementColumnWidths w = Traits::kColumns;
        const float gap = this->px(metric::gap);
        Columns c{};
        c.on = this->px(metric::pad);
        c.target = c.on + this->px(w.switchW) + gap;
        c.follow = c.target + this->px(w.targetW) + gap;
        c.rate = c.follow + this->px(w.followW) + gap;
        if constexpr (Traits::kHasModeChip) {
            c.mode = c.rate + this->px(w.rateW) + gap;
            c.phase = c.mode + this->px(w.modeW) + gap;
        } else {
            c.mode = 0.0f;
            c.phase = c.rate + this->px(w.rateW) + gap;
        }
        c.curve = c.phase + this->px(w.phaseW) + gap;
        c.depth = this->width() - this->px(metric::pad) - this->px(w.depthW);
        c.shape = c.depth - gap - this->px(w.shapeW);
        c.curveWidth = c.shape - gap - c.curve;
        return c;
    }

    float lineTop(int lane) const {
        float top = metric::pad;
        for (int i = 0; i < lane; ++i)
            top += i == selected_ ? detail::kMoveSelected : detail::kMoveLine;
        return this->px(top);
    }

    void layout() {
        constexpr MovementColumnWidths w = Traits::kColumns;
        const Columns c = columns();
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const float top = lineTop(lane);
            const auto place = [&](visage::Frame& frame, float x, float fw, float fh) {
                const float y = detail::snapPx(top + (this->px(detail::kMoveLine) - fh) * 0.5f);
                frame.setBounds(detail::snapPx(x), y, detail::snapPx(x + fw) - detail::snapPx(x),
                                detail::snapPx(y + fh) - y);
            };
            const float chip = this->px(detail::kMoveChipHeight);
            place(*controls.on, c.on, this->px(w.switchW), this->px(detail::kMoveLine));
            place(*controls.target, c.target, this->px(w.targetW), chip);
            place(*controls.follow, c.follow, this->px(w.followW), chip);
            place(*controls.rate, c.rate, this->px(w.rateW), chip);
            if constexpr (detail::hasFreeHz<Traits>())
                place(*controls.hz, c.rate, this->px(w.rateW), chip);
            place(*controls.ratio, c.rate, this->px(w.rateW), chip);
            if constexpr (Traits::kHasModeChip)
                place(*controls.mode, c.mode, this->px(w.modeW), chip);
            place(*controls.phase, c.phase, this->px(w.phaseW), chip);
            if constexpr (Traits::kHasEnvLane) {
                place(*controls.envAtk, c.rate, this->px(w.rateW), chip);
                if constexpr (Traits::kHasModeChip)
                    place(*controls.envRel, c.mode, this->px(w.modeW), chip);
                else
                    place(*controls.envRel, c.phase, this->px(w.phaseW), chip);
            }
            place(*controls.shape, c.shape, this->px(w.shapeW), chip);
            place(*controls.depth, c.depth, this->px(w.depthW), chip);
        }
        const float top = detail::snapPx(lineTop(selected_) + this->px(detail::kMoveLine + detail::kMoveEditorGap));
        const float bottom =
            detail::snapPx(lineTop(selected_) + this->px(detail::kMoveSelected - detail::kMoveEditorBelow));
        editor_->setBounds(detail::snapPx(c.curve), top,
                           detail::snapPx(c.curve + c.curveWidth) - detail::snapPx(c.curve), bottom - top);
    }

    void updateControls() {
        const Params params = Traits::paramsOf(this->context_.model);
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const bool follows = Traits::following(params, lane);
            bool envMode = false;
            if constexpr (Traits::kHasEnvLane)
                envMode = Traits::isEnvLane(params, lane);
            bool freeMode = false;
            if constexpr (detail::hasFreeHz<Traits>())
                freeMode = Traits::isFreeLane(params, lane);

            controls.rate->setVisible(!follows && !envMode && !freeMode);
            if constexpr (detail::hasFreeHz<Traits>())
                controls.hz->setVisible(!follows && !envMode && freeMode);
            controls.ratio->setVisible(follows && !envMode);
            controls.phase->setVisible(!envMode);
            controls.shape->setVisible(!follows);
            if constexpr (Traits::kHasEnvLane) {
                controls.envAtk->setVisible(envMode && !follows);
                controls.envRel->setVisible(envMode && !follows);
            }

            const bool on = params.on(Traits::field(lane, LaneField::on));
            const unsigned dim = on ? colour::textDim : colour::textFaint;
            unsigned accent = on ? Traits::laneHue(params, lane) : colour::textFaint;
            if constexpr (Traits::kHasEnvLane) {
                if (envMode)
                    accent = on ? Traits::envAccent() : colour::textFaint;
            }
            if constexpr (Traits::kDynamicOnAccent)
                controls.on->setAccent(accent);
            controls.target->setAccent(accent);
            controls.follow->setAccent(dim);
            if constexpr (Traits::kHasEnvLane) {
                controls.envAtk->setAccent(on ? Traits::envAccent() : colour::textFaint);
                controls.envRel->setAccent(on ? Traits::envAccent() : colour::textFaint);
            }
            if constexpr (Traits::kHasModeChip) {
                controls.mode->setVisible(!envMode);
                controls.mode->setAccent(Traits::modeAccent(params, lane, on, dim));
                if constexpr (detail::hasFreeHz<Traits>()) {
                    if (follows && freeMode)
                        controls.mode->setTextOverride(Traits::isFreeRetrig(params, lane) ? "Free R" : "Free");
                    else
                        controls.mode->setTextOverride({});
                }
            }
            for (Chip* chip : {controls.rate, controls.ratio, controls.phase, controls.shape, controls.depth})
                chip->setAccent(dim);
            if constexpr (detail::hasFreeHz<Traits>()) {
                const unsigned freeTint = (on && freeMode) ? Traits::freeAccent() : dim;
                controls.hz->setAccent(freeTint);
                if (follows && freeMode)
                    controls.ratio->setAccent(freeTint);
            }
        }
        if (editor_ != nullptr)
            editor_->syncVisibility();
    }

    int stripAt(const visage::MouseEvent& e) const {
        const Columns c = columns();
        if (e.position.x < c.curve || e.position.x > c.curve + c.curveWidth)
            return -1;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float top = lineTop(lane);
            if (e.position.y >= top && e.position.y < top + this->px(detail::kMoveLine))
                return lane;
        }
        return -1;
    }

    void setHovered(int lane) {
        if (lane == selected_)
            lane = -1;
        if (lane == hovered_)
            return;
        hovered_ = lane;
        this->redraw();
    }

    void drawGrid(visage::Canvas& canvas, float left, float plotWidth, float top, float height,
                  bool freeTime = false) const {
        const float line = detail::hairlinePx(this->context_.scale);
        const int divs = freeTime ? 4 : static_cast<int>(detail::kMoveWindowQuarters);
        for (int q = 1; q < divs; ++q) {
            const float x = detail::snapPx(left + plotWidth * static_cast<float>(q) / static_cast<float>(divs));
            const float alpha =
                freeTime ? (q % 2 == 0 ? 0.10f : 0.04f) : (q % detail::kMoveQuartersPerBar == 0 ? 0.10f : 0.04f);
            canvas.setColor(colour::withAlpha(colour::text, alpha));
            canvas.fill(x, top, line, height);
        }
        canvas.setColor(colour::track);
        canvas.fill(left, detail::snapPx(top + (height - line) * 0.5f), plotWidth, line);
    }

    std::array<LaneControls, static_cast<std::size_t>(kLaneCount)> lanes_{};
    CurveEditor* editor_ = nullptr;
    std::vector<float> values_;
    std::vector<float> scaledEnv_;
    int selected_ = 0;
    int hovered_ = -1;
    double drawnPpq_ = -1.0;
    double drawnNotePpq_ = -1.0;
    double drawnFreeSec_ = -1.0;
    double drawnNoteSec_ = -1.0;
    float drawnEnv_ = -1.0f;
    float drawnEnvTail_ = -1.0f;
    State shown_{};
};

} // namespace hba::ui
