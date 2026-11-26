// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 1.0 PhrasePanel container: left parameter rail + interactive PhraseStage.
#include "panels.h"
#include "phrase_stage.h"

#include <algorithm>

namespace cl::ui {

namespace {

class PhrasePanel final : public Panel {
  public:
    explicit PhrasePanel(Context& context)
        : Panel(context), outputMode_(context, ParamId::outputMode, colour::iris),
          inputMode_(context, ParamId::inputMode, colour::cyan),
          phraseBars_(context, ParamId::phraseBars, colour::iris),
          phraseMode_(context, ParamId::phraseMode, colour::amber),
          seed_(context, ParamId::seed, colour::iris, {}, true),
          seedPrev_(context, "\u2039", [this] { context_.model.stepSeedHistory(-1); }),
          rollSeed_(context, "Roll seed", [this] { context_.model.rollSeed(); }),
          seedNext_(context, "\u203a", [this] { context_.model.stepSeedHistory(1); }),
          attractor_(context, ParamId::attractor, colour::vermilion),
          rootNote_(context, ParamId::rootNote, colour::cyan), octave_(context, ParamId::octave, colour::cyan),
          scale_(context, ParamId::scale, colour::cyan), contour_(context, ParamId::contour, colour::iris),
          drumStyle_(context, ParamId::drumStyle, colour::vermilion), latch_(context, ParamId::latch, colour::cyan),
          stage_(context) {
        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &phraseMode_, &seed_, &attractor_, &rootNote_,
                        &octave_, &scale_, &contour_, &drumStyle_, &latch_})
            addChild(r);
        seedPrev_.setAccent(colour::textDim);
        rollSeed_.setAccent(colour::iris);
        seedNext_.setAccent(colour::textDim);
        addChild(&seedPrev_);
        addChild(&rollSeed_);
        addChild(&seedNext_);
        addChild(&stage_);
        syncModeVisibility();
    }

    void tick() override {
        seedPrev_.setAccent(context_.model.canStepSeedHistory(-1) ? colour::text : colour::textFaint);
        seedNext_.setAccent(context_.model.canStepSeedHistory(1) ? colour::text : colour::textFaint);
        syncModeVisibility();
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float pad = context_.px(metric::pad);
        const float railW = context_.px(216.0f);
        const float hintY = height() - pad - context_.px(34.0f);
        const float hintH = context_.px(34.0f);
        canvas.setColor(colour::withAlpha(colour::window, 0.55f));
        canvas.roundedRectangle(pad, hintY, railW, hintH, context_.px(5.0f));
        canvas.setColor(colour::edge);
        canvas.roundedRectangleBorder(pad, hintY, railW, hintH, context_.px(5.0f), context_.px(0.8f));
        drawLabel(canvas, context_, "Drag pitch \u00b7 Edge len \u00b7 Shift nudge", pad + context_.px(6.0f),
                  hintY + context_.px(3.0f), railW - context_.px(12.0f), context_.px(14.0f),
                  colour::withAlpha(colour::textDim, 0.82f), visage::Font::kCenter, 9.0f, Weight::medium);
        drawLabel(canvas, context_, "Brush VEL \u00b7 Drag chords \u00b7 R-click ops", pad + context_.px(6.0f),
                  hintY + context_.px(17.0f), railW - context_.px(12.0f), context_.px(14.0f),
                  colour::withAlpha(colour::textDim, 0.82f), visage::Font::kCenter, 9.0f, Weight::medium);
    }

    void resized() override {
        const float pad = context_.px(metric::pad);
        const float railW = context_.px(216.0f);
        const float gap = context_.px(metric::gap);
        const float rowH = context_.px(23.0f);
        const float rowGap = context_.px(3.5f);
        float y = pad;

        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &phraseMode_, &seed_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + rowGap;
        }

        const float navW = context_.px(28.0f);
        const float navGap = context_.px(4.0f);
        const float btnH = context_.px(24.0f);
        y += context_.px(2.0f);
        seedPrev_.setBounds(pad, y, navW, btnH);
        rollSeed_.setBounds(pad + navW + navGap, y, railW - 2.0f * navW - 2.0f * navGap, btnH);
        seedNext_.setBounds(pad + railW - navW, y, navW, btnH);
        y += btnH + context_.px(10.0f);

        for (auto* r : {&attractor_, &rootNote_, &octave_, &scale_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + rowGap;
        }
        // Contour (Mono/MPE) and Kit style (Drum Kit) share the exact same rail slot.
        contour_.setBounds(pad, y, railW, rowH);
        drumStyle_.setBounds(pad, y, railW, rowH);
        y += rowH + rowGap;

        latch_.setBounds(pad, y, railW, rowH);

        const float stageX = pad + railW + gap * 1.5f;
        stage_.setBounds(stageX, pad, width() - stageX - pad, height() - 2.0f * pad);
    }

  private:
    void syncModeVisibility() {
        const bool isDrum =
            static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f) == static_cast<int>(OutputMode::drum);
        contour_.setVisible(!isDrum);
        drumStyle_.setVisible(isDrum);
    }

    Row outputMode_;
    Row inputMode_;
    Row phraseBars_;
    Row phraseMode_;
    Row seed_;
    Button seedPrev_;
    Button rollSeed_;
    Button seedNext_;
    Row attractor_;
    Row rootNote_;
    Row octave_;
    Row scale_;
    Row contour_;
    Row drumStyle_;
    Row latch_;
    PhraseStage stage_;
};

} // namespace

std::unique_ptr<Panel> makePhrasePanel(Context& context) { return std::make_unique<PhrasePanel>(context); }

} // namespace cl::ui
