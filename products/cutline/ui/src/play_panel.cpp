// SPDX-License-Identifier: AGPL-3.0-or-later
#include "play_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace cl::ui {

namespace {

constexpr float kTabHeight = 40.0f;
constexpr float kTabGap = 4.0f;
constexpr float kTabInset = 5.0f;
constexpr float kDockHeight = 46.0f;
constexpr float kDockGap = 10.0f;
constexpr float kRail = 16.0f;

float easeOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u;
}

float frac(float v) { return v - std::floor(v); }

} // namespace

PlayPanel::PlayPanel(Context& context)
    : context_(context), holdBtn_(context, "Hold", [this] { toggleHold(); }),
      quantChip_(context, ParamId::sceneQuantize, colour::text), ccChip_(context, ParamId::padCcMode, colour::cyan),
      noteMapChip_(context, ParamId::noteMap, colour::text), slicerChip_(context, ParamId::slicerStyle, colour::cyan),
      auditionBtn_(context, "Audition",
                   [this] {
                       const bool on = context_.model.get(ParamId::audition) >= 0.5f;
                       context_.model.begin(ParamId::audition);
                       context_.model.set(ParamId::audition, on ? 0.0f : 1.0f);
                       context_.model.end(ParamId::audition);
                   }),
      dragMidBtn_(context, context.model.exportStatus() + " \u2197", [this] { context_.model.dragMidiPhrase(); }) {
    setAcceptsKeystrokes(true);
    for (visage::Frame* child : std::initializer_list<visage::Frame*>{&holdBtn_, &quantChip_, &ccChip_, &noteMapChip_,
                                                                      &slicerChip_, &auditionBtn_, &dragMidBtn_})
        addChild(child);

    dragMidBtn_.setOnDrag([this] { context_.model.dragMidiPhrase(); });

    program_ = playingProgram();
    programBefore_ = program_;
    motion_.x = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    motion_.y = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    motion_.engage = context_.model.visuals().padEngaged ? 1.0f : 0.0f;

    context_.live.push_back(this);
    tick();
}

PlayPanel::~PlayPanel() {
    endPadTouch();
    std::erase(context_.live, static_cast<Live*>(this));
}

int PlayPanel::playingProgram() const {
    const auto& v = context_.model.visuals();
    if (v.padEngaged || v.pendingPadProgram >= 0)
        return std::clamp(v.activePadProgram, 0, kPerfPadCount - 1);
    return std::clamp(static_cast<int>(context_.model.get(ParamId::padProgram) + 0.5f), 0, kPerfPadCount - 1);
}

PlayPanel::Rect PlayPanel::tabRect(int index) const {
    const float gap = context_.px(kTabGap);
    const float th = context_.px(kTabHeight);
    const float tw = (width() - static_cast<float>(kPerfPadCount - 1) * gap) / static_cast<float>(kPerfPadCount);
    return {static_cast<float>(index) * (tw + gap), 0.0f, tw, th};
}

PlayPanel::Rect PlayPanel::sheetRect() const {
    const float top = context_.px(kTabHeight);
    const float bottom = height() - context_.px(kDockHeight + kDockGap);
    return {0.0f, top, width(), std::max(10.0f, bottom - top)};
}

PlayPanel::Rect PlayPanel::artRect() const {
    const Rect s = sheetRect();
    const float r = context_.px(kRail);
    return {s.x + r, s.y + r, std::max(10.0f, s.w - 2.0f * r), std::max(10.0f, s.h - 2.0f * r)};
}

int PlayPanel::tabAt(float x, float y) const {
    if (y < 0.0f || y > context_.px(kTabHeight))
        return -1;
    for (int i = 0; i < kPerfPadCount; ++i)
        if (tabRect(i).contains(x, y))
            return i;
    return -1;
}

