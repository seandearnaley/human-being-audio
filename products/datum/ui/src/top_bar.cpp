// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/dt_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace dt::ui {

namespace {
float linearToDb(float v) { return v <= 1e-5f ? -100.0f : 20.0f * std::log10(v); }
} // namespace

TopBar::TopBar(Context& ctx, std::function<void()> showAbout, std::function<void(Editor::View)> onViewChanged)
    : context_(ctx), onViewChanged_(std::move(onViewChanged)),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Neuro Core 52", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }),
      viewSculpt_(ctx, "SCULPT", [this] { setView(Editor::View::sculpt); }),
      viewAnatomy_(ctx, "ANATOMY", [this] { setView(Editor::View::anatomy); }),
      viewCompose_(ctx, "COMPOSE", [this] { setView(Editor::View::compose); }),
      hitBtn_(ctx, "HIT", [this] { context_.model.triggerAudition(-1, 0.98f); }),
      dragWav_(ctx, "Drag .wav", [this] { context_.model.startWavExport(view_ == Editor::View::compose); }),
      dragMid_(ctx, "Drag .mid", [this] { context_.model.dragPatternMidi(); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(colour::text);
    nextPreset_.setAccent(colour::textDim);

    viewSculpt_.setFontSize(10.2f);
    viewAnatomy_.setFontSize(10.2f);
    viewCompose_.setFontSize(10.2f);

    hitBtn_.setAccent(colour::sub);
    hitBtn_.setFontSize(10.4f);

    dragWav_.setAccent(colour::body);
    dragWav_.setFontSize(10.2f);
    dragWav_.setOnDragOut([this] {
        if (context_.model.wavExportState() == ExportState::ready)
            context_.model.dragExportedWav();
    });
    dragWav_.setOnRightClick([this] {
        visage::PopupMenu menu;
        menu.addOption(1, "Render 1-Shot .wav (Sample-0)");
        menu.addOption(2, "Render Pattern Loop .wav");
        menu.onSelection() = [this](int opt) {
            if (opt == 1)
                context_.model.startWavExport(false);
            else if (opt == 2)
                context_.model.startWavExport(true);
        };
        menu.show(&dragWav_);
    });

    dragMid_.setAccent(colour::impact);
    dragMid_.setFontSize(10.2f);
    dragMid_.setOnDragOut([this] { context_.model.dragPatternMidi(); });

    ab_.setAlternateAction([this] { context_.model.copyAB(); });
    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    for (visage::Frame* child : std::initializer_list<visage::Frame*>{&prevPreset_, &presetName_, &nextPreset_, &undo_,
                                                                      &redo_, &ab_, &viewSculpt_, &viewAnatomy_,
                                                                      &viewCompose_, &hitBtn_, &dragWav_, &dragMid_}) {
        addChild(child);
    }

    viewSculpt_.setAccent(colour::sub);
    viewAnatomy_.setAccent(colour::textDim);
    viewCompose_.setAccent(colour::textDim);
    viewSculpt_.setActive(true);
    viewAnatomy_.setActive(false);
    viewCompose_.setActive(false);
    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::setView(Editor::View v) {
    view_ = v;
    viewSculpt_.setAccent(v == Editor::View::sculpt ? colour::sub : colour::textDim);
    viewAnatomy_.setAccent(v == Editor::View::anatomy ? colour::body : colour::textDim);
    viewCompose_.setAccent(v == Editor::View::compose ? colour::impact : colour::textDim);
    viewSculpt_.setActive(v == Editor::View::sculpt);
    viewAnatomy_.setActive(v == Editor::View::anatomy);
    viewCompose_.setActive(v == Editor::View::compose);
    if (onViewChanged_)
        onViewChanged_(v);
    redraw();
}

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        presetName_.setLabel(shownPreset_.empty() ? "Init" : shownPreset_);
    }

    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::sub, colour::body);

    const auto estate = context_.model.wavExportState();
    hba::ui::syncWavExport(dragWav_, estate);

    const auto& tel = context_.model.visuals();
    const float nextL = linearToDb(tel.peakL);
    const float nextR = linearToDb(tel.peakR);
    peakDbL_ = std::max(nextL, peakDbL_ - 1.3f);
    peakDbR_ = std::max(nextR, peakDbR_ - 1.3f);
    grDb_ = std::max(tel.limiterGrDb, grDb_ * 0.85f);
    hitBtn_.setActive(tel.sounding);
    redraw();
}

void TopBar::showPresetMenu() { hba::ui::showStudioPresetMenu(context_.model, &presetName_); }

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::datum_logotype_on_dark_svg, 1260.0f / 334.0f, 24.0f, 8.0f,
                               context_.scale);
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

    const float sc = context_.scale;
    const float meterW = 114.0f * sc;
    const float meterH = 26.0f * sc;
    const float meterX = width() - meterW - 8.0f * sc;
    const float meterY = (height() - meterH) * 0.5f;
    canvas.saveState();
    canvas.setPosition(meterX, meterY);
    hba::ui::drawStereoOutputMeter(canvas, sc, 2.0f * sc, meterW, peakDbL_, peakDbR_, grDb_,
                                   context_.model.get(ParamId::ceilingDb), colour::alert);
    canvas.restoreState();
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(30.0f);
    const float y = (h - bh) * 0.5f;

    wordmark_.setBounds(px(4.0f), 0.0f, px(118.0f), h);

    // Preset navigator
    float x = px(128.0f);
    const float arrW = px(24.0f);
    const float nameW = px(198.0f);
    prevPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(2.0f);
    presetName_.setBounds(x, y, nameW, bh);
    x += nameW + px(2.0f);
    nextPreset_.setBounds(x, y, arrW, bh);
    x += arrW + px(10.0f);

    // Undo / Redo / A-B
    undo_.setBounds(x, y, px(42.0f), bh);
    x += px(42.0f) + px(3.0f);
    redo_.setBounds(x, y, px(42.0f), bh);
    x += px(42.0f) + px(3.0f);
    ab_.setBounds(x, y, px(28.0f), bh);

    // Right-aligned: SCULPT | ANATOMY | COMPOSE · HIT · Drag .wav · Drag .mid · Meter
    float rx = width() - px(128.0f);

    const float midW = px(74.0f);
    rx -= midW;
    dragMid_.setBounds(rx, y, midW, bh);

    const float wavW = px(88.0f);
    rx -= px(6.0f) + wavW;
    dragWav_.setBounds(rx, y, wavW, bh);

    const float hitW = px(52.0f);
    rx -= px(6.0f) + hitW;
    hitBtn_.setBounds(rx, y, hitW, bh);

    rx -= px(14.0f);
    const float composeW = px(80.0f);
    const float anatomyW = px(82.0f);
    const float sculptW = px(72.0f);
    rx -= composeW;
    viewCompose_.setBounds(rx, y, composeW, bh);
    rx -= px(3.0f) + anatomyW;
    viewAnatomy_.setBounds(rx, y, anatomyW, bh);
    rx -= px(3.0f) + sculptW;
    viewSculpt_.setBounds(rx, y, sculptW, bh);
}

} // namespace dt::ui
