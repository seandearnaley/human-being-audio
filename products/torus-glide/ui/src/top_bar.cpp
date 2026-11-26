// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/tg_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace tg::ui {

TopBar::TopBar(Context& ctx, std::function<void()> showAbout)
    : context_(ctx), modeSpiral_(ctx, "SPIRAL", [this] { setMode(0); }),
      modeCopedent_(ctx, "COPEDENT", [this] { setMode(1); }), modeMobius_(ctx,
                                                                          "M\xc3\x96"
                                                                          "BIUS",
                                                                          [this] { setMode(2); }),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Sidewinder F Bass 4Bar", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }), keyChip_(ctx, ParamId::rootKey, colour::spiral, "Key"),
      scaleChip_(ctx, ParamId::scale, colour::copedent),
      audition_(ctx, "AUDITION", [this] { context_.model.setAudition(!context_.model.isAuditioning()); }),
      dragWav_(ctx, "Bounce .wav",
               [this] {
                   if (context_.model.resampleState() == ResampleState::idle)
                       context_.model.startResample();
                   else if (context_.model.resampleState() == ResampleState::ready)
                       context_.model.startResample();
               }),
      dragMid_(ctx, "Drag .mid", [this] { context_.model.dragExportedMidi(); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    modeSpiral_.setAccent(colour::spiral);
    modeCopedent_.setAccent(colour::copedent);
    modeMobius_.setAccent(colour::mobius);
    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(colour::text);
    nextPreset_.setAccent(colour::textDim);
    audition_.setAccent(colour::mobius);
    dragWav_.setAccent(colour::spiral);
    dragMid_.setAccent(colour::copedent);

    dragWav_.setOnDragOut([this] {
        if (context_.model.resampleState() == ResampleState::ready)
            context_.model.dragResampledWav();
    });
    dragMid_.setOnDragOut([this] { context_.model.dragExportedMidi(); });

    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    for (visage::Frame* child : std::initializer_list<visage::Frame*>{
             &modeSpiral_, &modeCopedent_, &modeMobius_, &prevPreset_, &presetName_, &nextPreset_, &undo_, &redo_, &ab_,
             &keyChip_, &scaleChip_, &audition_, &dragWav_, &dragMid_}) {
        addChild(child);
    }

    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::setMode(int idx) {
    context_.model.begin(ParamId::mode);
    context_.model.set(ParamId::mode, static_cast<float>(idx));
    context_.model.end(ParamId::mode);
    // When switching into Mobius mode while on a non-Mobius voicing pack, gently select Mobius Dim7 if unmodified
    if (idx == 2 && !context_.model.sceneMatrix().isCustom) {
        const int pack = static_cast<int>(std::lround(context_.model.get(ParamId::voicingPack)));
        if (pack < 6) {
            context_.model.begin(ParamId::voicingPack);
            context_.model.set(ParamId::voicingPack, 6.0f);
            context_.model.end(ParamId::voicingPack);
        }
    }
    tick();
}

void TopBar::tick() {
    const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
    if (modeIdx != shownMode_) {
        shownMode_ = modeIdx;
        modeSpiral_.setActive(modeIdx == 0);
        modeCopedent_.setActive(modeIdx == 1);
        modeMobius_.setActive(modeIdx == 2);
    }

    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        presetName_.setLabel(shownPreset_.empty() ? "Init" : shownPreset_);
    }

    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::spiral, colour::copedent);

    const bool aud = context_.model.isAuditioning();
    audition_.setActive(aud);
    audition_.setLabel(aud ? "PLAYING" : "AUDITION");

    const auto rstate = context_.model.resampleState();
    if (rstate == ResampleState::rendering) {
        dragWav_.setLabel("Bouncing...");
        dragWav_.setActive(false);
    } else if (rstate == ResampleState::ready) {
        dragWav_.setLabel("Drag .wav");
        dragWav_.setActive(true);
    } else {
        dragWav_.setLabel("Bounce .wav");
        dragWav_.setActive(false);
    }
}

void TopBar::showPresetMenu() {
    hba::ui::showCategorizedPresetMenu(
        context_.model.presets(), &presetName_, [this](int i) { context_.model.loadPreset(i); },
        [this] { context_.model.savePreset(); });
}

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::torus_glide_logotype_on_dark_svg, 2100.0f / 334.0f, 24.0f,
                               6.0f, context_.scale);
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(30.0f);
    const float y = (h - bh) * 0.5f;
    const float gap = px(5.0f);

    wordmark_.setBounds(px(4.0f), 0.0f, px(164.0f), h);

    // Mode segmented strip
    float x = px(176.0f);
    const float modeW = px(74.0f);
    modeSpiral_.setBounds(x, y, modeW, bh);
    x += modeW + px(3.0f);
    modeCopedent_.setBounds(x, y, modeW + px(8.0f), bh);
    x += modeW + px(11.0f);
    modeMobius_.setBounds(x, y, modeW, bh);
    x += modeW + px(14.0f);

    // Preset navigator
    const float arrW = px(24.0f);
    const float nameW = px(196.0f);
    prevPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(2.0f);
    presetName_.setBounds(x, y, nameW, bh);
    x += nameW + px(2.0f);
    nextPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(10.0f);

    // Undo / Redo / AB
    undo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + px(3.0f);
    redo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + px(3.0f);
    ab_.setBounds(x, y, px(28.0f), bh);

    // Right-aligned performance & drag bar
    float rx = width() - px(10.0f);
    const float midW = px(76.0f);
    rx -= midW;
    dragMid_.setBounds(rx, y, midW, bh);

    rx -= gap + px(88.0f);
    dragWav_.setBounds(rx, y, px(88.0f), bh);

    rx -= gap + px(82.0f);
    audition_.setBounds(rx, y, px(82.0f), bh);

    rx -= px(10.0f) + px(92.0f);
    scaleChip_.setBounds(rx, y, px(92.0f), bh);

    rx -= px(4.0f) + px(62.0f);
    keyChip_.setBounds(rx, y, px(62.0f), bh);
}

} // namespace tg::ui
