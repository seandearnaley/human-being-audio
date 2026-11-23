#include "top_bar.h"

#include <embedded/pf_brand.h>

#include <array>
#include <cmath>

namespace pf::ui {

namespace {
struct Macro {
    ParamId id;
    unsigned accent;
};

constexpr std::array<Macro, 8> kMacros { {
    { ParamId::morphX, colour::poles },
    { ParamId::morphY, colour::poles },
    { ParamId::morphZ, colour::poles },
    { ParamId::freq,   colour::poles },
    { ParamId::res,    colour::drive },
    { ParamId::drive,  colour::drive },
    { ParamId::motion, colour::motion },
    { ParamId::mix,    colour::sub },
} };
}

TopBar::TopBar(Context& context, std::function<void()> showAbout)
    : context_(context), previous_(context, "\u2039", [this] { context_.model.stepPreset(-1); }),
      name_(context, "", [this] { showPresetMenu(); }),
      next_(context, "\u203a", [this] { context_.model.stepPreset(1); }),
      save_(context, "Save", [this] { context_.model.savePreset(); }),
      undo_(context, "Undo", [this] { context_.model.undo(); }),
      redo_(context, "Redo", [this] { context_.model.redo(); }),
      ab_(context, "A", [this] { context_.model.toggleAB(); }) {
    addChild(&wordmark_);
    wordmark_.onDraw() += [this](visage::Canvas& canvas) { drawWordmark(canvas); };
    wordmark_.onToggle() = [open = std::move(showAbout)](visage::Button*, bool) { if (open) open(); };
    wordmark_.setAcceptsKeystrokes(true);
    wordmark_.onKeyPress() = [this](const visage::KeyEvent& key) {
        if (key.keyCode() != visage::KeyCode::Return && key.keyCode() != visage::KeyCode::Space) return false;
        wordmark_.notify(false);
        return true;
    };
    for (auto* button : {&previous_, &name_, &next_, &save_, &undo_, &redo_, &ab_})
        addChild(button);
    save_.setAccent(colour::textDim);
    ab_.setOnRightClick([this] {
        visage::PopupMenu menu;
        menu.addOption(1, context_.model.isSlotB() ? "Copy B \u2192 A" : "Copy A \u2192 B");
        menu.onSelection() = [this](int opt) {
            if (opt == 1)
                context_.model.copyAB();
        };
        menu.show(&ab_);
    });
    for (const auto& macro : kMacros) {
        auto knob = std::make_unique<Knob>(context, macro.id, macro.accent);
        const auto id = macro.id;
        if (id == ParamId::morphX || id == ParamId::morphY || id == ParamId::morphZ || id == ParamId::freq ||
            id == ParamId::res || id == ParamId::drive) {
            knob->setModProvider([&model = context_.model, id]() {
                const auto& v = model.visuals();
                switch (id) {
                case ParamId::morphX:
                    return toNormalised(spec(id), v.effMorphX);
                case ParamId::morphY:
                    return toNormalised(spec(id), v.effMorphY);
                case ParamId::morphZ:
                    return toNormalised(spec(id), v.effMorphZ);
                case ParamId::freq:
                    return toNormalised(spec(id), v.effFreq + v.midiShiftSt);
                case ParamId::res:
                    return toNormalised(spec(id), v.effRes);
                case ParamId::drive:
                    return toNormalised(spec(id), v.effDrive);
                default:
                    return toNormalised(spec(id), model.get(id));
                }
            });
        }
        macros_.push_back(std::move(knob));
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
    undo_.setAccent(context_.model.canUndo() ? colour::textDim : colour::textFaint);
    redo_.setAccent(context_.model.canRedo() ? colour::textDim : colour::textFaint);
    const bool isB = context_.model.isSlotB();
    ab_.setLabel(isB ? "B" : "A");
    ab_.setAccent(isB ? colour::drive : colour::poles);
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
    visage::Svg logotype(brand::pole_field_logotype_on_dark_svg);
    logotype.setDimensions(static_cast<int>(std::ceil(logoWidth)), static_cast<int>(std::ceil(logoHeight)), canvas.dpiScale());
    canvas.svg(logotype, 0.0f, (wordmark_.height() - logoHeight) * 0.5f, logoWidth, logoHeight);
    if (wordmark_.hasKeyboardFocus()) {
        canvas.setColor(colour::poles);
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

}
