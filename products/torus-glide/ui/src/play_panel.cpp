// SPDX-License-Identifier: AGPL-3.0-or-later
#include "play_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace tg::ui {

namespace {
constexpr std::array<const char*, kPadProgramCount> kTabNames{"Spiral",
                                                              "M\xc3\xb6"
                                                              "bius",
                                                              "Copedent",
                                                              "Abyss",
                                                              "Risset",
                                                              "Void"};
constexpr std::array<const char*, kPadProgramCount> kTabBadges{"TORUS", "LOOP", "BEND", "SUB", "GATE", "HALO"};

constexpr float kTabHeight = 38.0f;
constexpr float kTabGap = 4.0f;
constexpr float kTabInset = 5.0f;
constexpr float kDockHeight = 48.0f;
constexpr float kDockGap = 10.0f;
constexpr float kRail = 16.0f;

float easeOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u;
}

float fracF(float v) { return v - std::floor(v); }

std::string formatAxisX(int program, float nx, const TelemetrySnapshot& tel) {
    char buf[48]{};
    switch (program) {
    case 0:
    case 1: {
        const float minV = program == 0 ? -2.0f : -1.5f;
        const float maxV = program == 0 ? 3.0f : 2.5f;
        const float vel = nx < 0.25f   ? minV * (1.0f - 4.0f * nx)
                          : nx < 0.50f ? 4.0f * (nx - 0.25f)
                                       : 1.0f + (maxV - 1.0f) * 2.0f * (nx - 0.50f);
        if (std::abs(vel) < 0.08f)
            return "FREEZE";
        std::snprintf(buf, sizeof(buf), "%+.1f\xc3\x97", static_cast<double>(vel));
        return buf;
    }
    case 2: {
        const int sc = std::clamp(static_cast<int>(std::lround(nx * 3.0f)), 0, 3);
        std::snprintf(buf, sizeof(buf), "%c \xc2\xb7 %d%%", static_cast<char>('A' + sc),
                      static_cast<int>(std::lround(nx * 100.0f)));
        return buf;
    }
    case 3:
    case 4:
    case 5: {
        const int pct = static_cast<int>(std::lround(nx * 100.0f));
        if (program == 4 && pct < 3)
            return "OFF";
        (void)tel;
        std::snprintf(buf, sizeof(buf), "%d%%", pct);
        return buf;
    }
    }
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(nx * 100.0f)));
    return buf;
}

std::string formatAxisY(float ny) {
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(ny * 100.0f)));
    return buf;
}
} // namespace

PlayPanel::PlayPanel(Context& ctx)
    : Panel(ctx), modeChip_(ctx, ParamId::mode, colour::spiral, "Mode"),
      timbreChip_(ctx, ParamId::timbre, colour::spiral), loopChip_(ctx, ParamId::loopBars, colour::textDim),
      stackChip_(ctx, ParamId::spiralStack, colour::spiral), voicingChip_(ctx, ParamId::voicingPack, colour::copedent),
      filterRow_(ctx, ParamId::filterHz, colour::spiral, "Filter", true),
      driveRow_(ctx, ParamId::drive, colour::drive, "Drive", true),
      subRow_(ctx, ParamId::subAnchor, colour::mobius, "Sub", true),
      spaceRow_(ctx, ParamId::space, colour::voidRose, "Halo", true) {
    setAcceptsKeystrokes(true);
    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&modeChip_, &timbreChip_, &loopChip_, &stackChip_, &voicingChip_,
                                               &filterRow_, &driveRow_, &subRow_, &spaceRow_}) {
        addChild(child);
    }

    program_ = activeProgram();
    programBefore_ = program_;
    motion_.x = context_.model.get(ParamId::padX) * 0.01f;
    motion_.y = context_.model.get(ParamId::padY) * 0.01f;

    context_.live.push_back(this);
    syncDockVisibility();
}

PlayPanel::~PlayPanel() {
    endPadTouch();
    std::erase(context_.live, static_cast<Live*>(this));
}

int PlayPanel::activeProgram() const {
    return std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::padProgram))), 0, kPadProgramCount - 1);
}

