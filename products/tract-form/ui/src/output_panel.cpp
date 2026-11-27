#include "panels.h"

#include <hba/ui/controls.h>

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
// Design pixels.
constexpr float kColumnGap = 12.0f;
constexpr float kButtonHeight = 26.0f;
constexpr float kGridRowHeight = 20.0f;
constexpr float kGridRowGap = 3.0f;

class ResampleButton final : public Button {
  public:
    explicit ResampleButton(Context& context)
        : Button(context, "Bounce WAV", [&context] { context.model.resample(); }) {
        setOnDragOut([&context] { context.model.dragResampledFile(); });
        sync();
    }
    void sync() { hba::ui::syncWavExport(*this, context_.model.resampleState()); }
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
