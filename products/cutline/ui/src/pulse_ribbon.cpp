// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pulse_ribbon.h"

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
    const int prog = std::clamp(
        (v.padEngaged || v.pendingPadProgram >= 0) ? v.activePadProgram
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
    const float stepW = innerW / static_cast<float>(kMaxLaneSteps);

    // Subtle bar & beat dividers
    for (int st = 0; st < kMaxLaneSteps; st += 4) {
        const float sx = innerX + static_cast<float>(st) * stepW;
        const bool isBar = (st % 16) == 0;
        canvas.setColor(isBar ? colour::withAlpha(accent, 0.28f) : 0x12ffffffu);
        canvas.rectangle(sx, wellY + 2.0f * px, (isBar ? 1.4f : 1.0f) * px, wellH - 4.0f * px);
    }

    if (mode == EngineMode::drum) {
        const float laneH = innerH / static_cast<float>(kDrumLaneCount);
        const int playStep32 = static_cast<int>(std::floor(v.playheadBeats * 4.0));
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const auto li = static_cast<std::size_t>(l);
            const float ly = innerY + static_cast<float>(l) * laneH;
            const unsigned hue = colour::lane[li];
            const int len = std::clamp(static_cast<int>(v.plan.laneLengths[li]), 1, kMaxLaneSteps);
            const float flash = v.laneFlash[li];
            const int activeCell = v.playing ? (playStep32 % len) : -1;

            for (int st = 0; st < kMaxLaneSteps; ++st) {
                const bool wrapped = (st >= len);
                const int src = st % len;
                const std::uint8_t vel = v.plan.gridVel[li][static_cast<std::size_t>(src)];
                if (vel == 0)
                    continue;
                const float vNorm = static_cast<float>(vel) / 127.0f;
                const bool isPlay = (!wrapped && st == activeCell);
                const float alpha = wrapped ? 0.14f : (isPlay ? 1.0f : (0.34f + 0.56f * vNorm + 0.25f * flash));
                const float bx = innerX + static_cast<float>(st) * stepW + 1.0f * px;
                const float bw = std::max(2.0f * px, stepW - 2.2f * px);
                const float bh = std::max(2.0f * px, laneH - 1.2f * px);
                canvas.setColor(isPlay ? 0xffffffffu : colour::withAlpha(hue, std::min(1.0f, alpha)));
                canvas.roundedRectangle(bx, ly + 0.5f * px, bw, bh, 1.2f * px);
            }

            // Crisp polymetric loop boundary tick when lane != 16/32
            if (len != 16 && len != 32) {
                const float ex = innerX + static_cast<float>(len) * stepW;
                canvas.setColor(colour::withAlpha(colour::cyan, 0.78f));
                canvas.rectangle(ex - 0.8f * px, ly, 1.8f * px, laneH);
            }
        }
    } else {
        // Slicer Mode: Live chord pills + sliced step gate bars
        const int slLen = std::clamp(static_cast<int>(v.plan.slicerLength), 1, kMaxLaneSteps);
        const int playStep = v.playing ? (static_cast<int>(std::floor(v.playheadBeats * 4.0)) % slLen) : -1;
        for (int st = 0; st < kMaxLaneSteps; ++st) {
            const bool wrapped = (st >= slLen);
            const std::uint8_t vel = v.plan.slicerVel[static_cast<std::size_t>(st % slLen)];
            if (vel == 0)
                continue;
            const float vNorm = static_cast<float>(vel) / 127.0f;
            const float bh = innerH * (0.24f + 0.72f * vNorm);
            const bool isPlay = (!wrapped && st == playStep);
            canvas.setColor(colour::withAlpha(isPlay ? 0xffffffffu : colour::cyan, wrapped ? 0.14f : 0.72f));
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

    // Live playhead needle
    if (v.playing) {
        const double beatIn2Bars = std::fmod(std::max(0.0, v.playheadBeats), 8.0);
        const float pxPos = innerX + static_cast<float>(beatIn2Bars * 4.0) * stepW;
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

    // Style & Scene headline
    const int styleIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::style) + 0.5f), 0, kDrumStyleCount - 1);
    const int scIdx = std::clamp(v.playing ? v.activeScene : static_cast<int>(context_.model.get(ParamId::scene) + 0.5f),
                                 0, kSceneCount - 1);
    const char sceneChar = static_cast<char>('A' + scIdx);
    char headBuf[48]{};
    if (mode == EngineMode::drum) {
        std::snprintf(headBuf, sizeof(headBuf), "%s \u00b7 SCN %c",
                      std::string(choices::style[static_cast<std::size_t>(styleIdx)]).c_str(), sceneChar);
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