std::string PlayPanel::formatAxisValue(int prog, bool isX, float v) const {
    char buf[32]{};
    if (isX) {
        switch (prog) {
        case 0: // Repeat: window
            return v < 0.25f ? "1 BEAT" : (v < 0.5f ? "1/2" : (v < 0.75f ? "1/4" : "1/8"));
        case 1: // Roll: rate
            return v < 0.25f ? "1/8" : (v < 0.5f ? "1/16" : (v < 0.75f ? "1/16T" : "1/32"));
        case 2: // Rate: speed
            return v < 1.0f / 3.0f ? "0.5\u00d7" : (v > 2.0f / 3.0f ? "2\u00d7" : "1\u00d7");
        default: // Drop
            return v < 0.5f ? "CUT" : "FILL";
        }
    }
    if (prog == 2)
        return v < 0.30f ? "KICK + SNARE" : (v > 0.70f ? "NO KICK" : "FULL KIT");
    if (prog == 3)
        return "";
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(v * 100.0f)));
    return buf;
}

std::string PlayPanel::liveStatusBadge(int prog, float x, float y, bool engaged, bool holdOn) const {
    const int ccIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::padCcMode) + 0.5f), 0, 3);
    const std::string ccStr =
        ccIdx > 0 ? (" \u00b7 " + std::string(choices::padCcMode[static_cast<std::size_t>(ccIdx)])) : "";
    const std::string modeTag = holdOn ? "HOLD" : (engaged ? "LIVE" : "TOUCH / DRAG");
    const std::string keyVal = formatAxisValue(prog, true, x);
    if (engaged || holdOn)
        return modeTag + " \u00b7 " + std::string(kPerfPadLabels[static_cast<std::size_t>(prog)]) + " " + keyVal +
               ccStr;
    (void)y;
    return modeTag + " \u00b7 MIDI " + std::string(kPerfPadHints[static_cast<std::size_t>(prog)]) + ccStr;
}

void PlayPanel::tick() {
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
        ring_ = 0.0f;
    }

    const unsigned col = colour::programArgb(program_);
    const bool holdOn = context_.model.get(ParamId::padHold) >= 0.5f;
    const bool isDrum = static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f) == 0;
    const bool audOn = context_.model.get(ParamId::audition) >= 0.5f;

    holdBtn_.setLabel(holdOn ? "Hold ON" : "Hold");
    holdBtn_.setAccent(holdOn ? col : colour::textDim);
    quantChip_.setAccent(col);
    ccChip_.setAccent(colour::cyan);
    noteMapChip_.setVisible(isDrum);
    slicerChip_.setVisible(!isDrum);
    auditionBtn_.setLabel(audOn ? "Aud ON" : "Aud OFF");
    auditionBtn_.setAccent(audOn ? colour::emerald : colour::textFaint);
    dragMidBtn_.setAccent(col);

    const std::string exp = context_.model.exportStatus() + " \u2197";
    if (exp != shownExport_) {
        shownExport_ = exp;
        dragMidBtn_.setLabel(shownExport_);
    }

    // 60 Hz smooth animation (~16.6 ms step)
    constexpr float dt = 1.0f / 60.0f;
    change_ = std::min(1.0f, change_ + dt / 0.16f);
    ring_ = std::min(1.0f, ring_ + dt / 0.34f);

    const float targetX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float targetY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    motion_.x += (targetX - motion_.x) * std::min(1.0f, dt * 16.0f);
    motion_.y += (targetY - motion_.y) * std::min(1.0f, dt * 16.0f);

    const float targetEng = (v.padEngaged || draggingPad_ || holdOn) ? 1.0f : 0.0f;
    motion_.engage += (targetEng - motion_.engage) * std::min(1.0f, dt * 14.0f);

    // Sync beat phase & lane flashes for live reactive 60fps geometry!
    float peakFlash = 0.0f;
    for (int l = 0; l < kDrumLaneCount; ++l) {
        const auto li = static_cast<std::size_t>(l);
        motion_.laneFlash[li] = v.laneFlash[li];
        peakFlash = std::max(peakFlash, v.laneFlash[li]);
    }
    motion_.impulse = std::max(peakFlash, motion_.impulse * 0.86f);
    motion_.isSlicer = !isDrum;
    motion_.heldCount = v.heldNotes.count;

    if (v.playing && v.bpm > 1.0) {
        if (std::abs(v.ppq - lastPlayhead_) > 1.0e-5) {
            const int prevBeat = static_cast<int>(std::floor(lastPlayhead_));
            const int curBeat = static_cast<int>(std::floor(v.ppq));
            if (curBeat != prevBeat && v.padEngaged)
                ring_ = 0.0f;
            lastPlayhead_ = v.ppq;
            motion_.beat = static_cast<float>(std::fmod(v.ppq, 64.0));
        } else {
            motion_.beat = std::fmod(motion_.beat + dt * static_cast<float>(v.bpm / 60.0), 64.0f);
        }
    } else {
        // Gentle idle kinetic breathing so the instrument still looks alive when paused!
        motion_.beat = std::fmod(motion_.beat + dt * 0.45f, 64.0f);
    }

    redraw();
}

