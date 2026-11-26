#include "panels.h"

#include <hba/ui/controls.h>

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
// Design pixels.
constexpr float kColumnGap = 12.0f;
constexpr float kDragThreshold = 4.0f;  // design pixels before a press becomes a drag
constexpr float kButtonHeight = 26.0f;
constexpr float kGridRowHeight = 20.0f;
constexpr float kGridRowGap = 3.0f;

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
                button_.setLabel("Drag WAV");
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

    // Once a render is ready, dragging off the button hands the file to the host.
    // The model starts the drag: hosts such as Ableton Live accept a real file on
    // disk but not the file promise Visage's own drag source offers.
    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragged_ || state_ != ResampleState::ready) return;
        const float dx = e.position.x - downPosition_.x;
        const float dy = e.position.y - downPosition_.y;
        if (std::hypot(dx, dy) < context_.px(kDragThreshold)) return;
        dragged_ = true;
        auto release = e;
        release.position = { -1.0f, -1.0f };  // outside: release the button without a click
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
      : Panel(context), attack_(add<Row>(context, ParamId::ampAttack, colour::master, "Attack", true)),
        decay_(add<Row>(context, ParamId::ampDecay, colour::master, "Decay", true)),
        sustain_(add<Row>(context, ParamId::ampSustain, colour::master, "Sustain", true)),
        release_(add<Row>(context, ParamId::ampRelease, colour::master, "Release", true)),
        squash_(add<Row>(context, ParamId::squash, colour::growl, "Squash", true)),
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
        const float rowHeight = px(kGridRowHeight);
        const float rowGap = px(kGridRowGap);
        const float buttonY = height() - pad - px(kButtonHeight);

        // Amp ADSR (4 rows on left) paired with Dynamics/Output (3 rows evenly aligned on right; OS lives in VoiceBar).
        const float meterBottom = pad + px(hba::ui::meter::kTotalDesignHeight);
        const float rowsHeight = 4.0f * rowHeight + 3.0f * rowGap;
        const float firstRow = std::round(meterBottom + 0.5f * (buttonY - meterBottom - rowsHeight));
        const auto rowY = [&](int idx) { return firstRow + static_cast<float>(idx) * (rowHeight + rowGap); };
        const float rightStep = (rowsHeight - rowHeight) * 0.5f;

        attack_.setBounds(pad, rowY(0), columnWidth, rowHeight);
        decay_.setBounds(pad, rowY(1), columnWidth, rowHeight);
        sustain_.setBounds(pad, rowY(2), columnWidth, rowHeight);
        release_.setBounds(pad, rowY(3), columnWidth, rowHeight);

        squash_.setBounds(right, firstRow, columnWidth, rowHeight);
        limit_.setBounds(right, std::round(firstRow + rightStep), columnWidth, rowHeight);
        ceiling_.setBounds(right, firstRow + rowsHeight - rowHeight, columnWidth, rowHeight);

        resample_.setBounds(pad, buttonY, width() - 2.0f * pad, px(kButtonHeight));
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();
        hba::ui::drawStereoOutputMeter(canvas, context_.scale, px(metric::pad), width(), visuals.peakDbLeft,
                                       visuals.peakDbRight, visuals.gainReductionDb,
                                       context_.model.get(ParamId::ceiling), colour::alert);
    }

  private:
    Row& attack_;
    Row& decay_;
    Row& sustain_;
    Row& release_;
    Row& squash_;
    Row& limit_;
    Row& ceiling_;
    ResampleButton& resample_;
};
}

std::unique_ptr<Panel> makeOutputPanel(Context& context) { return std::make_unique<OutputPanel>(context); }

}