PlayPanel::Rect PlayPanel::tabRect(int index) const {
    const float gap = context_.px(kTabGap);
    const float th = context_.px(kTabHeight);
    const float tw = (width() - static_cast<float>(kPadProgramCount - 1) * gap) / static_cast<float>(kPadProgramCount);
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
    for (int i = 0; i < kPadProgramCount; ++i)
        if (tabRect(i).contains(x, y))
            return i;
    return -1;
}

void PlayPanel::selectProgram(int index) {
    const int clamped = std::clamp(index, 0, kPadProgramCount - 1);
    context_.model.begin(ParamId::padProgram);
    context_.model.set(ParamId::padProgram, static_cast<float>(clamped));
    context_.model.end(ParamId::padProgram);

    // Selecting Spiral, Mobius, or Copedent world tabs smoothly aligns the core synthesis mode!
    if (clamped <= 2) {
        const int targetMode = clamped == 0 ? 0 : (clamped == 1 ? 2 : 1);
        const int curMode = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        if (curMode != targetMode) {
            context_.model.begin(ParamId::mode);
            context_.model.set(ParamId::mode, static_cast<float>(targetMode));
            context_.model.end(ParamId::mode);
            if (targetMode == 2 && !context_.model.sceneMatrix().isCustom) {
                const int pack = static_cast<int>(std::lround(context_.model.get(ParamId::voicingPack)));
                if (pack < 6) {
                    context_.model.begin(ParamId::voicingPack);
                    context_.model.set(ParamId::voicingPack, 6.0f);
                    context_.model.end(ParamId::voicingPack);
                }
            }
        }
    }
    ripple_ = 0.0f;
    syncDockVisibility();
    redraw();
}

void PlayPanel::syncDockVisibility() {
    const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
    const unsigned col = colour::padAccent(activeProgram());
    modeChip_.setAccent(col);
    timbreChip_.setAccent(col);
    stackChip_.setAccent(col);
    voicingChip_.setAccent(col);
    filterRow_.setAccent(col);
    if (modeIdx != shownMode_) {
        shownMode_ = modeIdx;
        stackChip_.setVisible(modeIdx == 0);
        voicingChip_.setVisible(modeIdx != 0);
        resized();
    }
}

void PlayPanel::tick() {
    if (!isVisible()) {
        endPadTouch();
        return;
    }

    const int nextProg = activeProgram();
    if (nextProg != program_) {
        programBefore_ = change_ >= 0.5f ? program_ : programBefore_;
        program_ = nextProg;
        change_ = 0.0f;
    }
    syncDockVisibility();

    constexpr float dt = 1.0f / 60.0f;
    change_ = std::min(1.0f, change_ + dt / 0.18f);
    ripple_ = std::min(1.0f, ripple_ + dt / 0.36f);

    const auto& tel = context_.model.visuals();
    const float tx = std::clamp(context_.model.get(ParamId::padX) * 0.01f, 0.0f, 1.0f);
    const float ty = std::clamp(context_.model.get(ParamId::padY) * 0.01f, 0.0f, 1.0f);
    motion_.x += (tx - motion_.x) * std::min(1.0f, dt * 16.0f);
    motion_.y += (ty - motion_.y) * std::min(1.0f, dt * 16.0f);
    motion_.spiralPos = tel.spiralPos;
    motion_.morphPos = tel.morphPos;
    motion_.barPhase = tel.barPhase;
    motion_.gateLevel = tel.gateLevel;
    motion_.rms += (tel.rmsLevel - motion_.rms) * 0.25f;
    motion_.subPeak += (tel.subPeak - motion_.subPeak) * 0.25f;
    motion_.padAmt = tel.padAmount;
    motion_.spiralVel = tel.spiralVel;
    motion_.partialU = tel.spiralPartialU;
    motion_.partialGain = tel.spiralPartialGain;
    motion_.voicePitches = tel.voicePitches;

    const float speedScale = tel.sounding ? std::clamp(std::abs(tel.spiralVel), 0.12f, 2.8f) : 0.45f;
    const float dirSign = tel.spiralVel < -0.05f ? -1.0f : 1.0f;
    motion_.phase += dt * (0.85f + 0.90f * speedScale) * dirSign;
    if (std::abs(motion_.phase) > 10000.0f)
        motion_.phase = std::fmod(motion_.phase, 62.831853f);

    redraw();
}

