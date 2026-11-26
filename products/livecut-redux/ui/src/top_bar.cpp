// SPDX-License-Identifier: GPL-3.0-or-later
#include "top_bar.h"

#include "pad_art.h"

#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>

namespace lc::ui {

TopBar::TopBar(Context& context, std::function<void()> showAbout)
    : context_(context), previous_(context, "\u2039", [this] { context_.model.stepPreset(-1); }),
      presetName_(context, "", [this] { showPresetMenu(); }),
      next_(context, "\u203a", [this] { context_.model.stepPreset(1); }),
      save_(context, "Save", [this] { context_.model.savePreset(); }),
      undo_(context, "Undo", [this] { context_.model.undo(); }),
      redo_(context, "Redo", [this] { context_.model.redo(); }),
      ab_(context, "A", [this] { context_.model.toggleAB(); }),
      surface_(context, "surface", {"Pad", "Classic"}, colour::accent),
      subAnchor_(context, "subAnchor", "Sub", colour::cyan, true), limiter_(context, "limiter", "LIM", colour::amber) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout), [this](visage::Canvas& canvas) {
        drawWordmark(canvas, 0.0f, 0.0f, wordmark_.width(), wordmark_.height(), context_.scale,
                     wordmark_.hasKeyboardFocus());
    });

    for (visage::Frame* child : std::initializer_list<visage::Frame*>{&previous_, &presetName_, &next_, &save_, &undo_,
                                                                      &redo_, &ab_, &surface_, &subAnchor_, &limiter_})
        addChild(child);

    save_.setAccent(colour::textDim);
    ab_.setOnRightClick(
        [this] { hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }); });

    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::showPresetMenu() {
    hba::ui::showCategorizedPresetMenu(context_.model.presets(), &presetName_,
                                       [this](int i) { context_.model.loadPreset(i); });
}

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        presetName_.setLabel(shownPreset_);
    }
    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::accent, colour::rose);

    // Sync active surface accent colour
    const bool onPad = context_.model.getNormalised("surface") < 0.5f;
    const unsigned activeCol = onPad ? colour::programArgb(context_.model.visuals().padActiveProgram) : colour::accent;
    surface_.setAccent(activeCol);

    const auto& v = context_.model.visuals();
    const float nextL = std::max(v.outputPeakL, peakL_ * 0.88f);
    const float nextR = std::max(v.outputPeakR, peakR_ * 0.88f);
    const float nextGr = std::max(v.limiterGrDb, grDb_ * 0.85f);
    if (std::abs(nextL - peakL_) > 0.004f || std::abs(nextR - peakR_) > 0.004f || std::abs(nextGr - grDb_) > 0.02f) {
        peakL_ = nextL;
        peakR_ = nextR;
        grDb_ = nextGr;
        redraw();
    }
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

    // Live Stereo Output Peak Meter on right edge
    const float px = context_.scale;
    const float meterW = 84.0f * px;
    const float meterX = width() - meterW - 12.0f * px;
    const float barH = 6.0f * px;
    const float barGap = 3.0f * px;
    const bool showGr = grDb_ > 0.08f;
    const float blockH = 2.0f * barH + barGap + (showGr ? 13.0f * px : 0.0f);
    const float topY = (height() - blockH) * 0.5f;

    const auto drawMeterBar = [&](float y, float peak) {
        const float db = peak > 1.0e-5f ? 20.0f * std::log10(peak) : -60.0f;
        const float norm = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        canvas.setColor(colour::track);
        canvas.roundedRectangle(meterX, y, meterW, barH, 3.0f * px);
        if (norm > 0.005f) {
            const unsigned barCol = db > -1.0f ? colour::rose : db > -6.0f ? colour::amber : colour::emerald;
            canvas.setColor(barCol);
            canvas.roundedRectangle(meterX, y, meterW * norm, barH, 3.0f * px);
        }
    };

    drawMeterBar(topY, peakL_);
    drawMeterBar(topY + barH + barGap, peakR_);

    if (showGr) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "-%.1fdB", grDb_);
        drawLabel(canvas, context_, buf, meterX, topY + 2.0f * barH + barGap + 2.0f * px, meterW, 11.0f * px,
                  colour::amber, visage::Font::kRight, 9.0f, Weight::semibold);
    }
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float bh = px(28.0f);
    const float y = (height() - bh) * 0.5f;
    const float arrow = px(26.0f);
    const float gap = px(4.0f);

    wordmark_.setBounds(px(10.0f), px(4.0f), px(136.0f), height() - px(8.0f));

    float x = px(158.0f);
    previous_.setBounds(x, y, arrow, bh);
    x += arrow + gap;
    presetName_.setBounds(x, y, px(216.0f), bh);
    x += px(216.0f) + gap;
    next_.setBounds(x, y, arrow, bh);
    x += arrow + px(8.0f);
    save_.setBounds(x, y, px(50.0f), bh);
    x += px(50.0f) + px(14.0f);

    undo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + gap;
    redo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + px(8.0f);
    ab_.setBounds(x, y, px(34.0f), bh);

    // Right side: [Pad | Classic] [Sub Anchor] [LIM] [Peak Meter]
    float rx = width() - px(108.0f); // reserve 84px meter + 24px margin
    const float limW = px(58.0f);
    rx -= limW;
    limiter_.setBounds(rx, y, limW, bh);

    rx -= px(8.0f);
    const float subW = px(124.0f);
    rx -= subW;
    subAnchor_.setBounds(rx, y, subW, bh);

    rx -= px(12.0f);
    const float surfW = px(150.0f);
    rx -= surfW;
    surface_.setBounds(rx, y, surfW, bh);
}

} // namespace lc::ui
