#include "top_bar.h"

#include <embedded/tf_brand.h>
#include <hba/ui/top_bar.h>

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

TopBar::TopBar(Context& context, std::function<void()> showAbout)
    : context_(context), previous_(context, "‹", [this] { context_.model.stepPreset(-1); }),
      name_(context, "", [this] { showPresetMenu(); }), next_(context, "›", [this] { context_.model.stepPreset(1); }),
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
    ab_.setAlternateAction([this] { context_.model.copyAB(); });
    ab_.setOnRightClick(
        [this] { hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }); });
    for (std::size_t i = 0; i < kMacros.size(); ++i) {
        auto knob = std::make_unique<Knob>(context, kMacros[i].id, kMacros[i].accent);
        const auto id = kMacros[i].id;
        if (id == ParamId::coupling || id == ParamId::beat || id == ParamId::reach || id == ParamId::sculpt ||
            id == ParamId::width) {
            knob->setModProvider([&model = context_.model, id]() {
                const auto& v = model.visuals();
                if (!v.effActive)
                    return toNormalised(spec(id), model.get(id));
                switch (id) {
                case ParamId::coupling:
                    return toNormalised(spec(id), v.effCoupling);
                case ParamId::beat:
                    return toNormalised(spec(id), v.effBeat);
                case ParamId::reach:
                    return toNormalised(spec(id), v.effReach);
                case ParamId::sculpt:
                    return toNormalised(spec(id), v.effSculpt);
                case ParamId::width:
                    return toNormalised(spec(id), v.effWidth);
                default:
                    return toNormalised(spec(id), model.get(id));
                }
            });
        }
        macros_.push_back(std::move(knob));
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
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        name_.setLabel(shownPreset_);
    }
    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::master, colour::growl);
}

// One submenu per category, in the order the model lists them.
void TopBar::showPresetMenu() { hba::ui::showStudioPresetMenu(context_.model, &name_); }

// The logotype is the brand asset itself, drawn as a vector at any size.
void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::tract_form_logotype_on_dark_svg, 1908.0f / 334.0f, 26.0f, 0.0f,
                               context_.scale, colour::master);
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float buttonHeight = px(28.0f);
    const float y = (height() - buttonHeight) * 0.5f;
    const float arrow = px(28.0f);
    const float gap = px(4.0f);

    wordmark_.setBounds(0, 0, px(156.0f), height());
    float x = px(168.0f);
    previous_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + gap;
    name_.setBounds(x, y, px(220.0f), buttonHeight);
    x += px(220.0f) + gap;
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