void PlayPanel::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const auto& tel = context_.model.visuals();
    const unsigned sheetCol =
        colour::lerpArgb(colour::padAccent(programBefore_), colour::padAccent(program_), easeOut(change_));
    const bool engaged = tel.padActive || draggingPad_;

    // 1. Six Jewel-Coded Manifold Folder Tabs
    for (int i = 0; i < kPadProgramCount; ++i) {
        const Rect tr = tabRect(i);
        const unsigned pCol = colour::padAccent(i);
        const bool isSelected = (i == program_);
        const bool isHovered = (i == hoveredTab_);

        const float inset = isSelected ? 0.0f : kTabInset * px;
        const float fy = tr.y + inset;
        const float fh = tr.h - inset;

        unsigned faceCol = colour::raised;
        unsigned nameCol = pCol;
        if (isSelected) {
            faceCol = pCol;
            nameCol = colour::onColour;
        } else if (isHovered) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.18f);
            nameCol = colour::text;
        }

        canvas.setColor(faceCol);
        canvas.topRoundedRectangle(tr.x, fy, tr.w, fh + 2.0f * px, 9.0f * px);
        if (!isSelected) {
            canvas.setColor(colour::withAlpha(pCol, isHovered ? 0.58f : 0.25f));
            canvas.roundedRectangleBorder(tr.x, fy, tr.w, fh, 9.0f * px, 1.0f * px);
        }

        const float textY = tr.y + kTabInset * px;
        const float textH = tr.h - kTabInset * px;
        drawLabel(canvas, context_, kTabNames[static_cast<std::size_t>(i)], tr.x + 10.0f * px, textY, tr.w - 64.0f * px,
                  textH, nameCol, visage::Font::kLeft, 12.5f, isSelected ? Weight::semibold : Weight::medium);

        const unsigned badgeCol =
            isSelected ? colour::withAlpha(colour::onColour, 0.68f) : colour::withAlpha(colour::textDim, 0.72f);
        drawLabel(canvas, context_, kTabBadges[static_cast<std::size_t>(i)], tr.x + tr.w - 58.0f * px, textY,
                  48.0f * px, textH, badgeCol, visage::Font::kRight, 9.2f, Weight::semibold);
    }

    // 2. Luminous Colored Sheet Bezel
    const Rect sheet = sheetRect();
    canvas.setColor(sheetCol);
    canvas.bottomRoundedRectangle(sheet.x, sheet.y, sheet.w, sheet.h, 12.0f * px);

    // 3. Inner 60fps Generative GPU Pitch-Manifold Art Well with smooth crossfade
    const Rect art = artRect();
    if (change_ < 0.995f)
        drawManifoldArt(canvas, programBefore_, art.x, art.y, art.w, art.h, motion_, px, 1.0f);
    drawManifoldArt(canvas, program_, art.x, art.y, art.w, art.h, motion_, px, easeOut(change_));

    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.roundedRectangleBorder(art.x, art.y, art.w, art.h, 8.0f * px, 2.0f * px);

    // 4. Interactive Crosshair, Ripple Ring, Luminous Puck & Folded Corner Axis Tags
    const float padX = std::clamp(context_.model.get(ParamId::padX) * 0.01f, 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY) * 0.01f, 0.0f, 1.0f);
    const float mx = art.x + padX * art.w;
    const float my = art.y + (1.0f - padY) * art.h;

    canvas.saveState();
    canvas.trimClampBounds(art.x, art.y, art.w, art.h);

    canvas.setColor(colour::withAlpha(0xffffffffu, engaged ? 0.56f : 0.34f));
    canvas.rectangle(art.x, my - 0.5f * px, art.w, 1.0f * px);
    canvas.rectangle(mx - 0.5f * px, art.y, 1.0f * px, art.h);

    if (ripple_ < 0.995f) {
        const float r = (12.0f + easeOut(ripple_) * 44.0f) * px;
        canvas.setColor(colour::withAlpha(0xffffffffu, (1.0f - ripple_) * 0.85f));
        canvas.ring(mx - r, my - r, 2.0f * r, 2.0f * px);
    }

    if (engaged) {
        const float pr = (22.0f + 5.0f * std::clamp(tel.rmsLevel, 0.0f, 1.0f)) * px;
        canvas.setColor(colour::withAlpha(sheetCol, 0.38f));
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 4.5f * px);
        canvas.setColor(0xffffffffu);
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 1.8f * px);
    }

    const float beatPulse = tel.sounding ? std::pow(1.0f - fracF(tel.barPhase * 4.0f), 3.0f) : 0.0f;
    const float dotR = 10.5f * (1.0f + 0.18f * beatPulse) * px;
    canvas.setColor(colour::withAlpha(0xff000000u, 0.50f));
    canvas.circle(mx - dotR, my - dotR + 2.5f * px, 2.0f * dotR);
    canvas.setColor(engaged ? colour::onColour : 0xffffffffu);
    canvas.circle(mx - dotR, my - dotR, 2.0f * dotR);
    canvas.setColor(engaged ? 0xffffffffu : colour::onColour);
    canvas.ring(mx - dotR, my - dotR, 2.0f * dotR, 2.4f * px);

    // Subtle center hint when idle so users immediately know touching the pad plays sound!
    if (!engaged && !tel.sounding) {
        const float hintW = 246.0f * px;
        const float hintH = 24.0f * px;
        const float hx = art.x + (art.w - hintW) * 0.5f;
        const float hy = art.y + 12.0f * px;
        canvas.setColor(colour::withAlpha(0xff080c12u, 0.72f));
        canvas.roundedRectangle(hx, hy, hintW, hintH, 12.0f * px);
        canvas.setColor(colour::withAlpha(sheetCol, 0.42f));
        canvas.roundedRectangleBorder(hx, hy, hintW, hintH, 12.0f * px, 1.0f * px);
        drawLabel(canvas, context_, "DRAG PAD TO PLAY  \xc2\xb7  SPACE FOR DRONE", hx, hy, hintW, hintH, colour::text,
                  visage::Font::kCenter, 9.4f, Weight::semibold);
    }

    // Folded corner axis tags (Y top-left, X bottom-right)
    const auto captions = padCaptions(static_cast<PadProgram>(program_));
    const std::string yVal = formatAxisY(padY);
    const std::string xVal = formatAxisX(program_, padX, tel);

    const float tagH = 26.0f * px;
    const float yTagW = 192.0f * px;
    const float xTagW = 204.0f * px;

    // Top-left Y tag
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(art.x - 4.0f * px, art.y - 4.0f * px, yTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
    drawLabel(canvas, context_, std::string(captions.yLabel), art.x + 10.0f * px, art.y, yTagW - 60.0f * px, tagH,
              colour::onColour, visage::Font::kLeft, 11.2f, Weight::semibold);
    drawLabel(canvas, context_, yVal, art.x + yTagW - 54.0f * px, art.y, 44.0f * px, tagH, colour::onColour,
              visage::Font::kRight, 11.2f, Weight::semibold);

    // Bottom-right X tag
    const float xTagX = art.x + art.w - xTagW;
    const float xTagY = art.y + art.h - tagH;
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(xTagX, xTagY, xTagW + 4.0f * px, tagH + 4.0f * px, 8.0f * px);
    drawLabel(canvas, context_, std::string(captions.xLabel), xTagX + 10.0f * px, xTagY, xTagW - 76.0f * px, tagH,
              colour::onColour, visage::Font::kLeft, 11.2f, Weight::semibold);
    drawLabel(canvas, context_, xVal, xTagX + xTagW - 70.0f * px, xTagY, 60.0f * px, tagH, colour::onColour,
              visage::Font::kRight, 11.2f, Weight::semibold);

    canvas.restoreState();

    // 5. Four Inward Rail Marker Triangles
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

    // 6. Bottom Performance Dock Card
    const float dockY = height() - context_.px(kDockHeight);
    drawPanel(canvas, context_, 0.0f, dockY, width(), context_.px(kDockHeight), colour::panel, metric::radius);
}

