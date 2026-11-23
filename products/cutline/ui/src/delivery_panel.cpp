// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"

namespace cl::ui {

namespace {

class DeliveryPanel final : public Panel {
  public:
    explicit DeliveryPanel(Context& context)
        : Panel(context), bendRange_(context, ParamId::bendRange, colour::cyan),
          legato_(context, ParamId::legato, colour::iris, {}, true),
          shiftMs_(context, ParamId::shiftMs, colour::vermilion, {}, true),
          ccThin_(context, ParamId::ccThinMs, colour::textDim), audition_(context, ParamId::audition, colour::emerald),
          auditionTone_(context, ParamId::auditionTone, colour::emerald),
          auditionGain_(context, ParamId::auditionGain, colour::emerald, {}, true),
          dragMid_(context, "Drag 4-bar .mid", [this] { context_.model.dragMidiPhrase(); }) {
        for (auto* r : {&bendRange_, &legato_, &shiftMs_, &ccThin_, &audition_, &auditionTone_, &auditionGain_})
            addChild(r);
        dragMid_.setAccent(colour::iris);
        dragMid_.setOnDrag([this] { context_.model.dragMidiPhrase(); });
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
        const float pad = context_.px(metric::pad);
        const float w = width() - 2.0f * pad;
        const float rowH = context_.px(19.0f);
        const float gap = context_.px(2.0f);
        float y = pad - context_.px(3.0f);

        for (auto* r : {&bendRange_, &legato_, &shiftMs_, &ccThin_}) {
            r->setBounds(pad, y, w, rowH);
            y += rowH + gap;
        }
        const float splitGap = context_.px(12.0f);
        const float halfW = (w - splitGap) * 0.5f;
        audition_.setBounds(pad, y, halfW, rowH);
        auditionTone_.setBounds(pad + halfW + splitGap, y, halfW, rowH);
        y += rowH + gap;

        auditionGain_.setBounds(pad, y, w, rowH);
        dragMid_.setBounds(pad, height() - pad - context_.px(25.0f), w, context_.px(25.0f));
    }

  private:
    Row bendRange_;
    Row legato_;
    Row shiftMs_;
    Row ccThin_;
    Row audition_;
    Row auditionTone_;
    Row auditionGain_;
    Button dragMid_;
    std::string shownExport_;
};

} // namespace

std::unique_ptr<Panel> makeDeliveryPanel(Context& context) { return std::make_unique<DeliveryPanel>(context); }

} // namespace cl::ui
