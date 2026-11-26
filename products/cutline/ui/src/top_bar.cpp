// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/cl_brand.h>
#include <hba/ui/top_bar.h>

#include <array>

namespace cl::ui {

namespace {
struct Macro {
    ParamId id;
    unsigned accent;
};

constexpr std::array<Macro, 8> kMacros{{
    {ParamId::grammar, colour::iris},
    {ParamId::gravity, colour::vermilion},
    {ParamId::ratchet, colour::vermilion},
    {ParamId::glide, colour::cyan},
    {ParamId::density, colour::amber},
    {ParamId::mutate, colour::amber},
    {ParamId::motion, colour::iris},
    {ParamId::velArc, colour::emerald},
}};
} // namespace

TopBar::TopBar(Context& context, std::function<void()> showAbout)
    : context_(context), previous_(context, "\u2039", [this] { context_.model.stepPreset(-1); }),
      name_(context, "", [this] { showPresetMenu(); }),
      next_(context, "\u203a", [this] { context_.model.stepPreset(1); }),
      save_(context, "Save", [this] { context_.model.savePreset(); }),
      undo_(context, "Undo", [this] { context_.model.undo(); }),
      redo_(context, "Redo", [this] { context_.model.redo(); }),
      ab_(context, "A", [this] { context_.model.toggleAB(); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });
    for (auto* button : {&previous_, &name_, &next_, &save_, &undo_, &redo_, &ab_})
        addChild(button);
    save_.setAccent(colour::textDim);
    ab_.setOnRightClick(
        [this] { hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }); });
    for (const auto& macro : kMacros) {
        macros_.push_back(std::make_unique<Knob>(context, macro.id, macro.accent));
        addChild(macros_.back().get());
    }
    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        name_.setLabel(shownPreset_);
    }
    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::iris, colour::vermilion);
}

void TopBar::showPresetMenu() {
    hba::ui::showCategorizedPresetMenu(context_.model.presets(), &name_,
                                       [this](int i) { context_.model.loadPreset(i); });
}

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::cutline_logotype_on_dark_svg, 1640.0f / 334.0f, 26.0f, 0.0f,
                               context_.scale, colour::iris);
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float buttonHeight = px(28.0f);
    const float y = (height() - buttonHeight) * 0.5f;
    const float arrow = px(28.0f);
    const float gap = px(4.0f);

    wordmark_.setBounds(0, 0, px(142.0f), height());
    float x = px(156.0f);
    previous_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + gap;
    name_.setBounds(x, y, px(228.0f), buttonHeight);
    x += px(228.0f) + gap;
    next_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + px(metric::gap);
    save_.setBounds(x, y, px(52.0f), buttonHeight);
    x += px(52.0f) + px(10.0f);
    undo_.setBounds(x, y, px(44.0f), buttonHeight);
    x += px(44.0f) + gap;
    redo_.setBounds(x, y, px(44.0f), buttonHeight);
    x += px(44.0f) + px(8.0f);
    ab_.setBounds(x, y, px(34.0f), buttonHeight);

    const float knobWidth = px(64.0f);
    const float knobHeight = px(60.0f);
    const float knobY = (height() - knobHeight) * 0.5f;
    float knobX = width() - knobWidth * static_cast<float>(macros_.size());
    for (auto& knob : macros_) {
        knob->setBounds(knobX, knobY, knobWidth, knobHeight);
        knobX += knobWidth;
    }
}

} // namespace cl::ui
