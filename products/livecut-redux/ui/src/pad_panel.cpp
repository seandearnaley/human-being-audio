// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad_panel.h"

#include "engine/PadPrograms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace lc::ui {

namespace {

constexpr std::array<const char*, 8> kProgramNames{"Cut",    "Bounce", "Glitch", "Suction",
                                                   "Filter", "Tape",   "Loops",  "Rush"};

constexpr std::array<const char*, 8> kMidiBadges{"C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1"};

constexpr float kTabHeight = 38.0f;
constexpr float kTabGap = 4.0f;
constexpr float kTabInset = 5.0f;
constexpr float kDockHeight = 46.0f;
constexpr float kDockGap = 10.0f;
constexpr float kRail = 18.0f;

float easeOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u;
}

float frac(float v) { return v - std::floor(v); }

} // namespace

PadPanel::PadPanel(Context& context)
    : context_(context), hold_(context, "padHold", "Hold", colour::accent),
      straight_(context, "padStraight", "Straight", colour::accent),
      quantize_(context, "padQuantize", {"Beat", "Bar"}, colour::accent),
      rollSeed_(context, "Roll Seed", [this] { context_.model.rollSeed(); }),
      dragWav_(context, "Drag .wav", [this] { context_.model.requestCapture(); }),
      trim_(context, "outputTrim", "Trim", colour::textDim, true), mix_(context, "wet", "Mix", colour::accent, true) {
    setAcceptsKeystrokes(true);
    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&hold_, &straight_, &quantize_, &rollSeed_, &dragWav_, &trim_, &mix_})
        addChild(child);

    dragWav_.setAccent(colour::cyan);
    dragWav_.setOnDrag([this] { context_.model.beginDragCaptureWav(); });
    rollSeed_.setAccent(colour::textDim);

    program_ = playingProgram();
    programBefore_ = program_;
    motion_.x = context_.model.getNormalised("padX");
    motion_.y = context_.model.getNormalised("padY");

    context_.live.push_back(this);
}

PadPanel::~PadPanel() {
    endPadTouch();
    std::erase(context_.live, static_cast<Live*>(this));
}

int PadPanel::playingProgram() const {
    const auto& v = context_.model.visuals();
    if (v.padEngaged || v.padPendingProgram >= 0)
        return std::clamp(v.padActiveProgram, 0, 7);
    const int chosen = static_cast<int>(std::lround(context_.model.getNormalised("padProgram") * 7.0f));
    return std::clamp(chosen, 0, 7);
}

PadPanel::Rect PadPanel::tabRect(int index) const {
    const float gap = context_.px(kTabGap);
    const float th = context_.px(kTabHeight);
    const float tw = (width() - 7.0f * gap) / 8.0f;
    return {static_cast<float>(index) * (tw + gap), 0.0f, tw, th};
}

PadPanel::Rect PadPanel::sheetRect() const {
    const float top = context_.px(kTabHeight);
    const float bottom = height() - context_.px(kDockHeight + kDockGap);
    return {0.0f, top, width(), std::max(10.0f, bottom - top)};
}

PadPanel::Rect PadPanel::artRect() const {
    const Rect s = sheetRect();
    const float r = context_.px(kRail);
    return {s.x + r, s.y + r, std::max(10.0f, s.w - 2.0f * r), std::max(10.0f, s.h - 2.0f * r)};
}

int PadPanel::tabAt(float x, float y) const {
    if (y < 0.0f || y > context_.px(kTabHeight))
        return -1;
    for (int i = 0; i < 8; ++i)
        if (tabRect(i).contains(x, y))
            return i;
    return -1;
}

