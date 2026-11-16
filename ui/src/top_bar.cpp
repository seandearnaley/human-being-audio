#include "top_bar.h"

#include "tf/feel.h"

#include <algorithm>

namespace tf::ui {

namespace {
struct Macro {
    ParamId id;
    unsigned accent;
};

// The performance controls, in signal order. Coupling and Beat act on the
// master/voice relationship; Reach, Tract, Warp and Sculpt on the growl; Motion on the
// lanes; Width on the stereo field. Same order as the first eight parameters.
constexpr std::array<Macro, 8> kMacros { {
    { ParamId::coupling, colour::master },
    { ParamId::beat, colour::master },
    { ParamId::reach, colour::growl },
    { ParamId::tract, colour::growl },
    { ParamId::warp, colour::growl },
    { ParamId::sculpt, colour::growl },
    { ParamId::motion, colour::motion },
    { ParamId::width, colour::text },
} };
}

TopBar::TopBar(Context& context)
    : context_(context),
      previous_(context, "‹", [this] { context_.model.stepPreset(-1); }),
      name_(context, "", [this] { showPresetMenu(); }),
      next_(context, "›", [this] { context_.model.stepPreset(1); }),
      save_(context, "Save", [this] { context_.model.savePreset(); }) {
    for (auto* button : { &previous_, &name_, &next_, &save_ }) addChild(button);
    save_.setAccent(colour::textDim);
    for (std::size_t i = 0; i < kMacros.size(); ++i) {
        macros_.push_back(std::make_unique<Knob>(context, kMacros[i].id, kMacros[i].accent));
        addChild(macros_.back().get());
    }
    // With Beat lock on, Beat selects a division: show it, and snap the arc to it.
    for (auto& knob : macros_) {
        if (knob->id() != ParamId::beat) continue;
        knob->setFace([&model = context_.model](const Knob& beat) {
            if (model.get(ParamId::beatLock) < 0.5f) return beat.valueFace();
            constexpr int divisions = static_cast<int>(choices::beatLock.size());
            const int index = feel::beatLockIndex(beat.value(), divisions);
            return Knob::Face { std::string(choices::beatLock[static_cast<std::size_t>(index)]),
                                static_cast<float>(index) / static_cast<float>(divisions - 1) };
        });
    }
    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current == shownPreset_) return;
    shownPreset_ = std::move(current);
    name_.setLabel(shownPreset_);
}

// One submenu per category, in the order the model lists them.
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

void TopBar::draw(visage::Canvas& canvas) {
    drawLabel(canvas, context_, "Tract Form", 0.0f, 0.0f, context_.px(160.0f), height(), colour::text,
              visage::Font::kLeft, type::wordmark, Weight::semibold);
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float buttonHeight = px(28.0f);
    const float y = (height() - buttonHeight) * 0.5f;
    const float arrow = px(28.0f);
    const float gap = px(4.0f);

    float x = px(168.0f);
    previous_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + gap;
    name_.setBounds(x, y, px(220.0f), buttonHeight);
    x += px(220.0f) + gap;
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

}
