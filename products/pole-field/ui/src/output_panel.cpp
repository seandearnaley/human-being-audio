#include "panels.h"

#include <hba/ui/controls.h>

#include <algorithm>
#include <cmath>

namespace pf::ui {

namespace {
constexpr float kColumnGap = 12.0f;
constexpr float kButtonHeight = 28.0f;

class OutputPanel final : public Panel {
public:
  explicit OutputPanel(Context& context)
      : Panel(context), input_(add<Row>(context, ParamId::inputGain, colour::text, "Input", true)),
        output_(add<Row>(context, ParamId::outputGain, colour::text, "Output", true)),
        limit_(add<Row>(context, ParamId::limit, colour::text, "Limit")),
        ceiling_(add<Row>(context, ParamId::ceiling, colour::text, "Ceiling")),
        envSens_(add<Row>(context, ParamId::envSens, colour::sub, "Env sens", true)),
        quality_(add<Row>(context, ParamId::quality, colour::text, "Quality")),
        imprint_(add<Button>(context, "Imprint WAV", [this] { context_.model.browseImprintAudio(); })),
        capture_(add<Button>(context, "Capture", [this] { context_.model.captureLiveImprint(0); })),
        clear_(add<Button>(context, "Clear", [this] { context_.model.clearImprint(); })) {
      imprint_.setAccent(colour::poles);
      capture_.setAccent(colour::sub);
      clear_.setAccent(colour::textDim);
  }

    void tick() override {
        const auto label = context_.model.imprintLabel();
        const bool active = context_.model.imprint().hasAny();
        imprint_.setLabel(label);
        imprint_.setAccent(active ? colour::sub : colour::poles);
        clear_.setVisible(active);
        if (active != hadImprint_) {
            hadImprint_ = active;
            resized();
        }
        redraw();
    }

    void resized() override {
        const float pad = px(metric::pad);
        const float columnWidth = (width() - 2.0f * pad - px(kColumnGap)) * 0.5f;
        const float right = pad + columnWidth + px(kColumnGap);
        const float rowHeight = px(metric::row);
        const float buttonY = height() - pad - px(kButtonHeight);

        const float meterBottom = pad + px(hba::ui::meter::kTotalDesignHeight);
        const float avail = buttonY - meterBottom;
        const float step = rowHeight + px(4.0f);
        const float firstRow = std::round(meterBottom + 0.5f * (avail - (3.0f * rowHeight + 8.0f * px(1.0f))));

        input_.setBounds(pad, firstRow, columnWidth, rowHeight);
        output_.setBounds(pad, firstRow + step, columnWidth, rowHeight);
        quality_.setBounds(pad, firstRow + 2.0f * step, columnWidth, rowHeight);

        limit_.setBounds(right, firstRow, columnWidth, rowHeight);
        ceiling_.setBounds(right, firstRow + step, columnWidth, rowHeight);
        envSens_.setBounds(right, firstRow + 2.0f * step, columnWidth, rowHeight);

        const float totalW = width() - 2.0f * pad;
        const float btnGap = px(5.0f);
        if (context_.model.imprint().hasAny()) {
            const float clearW = px(48.0f);
            const float captureW = px(68.0f);
            const float imprintW = totalW - captureW - clearW - 2.0f * btnGap;
            imprint_.setBounds(pad, buttonY, imprintW, px(kButtonHeight));
            capture_.setBounds(pad + imprintW + btnGap, buttonY, captureW, px(kButtonHeight));
            clear_.setBounds(pad + imprintW + captureW + 2.0f * btnGap, buttonY, clearW, px(kButtonHeight));
        } else {
            const float captureW = px(92.0f);
            const float imprintW = totalW - captureW - btnGap;
            imprint_.setBounds(pad, buttonY, imprintW, px(kButtonHeight));
            capture_.setBounds(pad + imprintW + btnGap, buttonY, captureW, px(kButtonHeight));
            clear_.setBounds(0, 0, 0, 0);
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();
        hba::ui::drawStereoOutputMeter(canvas, context_.scale, px(metric::pad), width(), visuals.peakDbLeft,
                                       visuals.peakDbRight, visuals.gainReductionDb,
                                       context_.model.get(ParamId::ceiling), colour::alert);
    }

  private:
    Row& input_;
    Row& output_;
    Row& limit_;
    Row& ceiling_;
    Row& envSens_;
    Row& quality_;
    Button& imprint_;
    Button& capture_;
    Button& clear_;
    bool hadImprint_ = false;
};
} // namespace

std::unique_ptr<Panel> makeOutputPanel(Context& context) { return std::make_unique<OutputPanel>(context); }

}