void PadPanel::tick() {
    if (!isVisible()) {
        endPadTouch();
        return;
    }

    const auto& v = context_.model.visuals();
    const int nextProg = playingProgram();
    if (nextProg != program_) {
        programBefore_ = change_ >= 0.5f ? program_ : programBefore_;
        program_ = nextProg;
        change_ = 0.0f;
    }

    const unsigned col = colour::programArgb(program_);
    hold_.setAccent(col);
    straight_.setAccent(col);
    quantize_.setAccent(col);
    mix_.setAccent(col);
    dragWav_.setLabel(v.hasCapture ? "Drag .wav \u2197" : "Capture .wav");
    dragWav_.setActive(v.hasCapture);

    char seedLabel[24];
    std::snprintf(seedLabel, sizeof(seedLabel), "Seed #%d",
                  static_cast<int>(std::lround(context_.model.getReal("seed"))));
    rollSeed_.setLabel(seedLabel);

    // 60 Hz smooth animation (~16.6 ms step)
    constexpr float dt = 1.0f / 60.0f;
    change_ = std::min(1.0f, change_ + dt / 0.18f);
    ring_ = std::min(1.0f, ring_ + dt / 0.32f);
    motion_.kick = std::max(0.0f, motion_.kick - dt / 0.60f);

    const float targetX = context_.model.getNormalised("padX");
    const float targetY = context_.model.getNormalised("padY");
    motion_.x += (targetX - motion_.x) * std::min(1.0f, dt * 14.0f);
    motion_.y += (targetY - motion_.y) * std::min(1.0f, dt * 14.0f);

    float beatsStep = dt * 0.5f;
    if (v.cutting && v.bpm > 1.0) {
        const float tempo = static_cast<float>(v.bpm / 60.0);
        if (std::abs(v.beat - lastBeat_) > 1.0e-6) {
            lastBeat_ = v.beat;
            float out = frac(static_cast<float>(v.beat)) - frac(motion_.beat + dt * tempo);
            out -= std::round(out);
            beatsStep = std::max(0.0f, dt * tempo + out * std::min(1.0f, dt * 10.0f));
        } else {
            beatsStep = dt * tempo;
        }
    }
    motion_.beat = std::fmod(motion_.beat + beatsStep, ArtMotion::kBeatsBeforeWrap);
    motion_.spin = std::fmod(motion_.spin + beatsStep * (1.0f - motion_.kick) * (1.0f - motion_.kick),
                             ArtMotion::kBeatsBeforeWrap);

    if (v.cutting && v.padEngaged && lastBlockStart_.has_value() &&
        std::abs(v.blockStart - *lastBlockStart_) > 1.0e-6) {
        motion_.kick = 1.0f;
        ++motion_.cuts;
        ring_ = 0.0f;
    }
    lastBlockStart_ = v.blockStart;

    redraw();
}