void PlayPanel::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const auto& v = context_.model.visuals();
    const bool holdOn = context_.model.get(ParamId::padHold) >= 0.5f;
    const bool engaged = v.padEngaged || draggingPad_ || holdOn;
    const unsigned sheetCol =
        colour::lerpArgb(colour::programArgb(programBefore_), colour::programArgb(program_), easeOut(change_));

    // 1. Folder Tabs Row: Repeat [C3] .. Drop [D#3]
    for (int i = 0; i < kPerfPadCount; ++i) {
        const Rect tr = tabRect(i);
        const unsigned pCol = colour::programArgb(i);
        const bool isPlaying = (i == program_);
        const bool isPending = (i == v.pendingPadProgram);
        const bool isMidiHeld = ((v.midiPadMask >> static_cast<unsigned>(i)) & 1u) != 0u;
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
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.24f + 0.36f * lit);
            nameCol = colour::text;
        } else if (isMidiHeld) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.44f);
            nameCol = colour::text;
        } else if (isHovered) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.18f);
            nameCol = colour::text;
        }

        canvas.setColor(faceCol);
        canvas.topRoundedRectangle(tr.x, fy, tr.w, fh + 2.0f * px, 9.0f * px);
        if (!isPlaying) {
            canvas.setColor(colour::withAlpha(pCol, isHovered || isPending ? 0.62f : 0.26f));
            canvas.roundedRectangleBorder(tr.x, fy, tr.w, fh, 9.0f * px, 1.0f * px);
        }

        const float textY = tr.y + kTabInset * px;
        const float textH = tr.h - kTabInset * px;
        drawLabel(canvas, context_, std::string(kPerfPadLabels[static_cast<std::size_t>(i)]), tr.x + 10.0f * px, textY,
                  tr.w - 38.0f * px, textH, nameCol, visage::Font::kLeft, 13.0f,
                  isPlaying ? Weight::semibold : Weight::medium);

        const unsigned badgeCol = isPlaying    ? colour::withAlpha(colour::onColour, 0.68f)
                                  : isMidiHeld ? colour::text
                                               : colour::withAlpha(colour::textDim, 0.75f);
        drawLabel(canvas, context_, std::string(kPerfPadHints[static_cast<std::size_t>(i)]), tr.x + tr.w - 34.0f * px,
                  textY, 26.0f * px, textH, badgeCol, visage::Font::kRight, 9.5f, Weight::semibold);
    }

    // 2. Hero Coloured Sheet (square top fused to active folder tab, rounded bottom)
    const Rect sheet = sheetRect();
    canvas.setColor(sheetCol);
    canvas.bottomRoundedRectangle(sheet.x, sheet.y, sheet.w, sheet.h, 12.0f * px);

    // 3. Giant Interactive 60fps Generative Art Well with Smooth Crossfade
    const Rect art = artRect();
    canvas.saveState();
    canvas.trimClampBounds(art.x, art.y, art.w, art.h);

    if (change_ < 0.995f)
        drawCutlinePadArt(canvas, programBefore_, art.x, art.y, art.w, art.h, 1.0f, motion_, px);
    drawCutlinePadArt(canvas, program_, art.x, art.y, art.w, art.h, easeOut(change_), motion_, px);

    // Subtle precision coordinate grid lines (1/4 subdivisions)
    for (int q = 1; q <= 3; ++q) {
        const float f = static_cast<float>(q) * 0.25f;
        canvas.setColor(0x14ffffffu);
        canvas.rectangle(art.x + f * art.w, art.y, 1.0f * px, art.h);
        canvas.rectangle(art.x, art.y + f * art.h, art.w, 1.0f * px);
    }

    // 4. Crosshair, Hit Ripple Ring & Glowing Expressive Cursor Puck
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    const float mx = art.x + padX * art.w;
    const float my = art.y + (1.0f - padY) * art.h;

    canvas.setColor(colour::withAlpha(0xffffffffu, engaged ? 0.56f : 0.34f));
    canvas.rectangle(art.x, my - 0.5f * px, art.w, 1.0f * px);
    canvas.rectangle(mx - 0.5f * px, art.y, 1.0f * px, art.h);

    if (ring_ < 0.995f) {
        const float r = (12.0f + easeOut(ring_) * 38.0f) * px;
        canvas.setColor(colour::withAlpha(0xffffffffu, (1.0f - ring_) * 0.85f));
        canvas.ring(mx - r, my - r, 2.0f * r, 2.2f * px);
    }

    if (engaged) {
        const float auraR = (26.0f + 8.0f * motion_.impulse) * px;
        canvas.setColor(colour::withAlpha(sheetCol, 0.24f));
        canvas.circle(mx - auraR, my - auraR, 2.0f * auraR);
        const float pr = 22.0f * px;
        canvas.setColor(colour::withAlpha(0xff000000u, 0.36f));
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 4.0f * px);
        canvas.setColor(0xffffffffu);
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 2.0f * px);
    }

    const float swell = v.playing ? (0.18f * motion_.impulse) : 0.0f;
    const float dotR = 10.5f * (1.0f + swell) * px;
    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.circle(mx - dotR, my - dotR + 2.5f * px, 2.0f * dotR);
    canvas.setColor(engaged ? colour::onColour : 0xffffffffu);
    canvas.circle(mx - dotR, my - dotR, 2.0f * dotR);
    canvas.setColor(engaged ? 0xffffffffu : colour::onColour);
    canvas.ring(mx - dotR, my - dotR, 2.0f * dotR, 2.6f * px);

    // 5. Folded Corner Axis Tags (Y Top-Left, X Bottom-Right) + Top-Right Live Status HUD Pill
    const float tagH = 26.0f * px;
    const float yTagW = 192.0f * px;
    const float xTagW = 200.0f * px;

    // Top-left Y Tag (Drop has no Y axis, so no tag)
    const std::string yLabel(kPerfPadYLabels[static_cast<std::size_t>(program_)]);
    if (!yLabel.empty()) {
        canvas.setColor(sheetCol);
        canvas.roundedRectangle(art.x - 4.0f * px, art.y - 4.0f * px, yTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
        drawLabel(canvas, context_, yLabel, art.x + 10.0f * px, art.y, yTagW - 88.0f * px, tagH, colour::onColour,
                  visage::Font::kLeft, 11.5f, Weight::semibold);
        drawLabel(canvas, context_, formatAxisValue(program_, false, padY), art.x + yTagW - 84.0f * px, art.y,
                  74.0f * px, tagH, colour::onColour, visage::Font::kRight, 11.0f, Weight::semibold);
    }

    // Bottom-right X Tag
    const float xTagX = art.x + art.w - xTagW;
    const float xTagY = art.y + art.h - tagH;
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(xTagX, xTagY, xTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
    drawLabel(canvas, context_, std::string(kPerfPadXLabels[static_cast<std::size_t>(program_)]), xTagX + 10.0f * px,
              xTagY, xTagW - 94.0f * px, tagH, colour::onColour, visage::Font::kLeft, 11.5f, Weight::semibold);
    drawLabel(canvas, context_, formatAxisValue(program_, true, padX), xTagX + xTagW - 90.0f * px, xTagY, 80.0f * px,
              tagH, colour::onColour, visage::Font::kRight, 11.0f, Weight::semibold);

    // Top-right Live Articulation Status HUD Pill (deep obsidian backdrop for razor legibility over bright art)
    const std::string status = liveStatusBadge(program_, padX, padY, engaged, holdOn);
    const float hudW = 290.0f * px;
    const float hudH = 24.0f * px;
    const float hudX = art.x + art.w - hudW - 10.0f * px;
    const float hudY = art.y + 10.0f * px;
    canvas.setColor(0xe60a0d13u);
    canvas.roundedRectangle(hudX, hudY, hudW, hudH, 6.0f * px);
    canvas.setColor(colour::withAlpha(sheetCol, engaged ? 0.92f : 0.48f));
    canvas.roundedRectangleBorder(hudX, hudY, hudW, hudH, 6.0f * px, 1.2f * px);
    const float sDot = 6.5f * px;
    canvas.setColor(engaged ? sheetCol : colour::textDim);
    canvas.circle(hudX + 9.0f * px, hudY + (hudH - sDot) * 0.5f, sDot);
    drawLabel(canvas, context_, status, hudX + 20.0f * px, hudY, hudW - 28.0f * px, hudH, colour::text,
              visage::Font::kLeft, 10.0f, Weight::semibold);

    canvas.restoreState();

    // Crisp recessed frame border around art well
    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.roundedRectangleBorder(art.x, art.y, art.w, art.h, 8.0f * px, 2.0f * px);

    // 6. Four Inward Rail Marker Triangles on the Coloured Bezel
    const float mInset = 3.5f * px;
    const float mLen = 6.5f * px;
    const float mReach = 5.0f * px;
    canvas.setColor(colour::onColour);
    canvas.triangle(mx, art.y + art.h + mInset, mx - mReach, art.y + art.h + mInset + mLen, mx + mReach,
                    art.y + art.h + mInset + mLen);
    canvas.triangle(mx, art.y - mInset, mx - mReach, art.y - mInset - mLen, mx + mReach, art.y - mInset - mLen);
    canvas.triangle(art.x + art.w + mInset, my, art.x + art.w + mInset + mLen, my - mReach,
                    art.x + art.w + mInset + mLen, my + mReach);
    canvas.triangle(art.x - mInset, my, art.x - mInset - mLen, my - mReach, art.x - mInset - mLen, my + mReach);

    // 7. Sleek Bottom Performance Dock Card
    const float dockY = height() - context_.px(kDockHeight);
    drawPanel(canvas, context_, 0.0f, dockY, width(), context_.px(kDockHeight), colour::panel, metric::radius);

    // Subtle dock group labels (QUANT / STYLE / XY->CC)
    const float lblH = 12.0f * px;
    const float lblY = dockY + 2.0f * px;
    (void)lblH;
    (void)lblY;
    // The hint fills the dock to the right of the controls, which end at 664 design pixels.
    const float hintX = context_.px(672.0f);
    drawLabel(canvas, context_, "Double-click pad to Hold \u00b7 Shift for fine drag", hintX, dockY,
              std::max(0.0f, width() - hintX - context_.px(12.0f)), context_.px(kDockHeight), colour::textFaint,
              visage::Font::kCenter, 9.5f, Weight::regular);
}

void PlayPanel::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float dockY = height() - px(kDockHeight);
    const float bh = px(28.0f);
    const float cy = dockY + (px(kDockHeight) - bh) * 0.5f;

    // Left to right: [Hold] [Quant] [XY->CC] [Kit map / Slicer] [Audition] [Drag .mid]
    float x = px(12.0f);
    holdBtn_.setBounds(x, cy, px(76.0f), bh);
    x += px(76.0f + 8.0f);
    quantChip_.setBounds(x, cy, px(76.0f), bh);
    x += px(76.0f + 8.0f);
    ccChip_.setBounds(x, cy, px(102.0f), bh);
    x += px(102.0f + 8.0f);
    noteMapChip_.setBounds(x, cy, px(142.0f), bh);
    slicerChip_.setBounds(x, cy, px(142.0f), bh);
    x += px(142.0f + 8.0f);
    auditionBtn_.setBounds(x, cy, px(74.0f), bh);
    x += px(74.0f + 8.0f);
    dragMidBtn_.setBounds(x, cy, px(142.0f), bh);
}

