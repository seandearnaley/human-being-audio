// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"

namespace cl::ui {

namespace {

class DeliveryPanel final : public Panel {
  public:
    explicit DeliveryPanel(Context& context)
        : Panel(context), shiftMs_(context, ParamId::shiftMs, colour::vermilion, {}, true),
          ccThin_(context, ParamId::ccThinMs, colour::textDim),
          exprAmt_(context, ParamId::motion, colour::cyan, {}, true),
          holdMidi_(context, ParamId::slicerLatch, colour::cyan),
          audition_(context, ParamId::audition, colour::emerald),
          auditionGain_(context, ParamId::auditionGain, colour::emerald, {}, true),
          dragMid_(context, "Drag 4-bar .mid", [this] { context_.model.dragMidiPhrase(); }) {
        for (auto* r : {&shiftMs_, &ccThin_, &exprAmt_, &holdMidi_, &audition_, &auditionGain_})
            addChild(r);
        dragMid_.setAccent(colour::iris);
        dragMid_.setOnDragOut([this] { context_.model.dragMidiPhrase(); });
        addChild(&dragMid_);
    }

    ~DeliveryPanel() override = default;

    void tick() override {
        const auto label = context_.model.exportStatus();
        if (label != shownExport_) {
            shownExport_ = label;
            dragMid_.setLabel(shownExport_);
        }
    }

    void resized() override {
        const float pad = px(12.0f);
        const float w = width() - 2.0f * pad;
        const float rowH = px(21.0f);
        const float gap = px(3.0f);
        float y = px(10.0f);

        for (auto* r : {&shiftMs_, &ccThin_, &exprAmt_}) {
            r->setBounds(pad, y, w, rowH);
            y += rowH + gap;
        }
        const float splitGap = px(10.0f);
        const float halfW = (w - splitGap) * 0.5f;
        holdMidi_.setBounds(pad, y, halfW, rowH);
        audition_.setBounds(pad + halfW + splitGap, y, halfW, rowH);
        y += rowH + gap;

        auditionGain_.setBounds(pad, y, w, rowH);
        dragMid_.setBounds(pad, height() - px(10.0f) - px(28.0f), w, px(28.0f));
    }

  private:
    Row shiftMs_;
    Row ccThin_;
    Row exprAmt_;
    Row holdMidi_;
    Row audition_;
    Row auditionGain_;
    Button dragMid_;
    std::string shownExport_;
};

} // namespace

std::unique_ptr<Panel> makeDeliveryPanel(Context& context) { return std::make_unique<DeliveryPanel>(context); }

} // namespace cl::ui