void PadPanel::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const auto& v = context_.model.visuals();
    const unsigned sheetCol =
        colour::lerpArgb(colour::programArgb(programBefore_), colour::programArgb(program_), easeOut(change_));

    // 1. Folder Tabs Row (Cut [C1] .. Rush [G1])
    for (int i = 0; i < 8; ++i) {
        const Rect tr = tabRect(i);
        const unsigned pCol = colour::programArgb(i);
        const bool isPlaying = (i == program_);
        const bool isPending = (i == v.padPendingProgram);
        const bool isMidiHeld = ((v.midiPadMask >> i) & 1u) != 0;
        const bool isHovered = (i == hoveredTab_);

        const float inset = isPlaying ? 0.0f : kTabInset * px;
        const float fy = tr.y + inset;
        const float fh = tr.h - inset;

        unsigned faceCol = colour::raised;
        unsigned nameCol = pCol;
        if (isPlaying) {
            faceCol = pCol;
            nameCol = colour::onColour;
        } else if (isPending) {
            const float lit = std::pow(1.0f - frac(motion_.beat), 2.0f);
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.22f + 0.34f * lit);
            nameCol = colour::text;
        } else if (isMidiHeld) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.42f);
            nameCol = colour::text;
        } else if (isHovered) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.16f);
            nameCol = colour::text;
        }

        canvas.setColor(faceCol);
        canvas.topRoundedRectangle(tr.x, fy, tr.w, fh + 2.0f * px, 9.0f * px);
        if (!isPlaying) {
            canvas.setColor(colour::withAlpha(pCol, isHovered || isPending ? 0.55f : 0.24f));
            canvas.roundedRectangleBorder(tr.x, fy, tr.w, fh, 9.0f * px, 1.0f * px);
        }

        // Program name centered on shared resting baseline
        const float textY = tr.y + kTabInset * px;
        const float textH = tr.h - kTabInset * px;
        drawLabel(canvas, context_, kProgramNames[static_cast<std::size_t>(i)], tr.x + 6.0f * px, textY,
                  tr.w - 30.0f * px, textH, nameCol, visage::Font::kLeft, 12.5f,
                  isPlaying ? Weight::semibold : Weight::medium);

        // Crisp MIDI note badge (C1..G1) on right of tab
        const unsigned badgeCol = isPlaying    ? colour::withAlpha(colour::onColour, 0.65f)
                                  : isMidiHeld ? colour::text
                                               : colour::withAlpha(colour::textDim, 0.72f);
        drawLabel(canvas, context_, kMidiBadges[static_cast<std::size_t>(i)], tr.x + tr.w - 30.0f * px, textY,
                  24.0f * px, textH, badgeCol, visage::Font::kRight, 9.5f, Weight::semibold);
    }

    // 2. Hero Coloured Sheet (square top joining tabs, rounded bottom)
    const Rect sheet = sheetRect();
    canvas.setColor(sheetCol);
    canvas.bottomRoundedRectangle(sheet.x, sheet.y, sheet.w, sheet.h, 12.0f * px);

    // 3. Inner Procedural GPU Artwork with smooth program crossfade
    const Rect art = artRect();
    if (change_ < 0.995f)
        drawProgramArt(canvas, programBefore_, art.x, art.y, art.w, art.h, motion_, px, 1.0f);
    drawProgramArt(canvas, program_, art.x, art.y, art.w, art.h, motion_, px, easeOut(change_));

    // Crisp frame around art well
    canvas.setColor(colour::withAlpha(0xff000000u, 0.45f));
    canvas.roundedRectangleBorder(art.x, art.y, art.w, art.h, 8.0f * px, 2.0f * px);

    // 4. Crosshair, Ripple Ring, Pointer Dot & Axis Badges
    const float padX = std::clamp(context_.model.getNormalised("padX"), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.getNormalised("padY"), 0.0f, 1.0f);
    const float mx = art.x + padX * art.w;
    const float my = art.y + (1.0f - padY) * art.h;

    canvas.saveState();
    canvas.trimClampBounds(art.x, art.y, art.w, art.h);

    canvas.setColor(colour::withAlpha(0xffffffffu, 0.50f));
    canvas.rectangle(art.x, my - 0.5f * px, art.w, 1.0f * px);
    canvas.rectangle(mx - 0.5f * px, art.y, 1.0f * px, art.h);

    if (ring_ < 0.995f) {
        const float r = (10.0f + easeOut(ring_) * 30.0f) * px;
        canvas.setColor(colour::withAlpha(0xffffffffu, 1.0f - ring_));
        canvas.ring(mx - r, my - r, 2.0f * r, 2.0f * px);
    }

    if (draggingPad_) {
        const float pr = 22.0f * px;
        canvas.setColor(colour::withAlpha(0xff000000u, 0.35f));
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 4.0f * px);
        canvas.setColor(0xffffffffu);
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 2.0f * px);
    }

    const float swell = v.cutting ? std::pow(1.0f - frac(motion_.beat), 3.0f) : 0.0f;
    const float dotR = 10.0f * (1.0f + 0.18f * swell) * px;
    canvas.setColor(colour::withAlpha(0xff000000u, 0.45f));
    canvas.circle(mx - dotR, my - dotR + 2.5f * px, 2.0f * dotR);
    canvas.setColor(v.padEngaged ? colour::onColour : 0xffffffffu);
    canvas.circle(mx - dotR, my - dotR, 2.0f * dotR);
    canvas.setColor(v.padEngaged ? 0xffffffffu : colour::onColour);
    canvas.ring(mx - dotR, my - dotR, 2.0f * dotR, 2.4f * px);

    // Folded axis tags (Y top-left, X bottom-right)
    const auto captions = livecut::engine::padCaptions(static_cast<livecut::engine::PadProgram>(program_));
    char xVal[16], yVal[16];
    std::snprintf(xVal, sizeof(xVal), "%d%%", static_cast<int>(std::lround(padX * 100.0f)));
    std::snprintf(yVal, sizeof(yVal), "%d%%", static_cast<int>(std::lround(padY * 100.0f)));

    const float tagH = 24.0f * px;
    const float yTagW = 136.0f * px;
    const float xTagW = 136.0f * px;

    // Top-left Y tag
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(art.x - 4.0f * px, art.y - 4.0f * px, yTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
    drawLabel(canvas, context_, captions.y, art.x + 8.0f * px, art.y, yTagW - 44.0f * px, tagH, colour::onColour,
              visage::Font::kLeft, 11.5f, Weight::semibold);
    drawLabel(canvas, context_, yVal, art.x + yTagW - 42.0f * px, art.y, 34.0f * px, tagH, colour::onColour,
              visage::Font::kRight, 11.5f, Weight::semibold);

    // Bottom-right X tag
    const float xTagX = art.x + art.w - xTagW;
    const float xTagY = art.y + art.h - tagH;
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(xTagX, xTagY, xTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
    drawLabel(canvas, context_, captions.x, xTagX + 8.0f * px, xTagY, xTagW - 44.0f * px, tagH, colour::onColour,
              visage::Font::kLeft, 11.5f, Weight::semibold);
    drawLabel(canvas, context_, xVal, xTagX + xTagW - 42.0f * px, xTagY, 34.0f * px, tagH, colour::onColour,
              visage::Font::kRight, 11.5f, Weight::semibold);

    canvas.restoreState();

    // 5. Four Inward Rail Marker Triangles
    const float mInset = 4.0f * px;
    const float mLen = 7.0f * px;
    const float mReach = 5.5f * px;
    canvas.setColor(colour::onColour);
    // Bottom pointing up
    canvas.triangle(mx, art.y + art.h + mInset, mx - mReach, art.y + art.h + mInset + mLen, mx + mReach,
                    art.y + art.h + mInset + mLen);
    // Top pointing down
    canvas.triangle(mx, art.y - mInset, mx - mReach, art.y - mInset - mLen, mx + mReach, art.y - mInset - mLen);
    // Right pointing left
    canvas.triangle(art.x + art.w + mInset, my, art.x + art.w + mInset + mLen, my - mReach,
                    art.x + art.w + mInset + mLen, my + mReach);
    // Left pointing right
    canvas.triangle(art.x - mInset, my, art.x - mInset - mLen, my - mReach, art.x - mInset - mLen, my + mReach);

    // 6. Bottom Performance Dock Card
    const float dockY = height() - context_.px(kDockHeight);
    drawPanel(canvas, context_, 0.0f, dockY, width(), context_.px(kDockHeight), colour::panel, metric::radius);

    // Capture status text beside Drag .wav
    if (!v.captureStatus.empty()) {
        drawLabel(canvas, context_, v.captureStatus, context_.px(566.0f), dockY, context_.px(210.0f),
                  context_.px(kDockHeight), colour::textDim, visage::Font::kLeft, type::small, Weight::medium);
    }
}

void PadPanel::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float dockY = height() - px(kDockHeight);
    const float bh = px(28.0f);
    const float cy = dockY + (px(kDockHeight) - bh) * 0.5f;

    float x = px(12.0f);
    hold_.setBounds(x, cy, px(76.0f), bh);
    x += px(84.0f);
    straight_.setBounds(x, cy, px(94.0f), bh);
    x += px(102.0f);
    quantize_.setBounds(x, cy, px(112.0f), bh);
    x += px(124.0f);
    rollSeed_.setBounds(x, cy, px(94.0f), bh);
    x += px(102.0f);
    dragWav_.setBounds(x, cy, px(124.0f), bh);

    // Right: Trim + Mix compact bar sliders
    const float mixW = px(176.0f);
    const float trimW = px(152.0f);
    mix_.setBounds(width() - px(12.0f) - mixW, cy, mixW, bh);
    trim_.setBounds(width() - px(12.0f) - mixW - px(10.0f) - trimW, cy, trimW, bh);
}

