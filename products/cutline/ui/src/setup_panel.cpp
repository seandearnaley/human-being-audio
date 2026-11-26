// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"

#include <array>

namespace cl::ui {

namespace {

struct MacroDef {
    ParamId id;
    unsigned accent;
};

constexpr std::array<MacroDef, 8> kMacros{{
    {ParamId::style, colour::iris},
    {ParamId::gravity, colour::vermilion},
    {ParamId::ghosts, colour::cyan},
    {ParamId::chop, colour::coral},
    {ParamId::ratchet, colour::amber},
    {ParamId::complexity, colour::iris},
    {ParamId::fills, colour::emerald},
    {ParamId::dynamics, colour::teal},
}};

class MacroStripPanel final : public Panel {
  public:
    explicit MacroStripPanel(Context& context) : Panel(context) {
        for (const auto& m : kMacros)
            knobs_.push_back(&add<Knob>(context, m.id, m.accent));
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float cellW = (width() - px(24.0f)) / static_cast<float>(knobs_.size());
        canvas.setColor(0x14ffffffu);
        for (std::size_t i = 1; i < knobs_.size(); ++i) {
            const float x = px(12.0f) + static_cast<float>(i) * cellW;
            canvas.rectangle(x, px(12.0f), px(1.0f), height() - px(24.0f));
        }
    }

    void resized() override {
        const float padX = px(12.0f);
        const float cellW = (width() - 2.0f * padX) / static_cast<float>(knobs_.size());
        const float knobW = std::min(cellW - px(6.0f), px(74.0f));
        const float knobH = height() - px(6.0f);
        const float y = (height() - knobH) * 0.5f;
        for (std::size_t i = 0; i < knobs_.size(); ++i) {
            const float cx = padX + (static_cast<float>(i) + 0.5f) * cellW;
            knobs_[i]->setBounds(cx - knobW * 0.5f, y, knobW, knobH);
        }
    }

  private:
    std::vector<Knob*> knobs_;
};

class SetupPanel final : public Panel {
  public:
    explicit SetupPanel(Context& context)
        : Panel(context), bars_(context, ParamId::phraseBars, colour::iris),
          pocket_(context, ParamId::attractor, colour::vermilion),
          fillType_(context, ParamId::fillType, colour::emerald),
          variation_(context, ParamId::variation, colour::amber, {}, true),
          noteMap_(context, ParamId::noteMap, colour::iris), slicerStyle_(context, ParamId::slicerStyle, colour::cyan),
          strum_(context, ParamId::strumMs, colour::cyan, {}, true),
          gate_(context, ParamId::gateLen, colour::teal, {}, true) {
        for (auto* r : {&bars_, &pocket_, &fillType_, &variation_, &noteMap_, &slicerStyle_, &strum_, &gate_})
            addChild(r);
        tick();
    }

    ~SetupPanel() override = default;

    void tick() override {
        const bool isDrum = static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f) == 0;
        noteMap_.setVisible(isDrum);
        slicerStyle_.setVisible(!isDrum);
    }

    void resized() override {
        const float pad = px(12.0f);
        const float w = width() - 2.0f * pad;
        const float rowH = px(21.0f);
        const float gap = px(4.0f);
        float y = px(10.0f);

        for (auto* r : {&bars_, &pocket_, &fillType_, &variation_}) {
            r->setBounds(pad, y, w, rowH);
            y += rowH + gap;
        }
        noteMap_.setBounds(pad, y, w, rowH);
        slicerStyle_.setBounds(pad, y, w, rowH);
        y += rowH + gap;

        strum_.setBounds(pad, y, w, rowH);
        y += rowH + gap;
        gate_.setBounds(pad, y, w, rowH);
    }

  private:
    Row bars_;
    Row pocket_;
    Row fillType_;
    Row variation_;
    Row noteMap_;
    Row slicerStyle_;
    Row strum_;
    Row gate_;
};

} // namespace

std::unique_ptr<Panel> makeMacroStripPanel(Context& context) { return std::make_unique<MacroStripPanel>(context); }
std::unique_ptr<Panel> makeSetupPanel(Context& context) { return std::make_unique<SetupPanel>(context); }

} // namespace cl::ui
