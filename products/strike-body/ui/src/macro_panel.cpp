// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <array>
#include <initializer_list>

namespace sb::ui {

namespace {

class MacroPanel final : public Panel {
  public:
    explicit MacroPanel(Context& ctx)
        : Panel(ctx), kPunch_(ctx, ParamId::macroPunch, colour::body, "PUNCH", true),
          kTight_(ctx, ParamId::macroTight, colour::locked, "TIGHT", true),
          kMetal_(ctx, ParamId::macroMetal, colour::shell, "METAL", true),
          kSmack_(ctx, ParamId::macroSmack, colour::smack, "SMACK", true),
          kScoop_(ctx, ParamId::scoopAmt, colour::punch, "SCOOP", true),
          kRoom_(ctx, ParamId::roomAmt, colour::violet, "ROOM", true),
          bSnare_(ctx, "SNARE", [this] { context_.model.loadArchetype(Archetype::snare); }),
          bKick_(ctx, "KICK", [this] { context_.model.loadArchetype(Archetype::kick); }),
          bClap_(ctx, "CLAP", [this] { context_.model.loadArchetype(Archetype::clap); }),
          bMetal_(ctx, "METAL", [this] { context_.model.loadArchetype(Archetype::metal); }),
          bTom_(ctx, "TOM", [this] { context_.model.loadArchetype(Archetype::tom); }),
          bPerc_(ctx, "PERC", [this] { context_.model.loadArchetype(Archetype::perc); }),
          bMutate_(ctx, "MUTATE [M]", [this] { context_.model.mutateSound(); }) {
        bSnare_.setAccent(colour::body);
        bKick_.setAccent(colour::locked);
        bClap_.setAccent(colour::wire);
        bMetal_.setAccent(colour::shell);
        bTom_.setAccent(colour::punch);
        bPerc_.setAccent(colour::violet);

        bMutate_.setGradientAccent(colour::smack, colour::shell);
        bMutate_.setActive(true);

        for (visage::Frame* child :
             std::initializer_list<visage::Frame*>{&kPunch_, &kTight_, &kMetal_, &kSmack_, &kScoop_, &kRoom_, &bSnare_,
                                                   &bKick_, &bClap_, &bMetal_, &bTom_, &bPerc_, &bMutate_}) {
            addChild(child);
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float leftW = std::floor((width() - gap) * 0.62f);
        const float rightX = leftW + gap;
        const float rightW = width() - rightX;

        const float padX = px(10.0f);
        const float knobY = px(23.0f);
        const float knobH = height() - px(27.0f);
        const float availW = leftW - 2.0f * padX;
        const float kw = availW / 6.0f;
        const std::array<Knob*, 6> knobs{&kPunch_, &kTight_, &kMetal_, &kSmack_, &kScoop_, &kRoom_};
        for (int i = 0; i < 6; ++i)
            knobs[static_cast<std::size_t>(i)]->setBounds(padX + static_cast<float>(i) * kw, knobY, kw, knobH);

        const float rPad = px(12.0f);
        const float innerW = rightW - 2.0f * rPad;
        const float mutateW = px(118.0f);
        const float gridW = innerW - mutateW - px(10.0f);
        const float btnGap = px(6.0f);
        const float btnW = (gridW - 2.0f * btnGap) / 3.0f;
        const float btnH = px(32.0f);
        const float topY = px(30.0f);

        const std::array<ActionButton*, 6> archBtns{&bSnare_, &bKick_, &bClap_, &bMetal_, &bTom_, &bPerc_};
        for (int i = 0; i < 6; ++i) {
            const int col = i % 3;
            const int row = i / 3;
            archBtns[static_cast<std::size_t>(i)]->setBounds(rightX + rPad + static_cast<float>(col) * (btnW + btnGap),
                                                             topY + static_cast<float>(row) * (btnH + btnGap), btnW,
                                                             btnH);
        }

        bMutate_.setBounds(rightX + rPad + gridW + px(10.0f), topY, mutateW, btnH * 2.0f + btnGap);
    }

    void draw(visage::Canvas& canvas) override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float leftW = std::floor((width() - gap) * 0.62f);
        const float rightX = leftW + gap;
        const float rightW = width() - rightX;

        drawCard(canvas, context_, 0.0f, 0.0f, leftW, height(), colour::punch);
        drawCard(canvas, context_, rightX, 0.0f, rightW, height(), colour::smack);

        drawLabel(canvas, context_, "PERFORMANCE MACROS  \xc2\xb7  AUTOMATABLE", px(14.0f), px(7.0f), leftW - px(28.0f),
                  px(15.0f), colour::punch, visage::Font::kLeft, 9.4f, Weight::semibold);
        drawLabel(canvas, context_, "QUICK-START ARCHETYPES  \xc2\xb7  INSTANT VARIATION", rightX + px(14.0f), px(7.0f),
                  rightW - px(28.0f), px(15.0f), colour::smack, visage::Font::kLeft, 9.4f, Weight::semibold);
    }

  private:
    Knob kPunch_, kTight_, kMetal_, kSmack_, kScoop_, kRoom_;
    ActionButton bSnare_, bKick_, bClap_, bMetal_, bTom_, bPerc_, bMutate_;
};

} // namespace

std::unique_ptr<Panel> makeMacroPanel(Context& ctx) { return std::make_unique<MacroPanel>(ctx); }

} // namespace sb::ui