void PlayPanel::updatePointer(float x, float y, bool shiftDown) {
    const Rect art = artRect();
    if (art.w <= 1.0f || art.h <= 1.0f)
        return;
    if (shiftDown && !fineDragging_) {
        dragOriginX_ = x;
        dragOriginY_ = y;
        dragStartX_ = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
        dragStartY_ = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    }
    fineDragging_ = shiftDown;

    float nx = (x - art.x) / art.w;
    float ny = (art.y + art.h - y) / art.h;
    if (fineDragging_) {
        nx = dragStartX_ + (x - dragOriginX_) / art.w * 0.1f;
        ny = dragStartY_ + (dragOriginY_ - y) / art.h * 0.1f;
    }
    context_.model.set(ParamId::padX, std::clamp(nx, 0.0f, 1.0f));
    context_.model.set(ParamId::padY, std::clamp(ny, 0.0f, 1.0f));
    redraw();
}

void PlayPanel::endPadTouch() {
    if (touching_) {
        touching_ = false;
        context_.model.set(ParamId::padTouch, 0.0f);
        context_.model.end(ParamId::padTouch);
    }
    if (draggingPad_) {
        draggingPad_ = false;
        context_.model.end(ParamId::padX);
        context_.model.end(ParamId::padY);
        redraw();
    }
}

