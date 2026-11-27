// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/pv_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace pv::ui {

namespace {
float linearToDb(float v) { return v <= 1e-5f ? -100.0f : 20.0f * std::log10(v); }
} // namespace

TopBar::TopBar(Context& ctx, std::function<void()> showAbout, std::function<void(Editor::View)> onViewChanged)
    : context_(ctx), onViewChanged_(std::move(onViewChanged)),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Iridescent F Min9", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }),
      viewColor_(ctx, "COLOR", [this] { setView(Editor::View::color); }),
      viewLattice_(ctx, "LATTICE", [this] { setView(Editor::View::lattice); }),
      freezeBtn_(ctx, "FREEZE",
                 [this] {
                     const float next = context_.model.get(ParamId::freeze) >= 0.5f ? 0.0f : 1.0f;
                     context_.model.begin(ParamId::freeze);
                     context_.model.set(ParamId::freeze, next);
                     context_.model.end(ParamId::freeze);
                 }),
      holdBtn_(ctx, "HOLD",
               [this] {
                   const float next = context_.model.get(ParamId::midiHold) >= 0.5f ? 0.0f : 1.0f;
                   context_.model.begin(ParamId::midiHold);
                   context_.model.set(ParamId::midiHold, next);
                   context_.model.end(ParamId::midiHold);
               }),
      demoBtn_(ctx, "DEMO", [this] {
          const float next = context_.model.get(ParamId::demoAudition) >= 0.5f ? 0.0f : 1.0f;
          context_.model.begin(ParamId::demoAudition);
          context_.model.set(ParamId::demoAudition, next);
          context_.model.end(ParamId::demoAudition);
      }) {
    prevPreset_.setName("Previous preset");
    nextPreset_.setName("Next preset");
    presetName_.setAccessibilityPrefix("Preset");
    ab_.setAccessibilityPrefix("Compare slot");
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(colour::text);
    nextPreset_.setAccent(colour::textDim);

    viewColor_.setFontSize(10.2f);
    viewLattice_.setFontSize(10.2f);
    viewColor_.setAccent(colour::prism);
    viewLattice_.setAccent(colour::textDim);
    viewColor_.setActive(true);
    viewLattice_.setActive(false);

    freezeBtn_.setAccent(colour::rose);
    freezeBtn_.setFontSize(10.0f);
    holdBtn_.setAccent(colour::harmonic);
    holdBtn_.setFontSize(10.0f);
    demoBtn_.setAccent(colour::sub);
    demoBtn_.setFontSize(10.0f);

    ab_.setAlternateAction([this] { context_.model.copyAB(); });
    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&prevPreset_, &presetName_, &nextPreset_, &undo_, &redo_, &ab_,
                                               &viewColor_, &viewLattice_, &freezeBtn_, &holdBtn_, &demoBtn_}) {
        addChild(child);
    }

    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::setView(Editor::View v) {
    view_ = v;
    viewColor_.setAccent(v == Editor::View::color ? colour::prism : colour::textDim);
    viewLattice_.setAccent(v == Editor::View::lattice ? colour::harmonic : colour::textDim);
    viewColor_.setActive(v == Editor::View::color);
    viewLattice_.setActive(v == Editor::View::lattice);
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
                                context_.model.isSlotB(), colour::prism, colour::tame);

    freezeBtn_.setActive(context_.model.get(ParamId::freeze) >= 0.5f);
    holdBtn_.setActive(context_.model.get(ParamId::midiHold) >= 0.5f);
    demoBtn_.setActive(context_.model.get(ParamId::demoAudition) >= 0.5f);

    const auto& tel = context_.model.visuals();
    const float nextL = linearToDb(tel.peakL);
    const float nextR = linearToDb(tel.peakR);
    peakDbL_ = std::max(nextL, peakDbL_ - 1.3f);
    peakDbR_ = std::max(nextR, peakDbR_ - 1.3f);
    grDb_ = std::min(tel.grDb, grDb_ * 0.85f);
    redraw();
}

void TopBar::showPresetMenu() { hba::ui::showStudioPresetMenu(context_.model, &presetName_); }

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::prism_vein_logotype_on_dark_svg, 1660.0f / 334.0f, 24.0f, 8.0f,
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
    hba::ui::drawStereoOutputMeter(canvas, sc, 2.0f * sc, meterW, peakDbL_, peakDbR_, std::abs(grDb_),
                                   context_.model.get(ParamId::ceilingDb), colour::alert);
    canvas.restoreState();
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(30.0f);
    const float y = (h - bh) * 0.5f;

    wordmark_.setBounds(px(4.0f), 0.0f, px(136.0f), h);

    // Preset navigator
    float x = px(146.0f);
    const float arrW = px(24.0f);
    const float nameW = px(208.0f);
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

    // Right-aligned: COLOR | LATTICE · FREEZE · HOLD · DEMO · Meter
    float rx = width() - px(128.0f);

    const float demoW = px(58.0f);
    rx -= demoW;
    demoBtn_.setBounds(rx, y, demoW, bh);

    const float holdW = px(56.0f);
    rx -= px(5.0f) + holdW;
    holdBtn_.setBounds(rx, y, holdW, bh);

    const float freezeW = px(68.0f);
    rx -= px(5.0f) + freezeW;
    freezeBtn_.setBounds(rx, y, freezeW, bh);

    rx -= px(16.0f);
    const float latticeW = px(82.0f);
    const float colorW = px(74.0f);
    rx -= latticeW;
    viewLattice_.setBounds(rx, y, latticeW, bh);
    rx -= px(3.0f) + colorW;
    viewColor_.setBounds(rx, y, colorW, bh);
}

} // namespace pv::ui
