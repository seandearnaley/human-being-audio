// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/cl_brand.h>

#include <array>
#include <cmath>

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
      save_(context, "Save", [this] { context_.model.savePreset(); }) {
    addChild(&wordmark_);
    wordmark_.onDraw() += [this](visage::Canvas& canvas) { drawWordmark(canvas); };
    wordmark_.onToggle() = [open = std::move(showAbout)](visage::Button*, bool) {
        if (open)
            open();
    };
    wordmark_.setAcceptsKeystrokes(true);
    wordmark_.onKeyPress() = [this](const visage::KeyEvent& key) {
        if (key.keyCode() != visage::KeyCode::Return && key.keyCode() != visage::KeyCode::Space)
            return false;
        wordmark_.notify(false);
        return true;
    };
    for (auto* button : {&previous_, &name_, &next_, &save_})
        addChild(button);
    save_.setAccent(colour::textDim);
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
    if (current == shownPreset_)
        return;
    shownPreset_ = std::move(current);
    name_.setLabel(shownPreset_);
}

void TopBar::showPresetMenu() {
    visage::PopupMenu menu;
    const auto presets = context_.model.presets();
    std::size_t i = 0;
    while (i < presets.size()) {
        const std::string& category = presets[i].category;
        visage::PopupMenu group(category);
        for (; i < presets.size() && presets[i].category == category; ++i)
            group.addOption(static_cast<int>(i), presets[i].name);
        menu.addSubMenu(std::move(group));
    }
    menu.onSelection() = [this](int index) { context_.model.loadPreset(index); };
    menu.show(&name_);
}

void TopBar::drawWordmark(visage::Canvas& canvas) {
    constexpr float aspect = 1640.0f / 334.0f;
    const float logoHeight = context_.px(26.0f);
    const float logoWidth = logoHeight * aspect;
    visage::Svg logotype(brand::cutline_logotype_on_dark_svg);
    logotype.setDimensions(static_cast<int>(std::ceil(logoWidth)), static_cast<int>(std::ceil(logoHeight)),
                           canvas.dpiScale());
    canvas.svg(logotype, 0.0f, (wordmark_.height() - logoHeight) * 0.5f, logoWidth, logoHeight);
    if (wordmark_.hasKeyboardFocus()) {
        canvas.setColor(colour::iris);
        canvas.roundedRectangleBorder(0, 0, wordmark_.width(), wordmark_.height(), context_.px(4), context_.px(1));
    }
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
