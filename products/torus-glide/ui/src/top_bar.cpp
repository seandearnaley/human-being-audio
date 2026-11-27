// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/tg_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace tg::ui {

namespace {
float linearToDb(float v) { return v <= 1e-5f ? -100.0f : 20.0f * std::log10(v); }
} // namespace

TopBar::TopBar(Context& ctx, std::function<void()> showAbout, std::function<void(bool)> onViewChanged)
    : context_(ctx), onViewChanged_(std::move(onViewChanged)),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Sidewinder F Bass 4Bar", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }), keyChip_(ctx, ParamId::rootKey, colour::spiral, "Key"),
      scaleChip_(ctx, ParamId::scale, colour::copedent),
      drone_(ctx, "DRONE",
             [this] {
                 const bool next = context_.model.get(ParamId::padHold) < 0.5f;
                 context_.model.begin(ParamId::padHold);
                 context_.model.set(ParamId::padHold, next ? 1.0f : 0.0f);
                 context_.model.end(ParamId::padHold);
             }),
      dragWav_(ctx, "Bounce .wav", [this] { context_.model.startResample(); }),
      dragMid_(ctx, "Drag .mid", [this] { context_.model.dragExportedMidi(); }),
      viewPlay_(ctx, "PLAY", [this] { setStudioView(false); }),
      viewStudio_(ctx, "STUDIO", [this] { setStudioView(true); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(colour::text);
    nextPreset_.setAccent(colour::textDim);
    drone_.setAccent(colour::mobius);
    dragWav_.setAccent(colour::spiral);
    dragMid_.setAccent(colour::copedent);
    viewPlay_.setAccent(colour::spiral);
    viewStudio_.setAccent(colour::textDim);
    viewPlay_.setActive(true);
    viewStudio_.setActive(false);

    dragWav_.setOnDragOut([this] {
        if (context_.model.resampleState() == ResampleState::ready)
            context_.model.dragResampledWav();
    });
    dragMid_.setOnDragOut([this] { context_.model.dragExportedMidi(); });

    ab_.setAlternateAction([this] { context_.model.copyAB(); });
    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    for (visage::Frame* child : std::initializer_list<visage::Frame*>{&prevPreset_, &presetName_, &nextPreset_, &undo_,
                                                                      &redo_, &ab_, &keyChip_, &scaleChip_, &drone_,
                                                                      &dragWav_, &dragMid_, &viewPlay_, &viewStudio_}) {
        addChild(child);
    }

    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::setStudioView(bool studio) {
    studioView_ = studio;
    const int padProg =
        std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::padProgram))), 0, kPadProgramCount - 1);
    viewPlay_.setAccent(colour::padAccent(padProg));
    viewStudio_.setAccent(studio ? colour::copedent : colour::textDim);
    viewPlay_.setActive(!studio);
    viewStudio_.setActive(studio);
    if (onViewChanged_)
        onViewChanged_(studio);
    redraw();
}

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        presetName_.setLabel(shownPreset_.empty() ? "Init" : shownPreset_);
    }

    const int padProg =
        std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::padProgram))), 0, kPadProgramCount - 1);
    const unsigned activeAcc = colour::padAccent(padProg);
    if (!studioView_)
        viewPlay_.setAccent(activeAcc);
    drone_.setAccent(activeAcc);

    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::spiral, colour::copedent);

    const bool droneOn = context_.model.get(ParamId::padHold) >= 0.5f || context_.model.isAuditioning();
    drone_.setActive(droneOn);
    drone_.setLabel(droneOn ? "DRONE ON" : "DRONE");

    const auto rstate = context_.model.resampleState();
    hba::ui::syncWavExport(dragWav_, rstate);

    const auto& tel = context_.model.visuals();
    const float nextL = linearToDb(tel.peakL);
    const float nextR = linearToDb(tel.peakR);
    peakDbL_ = std::max(nextL, peakDbL_ - 1.2f);
    peakDbR_ = std::max(nextR, peakDbR_ - 1.2f);
    const float nextGr = tel.limiterGain < 0.999f ? -linearToDb(tel.limiterGain) : 0.0f;
    grDb_ = std::max(nextGr, grDb_ * 0.85f);
    redraw();
}

void TopBar::showPresetMenu() { hba::ui::showStudioPresetMenu(context_.model, &presetName_); }

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::torus_glide_logotype_on_dark_svg, 2100.0f / 334.0f, 23.0f,
                               6.0f, context_.scale);
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

    // Compact right-aligned canonical stereo output meter
    const float px = context_.scale;
    const float meterW = 112.0f * px;
    const float meterH = 26.0f * px;
    const float meterX = width() - meterW - 8.0f * px;
    const float meterY = (height() - meterH) * 0.5f;
    canvas.saveState();
    canvas.setPosition(meterX, meterY);
    hba::ui::drawStereoOutputMeter(canvas, px, 2.0f * px, meterW, peakDbL_, peakDbR_, grDb_, -0.5f, colour::drive);
    canvas.restoreState();
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(30.0f);
    const float y = (h - bh) * 0.5f;
    const float gap = px(6.0f);

    wordmark_.setBounds(px(4.0f), 0.0f, px(160.0f), h);

    // Preset navigator
    float x = px(172.0f);
    const float arrW = px(24.0f);
    const float nameW = px(188.0f);
    prevPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(2.0f);
    presetName_.setBounds(x, y, nameW, bh);
    x += nameW + px(2.0f);
    nextPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(10.0f);

    // Undo / Redo / A/B
    undo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + px(3.0f);
    redo_.setBounds(x, y, px(44.0f), bh);
    x += px(44.0f) + px(3.0f);
    ab_.setBounds(x, y, px(28.0f), bh);
    x += px(28.0f) + px(12.0f);

    // Musical Key & Scale
    keyChip_.setBounds(x, y, px(60.0f), bh);
    x += px(60.0f) + px(4.0f);
    scaleChip_.setBounds(x, y, px(96.0f), bh);

    // Right-aligned: DRONE, Bounce .wav, Drag .mid, PLAY | STUDIO, Stereo Meter
    float rx = width() - px(126.0f); // reserve right 126px for Stereo Output Meter
    const float studioW = px(64.0f);
    const float playW = px(58.0f);
    rx -= studioW;
    viewStudio_.setBounds(rx, y, studioW, bh);
    rx -= px(3.0f) + playW;
    viewPlay_.setBounds(rx, y, playW, bh);

    rx -= px(12.0f) + px(78.0f);
    dragMid_.setBounds(rx, y, px(78.0f), bh);

    rx -= gap + px(94.0f);
    dragWav_.setBounds(rx, y, px(94.0f), bh);

    rx -= gap + px(80.0f);
    drone_.setBounds(rx, y, px(80.0f), bh);
}

} // namespace tg::ui
