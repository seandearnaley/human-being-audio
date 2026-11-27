// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/sb_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <initializer_list>

namespace sb::ui {

TopBar::TopBar(Context& ctx, std::function<void()> showAbout)
    : context_(ctx), padMain_(ctx, "MAIN C1", [this] { context_.model.triggerAudition(Articulation::main, 1.0f); }),
      padGhost_(ctx, "GHOST C#", [this] { context_.model.triggerAudition(Articulation::ghost, 0.72f); }),
      padRim_(ctx, "RIM D1", [this] { context_.model.triggerAudition(Articulation::rim, 0.96f); }),
      padRoll_(ctx, "ROLL D#", [this] { context_.model.triggerAudition(Articulation::roll, 0.92f); }),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Neuro Crack 185Hz", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }),
      autoAlign_(ctx, "Auto-Align", [this] { context_.model.runAutoAlign(); }),
      bounceWav_(ctx, "Bounce .wav", [this] { context_.model.startBounce(); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    padMain_.setGradientAccent(colour::shell, colour::shellAlt);
    padGhost_.setGradientAccent(colour::body, colour::bodyAlt);
    padRim_.setGradientAccent(colour::punch, colour::shell);
    padRoll_.setGradientAccent(colour::wire, colour::wireAlt);
    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(0xfff4f8ff);
    nextPreset_.setAccent(colour::textDim);
    autoAlign_.setGradientAccent(colour::locked, colour::body);
    bounceWav_.setGradientAccent(colour::body, colour::violet);

    bounceWav_.setOnDragOut([this] {
        if (context_.model.bounceState() == BounceState::ready)
            context_.model.dragBouncedWav();
    });

    ab_.setAlternateAction([this] { context_.model.copyAB(); });
    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&prevPreset_, &presetName_, &nextPreset_, &undo_, &redo_, &ab_,
                                               &padMain_, &padGhost_, &padRim_, &padRoll_, &autoAlign_, &bounceWav_}) {
        addChild(child);
    }

    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::tick() {
    const auto& tel = context_.model.visuals();
    if (tel.triggerCount != lastTrigCount_) {
        lastTrigCount_ = tel.triggerCount;
        flashArt_ = tel.lastArticulation;
        flashTicks_ = 10;
    } else if (flashTicks_ > 0) {
        --flashTicks_;
        if (flashTicks_ == 0)
            flashArt_ = -1;
    }

    padMain_.setActive(flashArt_ == 0);
    padGhost_.setActive(flashArt_ == 1);
    padRim_.setActive(flashArt_ == 2);
    padRoll_.setActive(flashArt_ == 3);

    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        presetName_.setLabel(shownPreset_.empty() ? "Init" : shownPreset_);
    }

    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::body, colour::shell, 0xffc6d4ec, colour::textFaint);

    const auto bstate = context_.model.bounceState();
    hba::ui::syncWavExport(bounceWav_, bstate);
}

void TopBar::showPresetMenu() { hba::ui::showStudioPresetMenu(context_.model, &presetName_); }

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::strike_body_logotype_on_dark_svg, 2100.0f / 334.0f, 24.0f,
                               8.0f, context_.scale);
}

void TopBar::draw(visage::Canvas& canvas) {
    drawCard(canvas, context_, 0.0f, 0.0f, width(), height(), colour::body, metric::radius, false);
    // Subtle dual-color studio signature bar across top chrome
    canvas.setBrush(
        visage::Brush::horizontal(colour::withAlpha(colour::shell, 0.85f), colour::withAlpha(colour::body, 0.85f)));
    canvas.roundedRectangle(context_.px(12.0f), 0.0f, width() - context_.px(24.0f), context_.px(2.0f),
                            context_.px(1.0f));
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(32.0f);
    const float y = (h - bh) * 0.5f;

    wordmark_.setBounds(px(6.0f), 0.0f, px(168.0f), h);

    float x = px(180.0f);
    const float arrW = px(26.0f);
    const float nameW = px(236.0f);
    prevPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(2.0f);
    presetName_.setBounds(x, y, nameW, bh);
    x += nameW + px(2.0f);
    nextPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(14.0f);

    undo_.setBounds(x, y, px(46.0f), bh);
    x += px(46.0f) + px(4.0f);
    redo_.setBounds(x, y, px(46.0f), bh);
    x += px(46.0f) + px(4.0f);
    ab_.setBounds(x, y, px(30.0f), bh);

    x += px(44.0f);
    const float padW = px(66.0f);
    for (auto* b : {&padMain_, &padGhost_, &padRim_, &padRoll_}) {
        b->setBounds(x, y, padW, bh);
        x += padW + px(5.0f);
    }
    x += px(14.0f);

    float rx = width() - px(10.0f);
    const float wavW = px(98.0f);
    rx -= wavW;
    bounceWav_.setBounds(rx, y, wavW, bh);

    rx -= px(6.0f) + px(96.0f);
    autoAlign_.setBounds(rx, y, px(96.0f), bh);
}

} // namespace sb::ui