void PadPanel::updatePointer(float x, float y, bool shiftDown) {
    const Rect art = artRect();
    if (art.w <= 1.0f || art.h <= 1.0f)
        return;
    if (shiftDown && !fineDragging_) {
        dragOriginX_ = x;
        dragOriginY_ = y;
        dragStartX_ = context_.model.getNormalised("padX");
        dragStartY_ = context_.model.getNormalised("padY");
    }
    fineDragging_ = shiftDown;

    float nx = (x - art.x) / art.w;
    float ny = (art.y + art.h - y) / art.h;
    if (fineDragging_) {
        nx = dragStartX_ + (x - dragOriginX_) / art.w * 0.1f;
        ny = dragStartY_ + (dragOriginY_ - y) / art.h * 0.1f;
    }
    context_.model.setNormalised("padX", std::clamp(nx, 0.0f, 1.0f));
    context_.model.setNormalised("padY", std::clamp(ny, 0.0f, 1.0f));
    redraw();
}

void PadPanel::endPadTouch() {
    if (touching_) {
        touching_ = false;
        context_.model.setNormalised("padTouch", 0.0f);
        context_.model.endGesture("padTouch");
    }
    if (draggingPad_) {
        draggingPad_ = false;
        context_.model.endGesture("padX");
        context_.model.endGesture("padY");
        redraw();
    }
}

