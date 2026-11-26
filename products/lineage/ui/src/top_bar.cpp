// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/ln_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace ln::ui {

namespace {
float linearToDb(float v) { return v <= 1e-5f ? -100.0f : 20.0f * std::log10(v); }
} // namespace

TopBar::TopBar(Context& ctx, std::function<void()> showAbout, std::function<void(bool)> onViewChanged)
    : context_(ctx), onViewChanged_(std::move(onViewChanged)),
      prevPreset_(ctx, "\xe2\x80\xb9", [this] { context_.model.stepPreset(-1); }),
      presetName_(ctx, "Init", [this] { showPresetMenu(); }),
      nextPreset_(ctx, "\xe2\x80\xba", [this] { context_.model.stepPreset(1); }),
      undo_(ctx, "Undo", [this] { context_.model.undo(); }), redo_(ctx, "Redo", [this] { context_.model.redo(); }),
      ab_(ctx, "A", [this] { context_.model.toggleAB(); }),
      printGen_(ctx, "PRINT GEN", [this] { context_.model.triggerPrint(false); }),
      drone_(ctx, "DRONE",
             [this] {
                 const bool next = context_.model.get(ParamId::padHold) < 0.5f;
                 context_.model.begin(ParamId::padHold);
                 context_.model.set(ParamId::padHold, next ? 1.0f : 0.0f);
                 context_.model.end(ParamId::padHold);
             }),
      bounceWav_(ctx, "Bounce .wav", [this] { context_.model.startWavExport(); }),
      viewPlay_(ctx, "PLAY", [this] { setStudioView(false); }),
      viewStudio_(ctx, "STUDIO", [this] { setStudioView(true); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });

    prevPreset_.setAccent(colour::textDim);
    presetName_.setAccent(colour::text);
    nextPreset_.setAccent(colour::textDim);
    printGen_.setAccent(colour::strata);
    drone_.setAccent(colour::sub);
    bounceWav_.setAccent(colour::reese);
    viewPlay_.setAccent(colour::sculpt);
    viewStudio_.setAccent(colour::textDim);
    viewPlay_.setActive(true);
    viewStudio_.setActive(false);

    bounceWav_.setOnDragOut([this] {
        if (context_.model.exportState() == ExportState::ready)
            context_.model.dragExportedWav();
    });

    ab_.setOnRightClick([this] {
        hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }, false);
    });

    drone_.setOnRightClick([this] {
        visage::PopupMenu menu;
        for (int i = 0; i < static_cast<int>(choices::droneNote.size()); ++i)
            menu.addOption(i, "Drone Key: " + std::string(choices::droneNote[static_cast<std::size_t>(i)]));
        menu.onSelection() = [this](int idx) {
            context_.model.begin(ParamId::droneNote);
            context_.model.set(ParamId::droneNote, static_cast<float>(idx));
            context_.model.end(ParamId::droneNote);
        };
        menu.show(&drone_);
    });

    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&prevPreset_, &presetName_, &nextPreset_, &undo_, &redo_, &ab_,
                                               &printGen_, &drone_, &bounceWav_, &viewPlay_, &viewStudio_}) {
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
    viewStudio_.setAccent(studio ? colour::reese : colour::textDim);
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
                                context_.model.isSlotB(), colour::sculpt, colour::reese);

    const auto pstate = context_.model.printState();
    const int gen = context_.model.printGeneration();
    if (pstate == PrintState::printing) {
        printGen_.setLabel("Printing...");
        printGen_.setActive(true);
    } else if (gen >= 2) {
        printGen_.setLabel("GEN " + std::to_string(gen) + " \xc2\xb7 READY");
        printGen_.setActive(false);
    } else {
        printGen_.setLabel("PRINT GEN");
        printGen_.setActive(false);
    }

    const bool droneOn = context_.model.get(ParamId::padHold) >= 0.5f;
    const int droneKeyIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::droneNote))), 0, 11);
    const std::string keyName(choices::droneNote[static_cast<std::size_t>(droneKeyIdx)]);
    drone_.setActive(droneOn);
    drone_.setLabel(droneOn ? ("ON \xc2\xb7 " + keyName) : ("DRONE \xc2\xb7 " + keyName));

    const auto estate = context_.model.exportState();
    if (estate == ExportState::rendering) {
        bounceWav_.setLabel("Bouncing...");
        bounceWav_.setActive(false);
    } else if (estate == ExportState::ready) {
        bounceWav_.setLabel("Drag .wav \xe2\x86\x97");
        bounceWav_.setActive(true);
    } else {
        bounceWav_.setLabel("Bounce .wav");
        bounceWav_.setActive(false);
    }

    const auto& tel = context_.model.visuals();
    const float nextL = linearToDb(tel.peakL);
    const float nextR = linearToDb(tel.peakR);
    peakDbL_ = std::max(nextL, peakDbL_ - 1.2f);
    peakDbR_ = std::max(nextR, peakDbR_ - 1.2f);
    const float nextGr = tel.limiterGain < 0.999f ? -linearToDb(tel.limiterGain) : 0.0f;
    grDb_ = std::max(nextGr, grDb_ * 0.85f);
    redraw();
}

void TopBar::showPresetMenu() {
    hba::ui::showCategorizedPresetMenu(
        context_.model.presets(), &presetName_, [this](int i) { context_.model.loadPreset(i); },
        [this] { context_.model.savePreset(); });
}

void TopBar::drawWordmark(visage::Canvas& canvas) {
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::lineage_logotype_on_dark_svg, 1520.0f / 334.0f, 24.0f, 8.0f,
                               context_.scale);
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

    const float sc = context_.scale;
    const float meterW = 116.0f * sc;
    const float meterH = 26.0f * sc;
    const float meterX = width() - meterW - 8.0f * sc;
    const float meterY = (height() - meterH) * 0.5f;
    canvas.saveState();
    canvas.setPosition(meterX, meterY);
    hba::ui::drawStereoOutputMeter(canvas, sc, 2.0f * sc, meterW, peakDbL_, peakDbR_, grDb_, -0.3f, colour::alert);
    canvas.restoreState();
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float h = height();
    const float bh = px(30.0f);
    const float y = (h - bh) * 0.5f;
    const float gap = px(6.0f);

    wordmark_.setBounds(px(4.0f), 0.0f, px(142.0f), h);

    // Preset navigator
    float x = px(154.0f);
    const float arrW = px(24.0f);
    const float nameW = px(214.0f);
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

    // Right-aligned: PRINT GEN, DRONE, Bounce .wav, PLAY | STUDIO, Stereo Meter
    float rx = width() - px(130.0f);
    const float studioW = px(66.0f);
    const float playW = px(58.0f);
    rx -= studioW;
    viewStudio_.setBounds(rx, y, studioW, bh);
    rx -= px(3.0f) + playW;
    viewPlay_.setBounds(rx, y, playW, bh);

    rx -= px(12.0f) + px(96.0f);
    bounceWav_.setBounds(rx, y, px(96.0f), bh);

    rx -= gap + px(92.0f);
    drone_.setBounds(rx, y, px(92.0f), bh);

    rx -= gap + px(112.0f);
    printGen_.setBounds(rx, y, px(112.0f), bh);
}

} // namespace ln::ui
