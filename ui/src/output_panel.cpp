#include "panels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::ui {

namespace {
// Design pixels.
constexpr float kMeterHeight = 5.0f;
constexpr float kMeterPitch = 11.0f;  // top of one bar to the top of the next
constexpr float kReductionHeight = 3.0f;
constexpr float kReadoutWidth = 54.0f;
constexpr float kColumnGap = 12.0f;
constexpr float kButtonHeight = 28.0f;
constexpr float kDragThreshold = 4.0f;

constexpr float kMeterFloorDb = -48.0f;
constexpr float kReductionRangeDb = 12.0f;
constexpr float kReductionVisibleDb = 0.1f;

float meterPosition(float db) { return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f); }

// The shared Button with drag-out added: once a file is ready, dragging from the
// button hands it to the host. The button itself ignores the mouse; this frame
// forwards events so the look and click behaviour stay the shared ones.
class ResampleButton final : public visage::Frame {
public:
    explicit ResampleButton(Context& context)
        : context_(context), button_(context, "Resample", [this] { clicked(); }) {
        button_.setIgnoresMouseEvents(true, false);
        addChild(&button_);
        sync();
    }

    void sync() {
        state_ = context_.model.resampleState();
        switch (state_) {
            case ResampleState::idle:
                button_.setLabel("Resample");
                button_.setAccent(colour::text);
                break;
            case ResampleState::rendering:
                button_.setLabel("Rendering…");
                button_.setAccent(colour::textDim);
                break;
            case ResampleState::ready:
                button_.setLabel("Drag into Live");
                button_.setAccent(colour::growl);
                break;
        }
    }

    void resized() override { button_.setBounds(localBounds()); }

    void mouseEnter(const visage::MouseEvent& e) override { button_.mouseEnter(e); }
    void mouseExit(const visage::MouseEvent& e) override { button_.mouseExit(e); }

    void mouseDown(const visage::MouseEvent& e) override {
        dragged_ = false;
        downPosition_ = e.position;
        button_.mouseDown(e);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragged_ || state_ != ResampleState::ready) return;
        const float dx = e.position.x - downPosition_.x;
        const float dy = e.position.y - downPosition_.y;
        if (std::hypot(dx, dy) < context_.px(kDragThreshold)) return;
        dragged_ = true;
        // Release the button without a click, then hand over to the host's drag.
        auto release = e;
        release.position = { -1.0f, -1.0f };
        button_.mouseUp(release);
        context_.model.dragResampledFile();
    }

    void mouseUp(const visage::MouseEvent& e) override {
        if (!dragged_) button_.mouseUp(e);
        dragged_ = false;
    }

private:
    void clicked() {
        if (state_ != ResampleState::rendering) context_.model.resample();
    }

    Context& context_;
    Button button_;
    ResampleState state_ = ResampleState::idle;
    visage::Point downPosition_;
    bool dragged_ = false;
};

class OutputPanel final : public Panel {
public:
    explicit OutputPanel(Context& context)
        : Panel(context), squash_(add<Row>(context, ParamId::squash, colour::growl, "Squash", true)),
          quality_(add<Row>(context, ParamId::quality, colour::text, "Quality")),
          limit_(add<Row>(context, ParamId::limit, colour::text, "Limit")),
          ceiling_(add<Row>(context, ParamId::ceiling, colour::text, "Ceiling")),
          resample_(add<ResampleButton>(context)) {}

    void tick() override {
        resample_.sync();
        redraw();
    }

    void resized() override {
        const float pad = px(metric::pad);
        const float columnWidth = (width() - 2.0f * pad - px(kColumnGap)) * 0.5f;
        const float right = pad + columnWidth + px(kColumnGap);
        const float rowHeight = px(metric::row);
        const float buttonY = height() - pad - px(kButtonHeight);

        // The two rows sit centred between the meters (gain-reduction bar
        // included, whether or not it shows) and the button.
        const float meterBottom = pad + px(2.0f * kMeterPitch + kReductionHeight);
        const float rowsHeight = 2.0f * rowHeight + px(metric::gap);
        const float firstRow = std::round(meterBottom + 0.5f * (buttonY - meterBottom - rowsHeight));
        const float secondRow = firstRow + rowHeight + px(metric::gap);

        squash_.setBounds(pad, firstRow, columnWidth, rowHeight);
        quality_.setBounds(pad, secondRow, columnWidth, rowHeight);
        limit_.setBounds(right, firstRow, columnWidth, rowHeight);
        ceiling_.setBounds(right, secondRow, columnWidth, rowHeight);
        resample_.setBounds(pad, buttonY, width() - 2.0f * pad, px(kButtonHeight));
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();
        const float ceiling = context_.model.get(ParamId::ceiling);

        const float pad = px(metric::pad);
        const float barWidth = width() - 2.0f * pad - px(kReadoutWidth);
        const float barHeight = px(kMeterHeight);

        meter(canvas, pad, pad, barWidth, barHeight, visuals.peakDbLeft, ceiling);
        meter(canvas, pad, pad + px(kMeterPitch), barWidth, barHeight, visuals.peakDbRight, ceiling);

        const float reduction = std::clamp(visuals.gainReductionDb, 0.0f, kReductionRangeDb);
        if (reduction > kReductionVisibleDb) {
            const float thickness = px(kReductionHeight);
            const float length = barWidth * reduction / kReductionRangeDb;
            canvas.setColor(colour::alert);
            canvas.roundedRectangle(pad + barWidth - length, pad + px(2.0f * kMeterPitch), length, thickness,
                                    0.5f * thickness);
        }

        const float peak = std::max(visuals.peakDbLeft, visuals.peakDbRight);
        char text[16];
        if (peak <= kMeterFloorDb * 2.0f)
            std::snprintf(text, sizeof text, "-inf dB");
        else
            std::snprintf(text, sizeof text, "%.1f dB", static_cast<double>(peak));
        drawLabel(canvas, context_, text, pad + barWidth, pad, px(kReadoutWidth), px(kMeterPitch + kMeterHeight),
                  peak > ceiling ? colour::alert : colour::text, visage::Font::kRight, type::label, Weight::regular);
    }

private:
    void meter(visage::Canvas& canvas, float x, float y, float w, float h, float db, float ceiling) {
        const float radius = 0.5f * h;
        canvas.setColor(colour::track);
        canvas.roundedRectangle(x, y, w, h, radius);

        const float level = w * meterPosition(db);
        if (level <= 0.0f) return;
        canvas.setColor(colour::withAlpha(colour::text, 0.85f));
        canvas.roundedRectangle(x, y, level, h, radius);

        const float limit = w * meterPosition(ceiling);
        if (db > ceiling && level > limit) {
            canvas.setColor(colour::alert);
            canvas.rightRoundedRectangle(x + limit, y, level - limit, h, radius);
        }
    }

    Row& squash_;
    Row& quality_;
    Row& limit_;
    Row& ceiling_;
    ResampleButton& resample_;
};
}

std::unique_ptr<Panel> makeOutputPanel(Context& context) { return std::make_unique<OutputPanel>(context); }

}
