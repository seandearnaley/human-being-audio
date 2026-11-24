// SPDX-License-Identifier: GPL-3.0-or-later
#include "phrase_tape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lc::ui {

PhraseTape::PhraseTape(Context& context) : context_(context) { context_.live.push_back(this); }

PhraseTape::~PhraseTape() { std::erase(context_.live, static_cast<Live*>(this)); }

void PhraseTape::tick() {
    const auto& v = context_.model.visuals();
    if (std::abs(v.positionInPhrase - lastPos_) > 1.0e-3 || std::abs(v.blockStart - lastBlockStart_) > 1.0e-3 ||
        v.padActiveProgram != lastProgram_ || v.headline != lastHeadline_ || v.cutting || v.padEngaged) {
        lastPos_ = v.positionInPhrase;
        lastBlockStart_ = v.blockStart;
        lastProgram_ = v.padActiveProgram;
        lastHeadline_ = v.headline;
        redraw();
    }
}

void PhraseTape::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float px = context_.scale;

    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::panel, metric::radius);

    const auto& v = context_.model.visuals();
    const bool onPad = context_.model.getNormalised("surface") < 0.5f;
    const unsigned accent = onPad ? colour::programArgb(v.padActiveProgram) : colour::accent;

    // Reserve right status pill area
    const float pillW = 248.0f * px;
    const float padX = 12.0f * px;
    const float padY = 6.0f * px;
    const float tapeX = padX;
    const float tapeY = padY;
    const float tapeW = std::max(10.0f, w - pillW - 3.0f * padX);
    const float tapeH = std::max(10.0f, h - 2.0f * padY);

    // Inner waveform recessed well
    canvas.setColor(colour::window);
    canvas.roundedRectangle(tapeX, tapeY, tapeW, tapeH, 6.0f * px);

    const int bars = std::max(1, v.phraseBars);
    const double phraseUnits = std::max(1.0, static_cast<double>(bars * std::max(1, v.unitsPerBar)));

    // Active block highlight band
    if (v.cutting && v.blockEnd > v.blockStart) {
        const float b0 = static_cast<float>(std::clamp(v.blockStart / phraseUnits, 0.0, 1.0));
        const float b1 = static_cast<float>(std::clamp(v.blockEnd / phraseUnits, 0.0, 1.0));
        canvas.setColor(colour::withAlpha(accent, 0.16f));
        canvas.roundedRectangle(tapeX + b0 * tapeW, tapeY + 1.0f * px, std::max(2.0f * px, (b1 - b0) * tapeW),
                                tapeH - 2.0f * px, 4.0f * px);
        canvas.setColor(colour::withAlpha(accent, 0.45f));
        canvas.rectangle(tapeX + b0 * tapeW, tapeY + 1.0f * px, std::max(2.0f * px, (b1 - b0) * tapeW), 2.0f * px);
    }

    // Subtle bar & beat gridlines
    const int beatsTotal = std::max(1, static_cast<int>(std::lround(bars * v.beatsPerBar)));
    for (int b = 1; b < beatsTotal; ++b) {
        const float gx = tapeX + (static_cast<float>(b) / static_cast<float>(beatsTotal)) * tapeW;
        const bool isBar =
            (v.beatsPerBar >= 1.0) && (std::abs(std::fmod(static_cast<double>(b), v.beatsPerBar)) < 1.0e-4);
        canvas.setColor(isBar ? colour::withAlpha(colour::text, 0.18f) : colour::withAlpha(colour::text, 0.07f));
        canvas.rectangle(gx, tapeY + 2.0f * px, 1.0f * px, tapeH - 4.0f * px);
    }

    // Center zero line
    const float midY = tapeY + tapeH * 0.5f;
    canvas.setColor(colour::withAlpha(colour::edge, 0.9f));
    canvas.rectangle(tapeX + 2.0f * px, midY - 0.5f * px, tapeW - 4.0f * px, 1.0f * px);

    // Live mirrored waveform bars + active slice markers
    constexpr std::size_t N = VisualState::kWaveformBins;
    const float binW = tapeW / static_cast<float>(N);
    const float maxAmp = tapeH * 0.44f;
    for (std::size_t i = 0; i < N; ++i) {
        const float bx = tapeX + static_cast<float>(i) * binW;
        const float level = std::clamp(v.waveform[i], 0.0f, 1.0f);
        if (level > 0.008f) {
            const float bh = std::max(2.0f * px, level * maxAmp * 2.0f);
            canvas.setColor(colour::withAlpha(accent, 0.52f + 0.35f * level));
            canvas.roundedRectangle(bx + 0.6f * px, midY - bh * 0.5f, std::max(1.2f * px, binW - 1.2f * px), bh,
                                    1.0f * px);
        }
        if (v.sliceMarks[i] != 0) {
            canvas.setColor(colour::withAlpha(colour::text, 0.72f));
            canvas.rectangle(bx, tapeY + 2.0f * px, 1.2f * px, tapeH - 4.0f * px);
        }
    }

    // Playhead needle
    if (v.cutting) {
        const float p = static_cast<float>(std::clamp(v.positionInPhrase / phraseUnits, 0.0, 1.0));
        const float pxPos = tapeX + p * tapeW;
        canvas.setColor(colour::withAlpha(accent, 0.35f));
        canvas.rectangle(pxPos - 2.0f * px, tapeY + 1.0f * px, 5.0f * px, tapeH - 2.0f * px);
        canvas.setColor(colour::text);
        canvas.rectangle(pxPos - 0.75f * px, tapeY + 1.0f * px, 1.5f * px, tapeH - 2.0f * px);
    }

    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(tapeX, tapeY, tapeW, tapeH, 6.0f * px, 1.0f * px);

    // Right status & transport pill
    const float pillX = tapeX + tapeW + padX;
    canvas.setColor(colour::raised);
    canvas.roundedRectangle(pillX, tapeY, pillW, tapeH, 6.0f * px);
    canvas.setColor(v.cutting ? colour::withAlpha(accent, 0.55f) : colour::edge);
    canvas.roundedRectangleBorder(pillX, tapeY, pillW, tapeH, 6.0f * px, 1.0f * px);

    const float dotD = 7.0f * px;
    canvas.setColor(v.cutting ? accent : colour::textFaint);
    canvas.circle(pillX + 10.0f * px, tapeY + (tapeH - dotD) * 0.5f, dotD);

    const std::string line = v.headline.empty() ? "Ready" : v.headline;
    drawLabel(canvas, context_, line, pillX + 23.0f * px, tapeY, pillW - 88.0f * px, tapeH, colour::text,
              visage::Font::kLeft, type::small, Weight::semibold);

    char bpmBuf[24];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM", v.bpm > 1.0 ? v.bpm : 120.0);
    drawLabel(canvas, context_, bpmBuf, pillX + pillW - 64.0f * px, tapeY, 54.0f * px, tapeH, colour::textDim,
              visage::Font::kRight, type::small, Weight::medium);
}

} // namespace lc::ui
