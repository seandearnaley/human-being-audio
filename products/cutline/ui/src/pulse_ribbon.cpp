// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pulse_ribbon.h"

#include "grid_edit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace cl::ui {

namespace {
constexpr std::array<const char*, 3> kLedNames{"NOTE", "CC", "XY"};
constexpr std::array<unsigned, 3> kLedColours{colour::emerald, colour::cyan, colour::coral};
} // namespace

PulseRibbon::PulseRibbon(Context& context, std::function<void()> onOpenGrid)
    : context_(context), onOpenGrid_(std::move(onOpenGrid)) {
    context_.live.push_back(this);
}

PulseRibbon::~PulseRibbon() { std::erase(context_.live, static_cast<Live*>(this)); }

void PulseRibbon::tick() {
    if (!isVisible())
        return;
    const auto& v = context_.model.visuals();
    const std::array<bool, 3> lit{
        (v.activityMask & 0x01u) != 0u,
        (v.activityMask & 0x02u) != 0u,
        v.padEngaged || ((v.activityMask & 0x04u) != 0u),
    };
    for (std::size_t i = 0; i < 3; ++i)
        ledHold_[i] = lit[i] ? 1.0f : ledHold_[i] * 0.86f;
    redraw();
}

void PulseRibbon::mouseEnter(const visage::MouseEvent&) {
    hoveredTimeline_ = true;
    redraw();
}

void PulseRibbon::mouseExit(const visage::MouseEvent&) {
    hoveredTimeline_ = false;
    redraw();
}

void PulseRibbon::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    const float pillW = context_.px(286.0f);
    if (e.position.x < width() - pillW - context_.px(16.0f) && onOpenGrid_)
        onOpenGrid_();
}