void PlayPanel::toggleHold() {
    const bool next = context_.model.get(ParamId::padHold) < 0.5f;
    context_.model.begin(ParamId::padHold);
    context_.model.set(ParamId::padHold, next ? 1.0f : 0.0f);
    context_.model.end(ParamId::padHold);
    if (next)
        ring_ = 0.0f;
    redraw();
}

void PlayPanel::selectProgram(int index) {
    const int clamped = std::clamp(index, 0, kPerfPadCount - 1);
    context_.model.begin(ParamId::padProgram);
    context_.model.set(ParamId::padProgram, static_cast<float>(clamped));
    context_.model.end(ParamId::padProgram);
    redraw();
}

void PlayPanel::mouseMove(const visage::MouseEvent& e) {
    const int idx = tabAt(e.position.x, e.position.y);
    if (idx != hoveredTab_) {
        hoveredTab_ = idx;
        redraw();
    }
}

void PlayPanel::mouseExit(const visage::MouseEvent&) {
    if (hoveredTab_ != -1) {
        hoveredTab_ = -1;
        redraw();
    }
}

void PlayPanel::mouseDown(const visage::MouseEvent& e) {
    const int tab = tabAt(e.position.x, e.position.y);
    if (tab >= 0) {
        selectProgram(tab);
        if (e.shouldTriggerPopup())
            toggleHold();
        return;
    }
    if (e.shouldTriggerPopup())
        return;

    if (artRect().contains(e.position.x, e.position.y)) {
        if (e.repeatClickCount() >= 2) {
            toggleHold();
            return;
        }
        endPadTouch();
        draggingPad_ = true;
        fineDragging_ = e.isShiftDown();
        dragOriginX_ = e.position.x;
        dragOriginY_ = e.position.y;
        dragStartX_ = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
        dragStartY_ = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
        ring_ = 0.0f;

        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        updatePointer(e.position.x, e.position.y, e.isShiftDown());

        touching_ = true;
        context_.model.begin(ParamId::padTouch);
        context_.model.set(ParamId::padTouch, 1.0f);
        redraw();
    }
}