void PadPanel::mouseMove(const visage::MouseEvent& e) {
    const int idx = tabAt(e.position.x, e.position.y);
    if (idx != hoveredTab_) {
        hoveredTab_ = idx;
        redraw();
    }
}

void PadPanel::mouseExit(const visage::MouseEvent&) {
    if (hoveredTab_ != -1) {
        hoveredTab_ = -1;
        redraw();
    }
}

void PadPanel::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;

    const int tab = tabAt(e.position.x, e.position.y);
    if (tab >= 0) {
        const float norm = static_cast<float>(tab) / 7.0f;
        context_.model.beginGesture("padProgram");
        context_.model.setNormalised("padProgram", norm);
        context_.model.endGesture("padProgram");
        redraw();
        return;
    }

    if (artRect().contains(e.position.x, e.position.y)) {
        endPadTouch();
        draggingPad_ = true;
        fineDragging_ = e.isShiftDown();
        dragOriginX_ = e.position.x;
        dragOriginY_ = e.position.y;
        dragStartX_ = context_.model.getNormalised("padX");
        dragStartY_ = context_.model.getNormalised("padY");

        context_.model.beginGesture("padX");
        context_.model.beginGesture("padY");
        updatePointer(e.position.x, e.position.y, e.isShiftDown());

        touching_ = true;
        context_.model.beginGesture("padTouch");
        context_.model.setNormalised("padTouch", 1.0f);
        redraw();
    }
}

void PadPanel::mouseDrag(const visage::MouseEvent& e) {
    if (draggingPad_)
        updatePointer(e.position.x, e.position.y, e.isShiftDown());
}

void PadPanel::mouseUp(const visage::MouseEvent& e) {
    if (!draggingPad_)
        return;
    updatePointer(e.position.x, e.position.y, fineDragging_);
    endPadTouch();
}

bool PadPanel::keyPress(const visage::KeyEvent& key) {
    const float step = key.isShiftDown() ? 0.005f : key.isMainModifier() ? 0.10f : 0.02f;
    const auto nudge = [&](std::string_view id, float delta) {
        const float next = std::clamp(context_.model.getNormalised(id) + delta, 0.0f, 1.0f);
        context_.model.beginGesture(id);
        context_.model.setNormalised(id, next);
        context_.model.endGesture(id);
        redraw();
    };
    if (key.keyCode() == visage::KeyCode::Left) {
        nudge("padX", -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Right) {
        nudge("padX", step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Down) {
        nudge("padY", -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Up) {
        nudge("padY", step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Home) {
        context_.model.beginGesture("padX");
        context_.model.beginGesture("padY");
        context_.model.setNormalised("padX", 0.5f);
        context_.model.setNormalised("padY", 0.5f);
        context_.model.endGesture("padX");
        context_.model.endGesture("padY");
        redraw();
        return true;
    }
    return false;
}

} // namespace lc::ui