void PulseRibbon::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float px = context_.scale;

    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::panel, metric::radius);

    const auto& v = context_.model.visuals();
    const int prog = std::clamp((v.padEngaged || v.pendingPadProgram >= 0)
                                    ? v.activePadProgram
                                    : static_cast<int>(context_.model.get(ParamId::padProgram) + 0.5f),
                                0, kPerfPadCount - 1);
    const unsigned accent = colour::programArgb(prog);
    const auto mode =
        static_cast<EngineMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f), 0, 1));

    const float padX = 10.0f * px;
    const float padY = 6.0f * px;
    const float pillW = 286.0f * px;
    const float wellX = padX;
    const float wellY = padY;
    const float wellW = std::max(80.0f * px, w - pillW - 3.0f * padX);
    const float wellH = std::max(20.0f * px, h - 2.0f * padY);

    // 1. Left Live Polymetric Pulse Timeline Well
    canvas.setColor(colour::window);
    canvas.roundedRectangle(wellX, wellY, wellW, wellH, 6.0f * px);

    const float innerX = wellX + 6.0f * px;
    const float innerY = wellY + 4.0f * px;
    const float innerW = wellW - 12.0f * px;
    const float innerH = wellH - 8.0f * px;

    // The scene being shown: the one playing, or the one the scene parameter selects.
    const GrooveState& groove = context_.model.groove();
    const int scIdx = std::clamp(
        v.playing ? v.activeScene : static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(scIdx)];
    const int slLen = std::clamp(static_cast<int>(groove.bank.slicerLength), 1, kMaxSlicerSteps);
    const int steps = mode == EngineMode::drum ? patternSteps(pattern) : kMaxSlicerSteps;
    const float stepW = innerW / static_cast<float>(steps);
    const int absStep = v.position.barInPattern * kStepsPerBar + v.position.stepInBar;
    const int playCol = !v.playing ? -1 : (mode == EngineMode::drum ? absStep : absStep % slLen);

    // Beat dividers every 4 steps, bar lines every 16.
    for (int st = 0; st < steps; st += 4) {
        const float sx = innerX + static_cast<float>(st) * stepW;
        const bool isBar = (st % kStepsPerBar) == 0;
        canvas.setColor(isBar ? colour::withAlpha(accent, 0.28f) : 0x12ffffffu);
        canvas.rectangle(sx, wellY + 2.0f * px, (isBar ? 1.4f : 1.0f) * px, wellH - 4.0f * px);
    }

    if (mode == EngineMode::drum) {
        // What sounds, from the same function and the same maps as the engine.
        const float density = context_.model.get(ParamId::density);
        const float dynamics = context_.model.get(ParamId::dynamics);
        const int liveFillBar = (v.playing && v.position.fillLive) ? v.position.barInPattern : -1;
        const grid::View view = grid::resolveView(groove, scIdx, density, dynamics, liveFillBar);
        const float laneH = innerH / static_cast<float>(kDrumLaneCount);
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const auto li = static_cast<std::size_t>(l);
            const float ly = innerY + static_cast<float>(l) * laneH;
            const unsigned hue = colour::lane[li];
            const float flash = v.laneFlash[li];

            for (int st = 0; st < steps; ++st) {
                const grid::Cell cell = grid::cellOf(view.bar[static_cast<std::size_t>(st / kStepsPerBar)]
                                                         .lanes[li][static_cast<std::size_t>(st % kStepsPerBar)]);
                if (cell.look == grid::CellLook::empty || cell.look == grid::CellLook::outline)
                    continue; // the ribbon shows only what sounds
                const unsigned tone = cell.look == grid::CellLook::fill ? colour::amber : hue;
                const float vNorm = static_cast<float>(cell.velocity) / 127.0f;
                const bool isPlay = st == playCol;
                const float alpha = isPlay ? 1.0f : (0.34f + 0.56f * vNorm + 0.25f * flash);
                const float bx = innerX + static_cast<float>(st) * stepW + 1.0f * px;
                const float bw = std::max(2.0f * px, stepW - 2.2f * px);
                const float bh = std::max(2.0f * px, laneH - 1.2f * px);
                canvas.setColor(isPlay ? 0xffffffffu : colour::withAlpha(tone, std::min(1.0f, alpha)));
                canvas.roundedRectangle(bx, ly + 0.5f * px, bw, bh, 1.2f * px);
            }
        }
    } else {
        // MIDI Slicer: the literal gate strip up to its length, and the held notes.
        for (int st = 0; st < slLen; ++st) {
            const Step& step = groove.bank.slicerSteps[static_cast<std::size_t>(st)];
            if (!step.on())
                continue;
            const float vNorm = static_cast<float>(step.velocity) / 127.0f;
            const float bh = innerH * (0.24f + 0.72f * vNorm);
            const bool isPlay = st == playCol;
            canvas.setColor(colour::withAlpha(isPlay ? 0xffffffffu : colour::cyan, 0.72f));
            canvas.roundedRectangle(innerX + static_cast<float>(st) * stepW + 1.5f * px, innerY + (innerH - bh),
                                    std::max(3.0f * px, stepW - 3.0f * px), bh, 2.0f * px);
        }
        if (v.heldNotes.count > 0) {
            float nx = innerX + 2.0f * px;
            for (int i = 0; i < std::min(v.heldNotes.count, 6); ++i) {
                const std::string lbl = midiNoteLabel(static_cast<int>(v.heldNotes.notes[static_cast<std::size_t>(i)]));
                canvas.setColor(0xd00d1118u);
                canvas.roundedRectangle(nx, innerY + 2.0f * px, 34.0f * px, 16.0f * px, 3.0f * px);
                canvas.setColor(colour::cyan);
                canvas.roundedRectangleBorder(nx, innerY + 2.0f * px, 34.0f * px, 16.0f * px, 3.0f * px, 1.0f * px);
                drawLabel(canvas, context_, lbl, nx, innerY + 2.0f * px, 34.0f * px, 16.0f * px, colour::text,
                          visage::Font::kCenter, 9.0f, Weight::semibold);
                nx += 38.0f * px;
            }
        } else {
            drawLabel(canvas, context_, "MIDI SLICER \u00b7 Feed chords or clips from Live 12", innerX + 4.0f * px,
                      innerY, innerW - 8.0f * px, innerH, colour::withAlpha(colour::cyan, 0.75f), visage::Font::kLeft,
                      10.5f, Weight::medium);
        }
    }

    // Playhead needle at the left edge of the step telemetry reports.
    if (playCol >= 0 && playCol < steps) {
        const float pxPos = innerX + static_cast<float>(playCol) * stepW;
        canvas.setColor(colour::withAlpha(accent, 0.34f));
        canvas.rectangle(pxPos - 2.5f * px, wellY + 1.0f * px, 5.0f * px, wellH - 2.0f * px);
        canvas.setColor(0xffffffffu);
        canvas.rectangle(pxPos - 0.8f * px, wellY + 1.0f * px, 1.6f * px, wellH - 2.0f * px);
    }

    // Hover callout to jump to Grid
    if (hoveredTimeline_) {
        const float tagW = 86.0f * px;
        const float tagH = 18.0f * px;
        const float tagX = wellX + wellW - tagW - 6.0f * px;
        const float tagY = wellY + (wellH - tagH) * 0.5f;
        canvas.setColor(0xe60d1118u);
        canvas.roundedRectangle(tagX, tagY, tagW, tagH, 4.0f * px);
        canvas.setColor(colour::withAlpha(accent, 0.80f));
        canvas.roundedRectangleBorder(tagX, tagY, tagW, tagH, 4.0f * px, 1.0f * px);
        drawLabel(canvas, context_, "EDIT GRID \u2197", tagX, tagY, tagW, tagH, colour::text, visage::Font::kCenter,
                  9.0f, Weight::semibold);
    }

    canvas.setColor(hoveredTimeline_ ? colour::withAlpha(accent, 0.65f) : colour::edge);
    canvas.roundedRectangleBorder(wellX, wellY, wellW, wellH, 6.0f * px, 1.0f * px);

    // 2. Right Telemetry Pill: Style / Scene + BPM + Live MIDI LEDs
    const float pillX = wellX + wellW + padX;
    canvas.setColor(colour::raised);
    canvas.roundedRectangle(pillX, wellY, pillW, wellH, 6.0f * px);
    canvas.setColor(v.padEngaged ? colour::withAlpha(accent, 0.65f) : colour::edge);
    canvas.roundedRectangleBorder(pillX, wellY, pillW, wellH, 6.0f * px, 1.0f * px);

    // Status beacon dot
    const float dotD = 7.0f * px;
    canvas.setColor(v.playing ? accent : colour::textFaint);
    canvas.circle(pillX + 10.0f * px, wellY + (wellH - dotD) * 0.5f, dotD);

    // Scene headline
    const char sceneChar = static_cast<char>('A' + scIdx);
    char headBuf[48]{};
    if (mode == EngineMode::drum) {
        const int bars = patternBars(pattern);
        std::snprintf(headBuf, sizeof(headBuf), "SCN %c \u00b7 %d %s", sceneChar, bars, bars == 1 ? "bar" : "bars");
    } else {
        const int sIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::slicerStyle) + 0.5f), 0, 4);
        std::snprintf(headBuf, sizeof(headBuf), "%s",
                      std::string(choices::slicerStyle[static_cast<std::size_t>(sIdx)]).c_str());
    }
    drawLabel(canvas, context_, headBuf, pillX + 22.0f * px, wellY + 4.0f * px, 134.0f * px, 16.0f * px, colour::text,
              visage::Font::kLeft, 10.5f, Weight::semibold);

    char subBuf[48]{};
    const int rateIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::seqRate) + 0.5f), 0, 2);
    if (rateIdx == 1)
        std::snprintf(subBuf, sizeof(subBuf), "%.0f BPM", v.bpm > 1.0 ? v.bpm : 172.0);
    else
        std::snprintf(subBuf, sizeof(subBuf), "%.0f BPM \u00b7 %s", v.bpm > 1.0 ? v.bpm : 172.0,
                      std::string(choices::seqRate[static_cast<std::size_t>(rateIdx)]).c_str());
    drawLabel(canvas, context_, subBuf, pillX + 22.0f * px, wellY + 21.0f * px, 126.0f * px, 14.0f * px,
              rateIdx == 0 ? colour::amber : (rateIdx == 2 ? colour::cyan : colour::textDim), visage::Font::kLeft, 9.5f,
              rateIdx == 1 ? Weight::medium : Weight::semibold);

    // 3 Crisp Micro Activity LEDs (NOTE, CC, XY)
    float lx = pillX + pillW - 8.0f * px;
    const float ledH = 18.0f * px;
    const float ledY = wellY + (wellH - ledH) * 0.5f;
    for (int i = 2; i >= 0; --i) {
        const float ledW = (i == 0 ? 38.0f : 28.0f) * px;
        lx -= ledW;
        const float glow = ledHold_[static_cast<std::size_t>(i)];
        const unsigned c = (i == 2) ? accent : kLedColours[static_cast<std::size_t>(i)];
        canvas.setColor(colour::withAlpha(c, 0.10f + 0.36f * glow));
        canvas.roundedRectangle(lx, ledY, ledW, ledH, 3.5f * px);
        canvas.setColor(colour::withAlpha(c, 0.28f + 0.72f * glow));
        canvas.roundedRectangleBorder(lx, ledY, ledW, ledH, 3.5f * px, 1.0f * px);
        drawLabel(canvas, context_, kLedNames[static_cast<std::size_t>(i)], lx, ledY, ledW, ledH,
                  glow > 0.25f ? colour::text : colour::textDim, visage::Font::kCenter, 8.5f, Weight::semibold);
        lx -= 4.0f * px;
    }
}

} // namespace cl::ui