void PlayPanel::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float dockY = height() - px(kDockHeight);
    const float bh = px(28.0f);
    const float cy = dockY + (px(kDockHeight) - bh) * 0.5f;
    const float gap = px(8.0f);

    // Left: Mode, Timbre, Loop, Stack/Voicing
    float x = px(12.0f);
    modeChip_.setBounds(x, cy, px(114.0f), bh);
    x += px(114.0f) + gap;
    timbreChip_.setBounds(x, cy, px(118.0f), bh);
    x += px(118.0f) + gap;
    loopChip_.setBounds(x, cy, px(82.0f), bh);
    x += px(82.0f) + gap;
    const float ctxW = px(136.0f);
    stackChip_.setBounds(x, cy, ctxW, bh);
    voicingChip_.setBounds(x, cy, ctxW, bh);

    // Right: 4 high-impact tonal bar sliders (Filter, Drive, Sub, Halo)
    const float sliderW = px(164.0f);
    const float sGap = px(10.0f);
    float rx = width() - px(12.0f);
    spaceRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    subRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    driveRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    filterRow_.setBounds(rx - sliderW, cy, sliderW, bh);
}

void PlayPanel::updatePointer(float x, float y, bool shiftDown) {
    const Rect art = artRect();
    if (art.w <= 1.0f || art.h <= 1.0f)
        return;
    if (shiftDown && !fineDragging_) {
        dragOriginX_ = x;
        dragOriginY_ = y;
        dragStartX_ = context_.model.get(ParamId::padX) * 0.01f;
        dragStartY_ = context_.model.get(ParamId::padY) * 0.01f;
    }
    fineDragging_ = shiftDown;

    float nx = (x - art.x) / art.w;
    float ny = (art.y + art.h - y) / art.h;
    if (fineDragging_) {
        nx = dragStartX_ + (x - dragOriginX_) / art.w * 0.12f;
        ny = dragStartY_ + (dragOriginY_ - y) / art.h * 0.12f;
    }
    context_.model.set(ParamId::padX, std::clamp(nx, 0.0f, 1.0f) * 100.0f);
    context_.model.set(ParamId::padY, std::clamp(ny, 0.0f, 1.0f) * 100.0f);
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
    if (e.shouldTriggerPopup())
        return;

    const int tab = tabAt(e.position.x, e.position.y);
    if (tab >= 0) {
        selectProgram(tab);
        return;
    }

    if (artRect().contains(e.position.x, e.position.y)) {
        if (e.repeatClickCount() >= 2) {
            context_.model.begin(ParamId::padX);
            context_.model.begin(ParamId::padY);
            context_.model.set(ParamId::padX, 50.0f);
            context_.model.set(ParamId::padY, 50.0f);
            context_.model.end(ParamId::padX);
            context_.model.end(ParamId::padY);
            ripple_ = 0.0f;
            redraw();
            return;
        }
        endPadTouch();
        draggingPad_ = true;
        fineDragging_ = e.isShiftDown();
        dragOriginX_ = e.position.x;
        dragOriginY_ = e.position.y;
        dragStartX_ = context_.model.get(ParamId::padX) * 0.01f;
        dragStartY_ = context_.model.get(ParamId::padY) * 0.01f;

        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        updatePointer(e.position.x, e.position.y, e.isShiftDown());

        touching_ = true;
        ripple_ = 0.0f;
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
    const float step = key.isShiftDown() ? 0.5f : (key.isMainModifier() ? 10.0f : 2.5f);
    const auto nudge = [&](ParamId id, float delta) {
        const float next = std::clamp(context_.model.get(id) + delta, 0.0f, 100.0f);
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
    if (key.keyCode() == visage::KeyCode::Home) {
        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        context_.model.set(ParamId::padX, 50.0f);
        context_.model.set(ParamId::padY, 50.0f);
        context_.model.end(ParamId::padX);
        context_.model.end(ParamId::padY);
        redraw();
        return true;
    }
    return false;
}

std::unique_ptr<Panel> makePlayPanel(Context& ctx) { return std::make_unique<PlayPanel>(ctx); }

} // namespace tg::ui
