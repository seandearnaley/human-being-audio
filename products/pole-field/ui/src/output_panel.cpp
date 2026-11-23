#include "panels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pf::ui {

namespace {
constexpr float kMeterHeight = 5.0f;
constexpr float kMeterPitch = 11.0f;
constexpr float kReductionHeight = 3.0f;
constexpr float kReadoutWidth = 54.0f;
constexpr float kColumnGap = 12.0f;
constexpr float kButtonHeight = 28.0f;
constexpr float kMeterFloorDb = -48.0f;
constexpr float kReductionRangeDb = 12.0f;
constexpr float kReductionVisibleDb = 0.1f;

float meterPosition(float db) { return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f); }

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

        const float meterBottom = pad + px(2.0f * kMeterPitch + kReductionHeight);
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
}

std::unique_ptr<Panel> makeOutputPanel(Context& context) { return std::make_unique<OutputPanel>(context); }

}