void PlayPanel::mouseDrag(const visage::MouseEvent& e) {
    if (draggingPad_)
        updatePointer(e.position.x, e.position.y, e.isShiftDown());
}

void PlayPanel::mouseUp(const visage::MouseEvent& e) {
    if (!draggingPad_)
        return;
    updatePointer(e.position.x, e.position.y, fineDragging_);
    endPadTouch();
}

bool PlayPanel::keyPress(const visage::KeyEvent& key) {
    const float step = key.isShiftDown() ? 0.005f : key.isMainModifier() ? 0.10f : 0.02f;
    const auto nudge = [&](ParamId id, float delta) {
        const float next = std::clamp(context_.model.get(id) + delta, 0.0f, 1.0f);
        context_.model.begin(id);
        context_.model.set(id, next);
        context_.model.end(id);
        redraw();
    };
    if (key.keyCode() == visage::KeyCode::Left) {
        nudge(ParamId::padX, -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Right) {
        nudge(ParamId::padX, step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Down) {
        nudge(ParamId::padY, -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Up) {
        nudge(ParamId::padY, step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Space) {
        toggleHold();
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Home) {
        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        context_.model.set(ParamId::padX, 0.5f);
        context_.model.set(ParamId::padY, 0.5f);
        context_.model.end(ParamId::padX);
        context_.model.end(ParamId::padY);
        redraw();
        return true;
    }
    return false;
}

} // namespace cl::ui
