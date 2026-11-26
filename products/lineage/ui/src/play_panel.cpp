// SPDX-License-Identifier: AGPL-3.0-or-later
#include "play_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace ln::ui {

namespace {
constexpr std::array<const char*, kPadProgramCount> kTabNames{"Sculpt", "Reese", "Strata", "Disperse", "Abyss"};
constexpr std::array<const char*, kPadProgramCount> kTabBadges{"TERRAIN", "7-VOICE", "RESAMPLE", "CAUSTIC", "SUB CORE"};

constexpr float kTabHeight = 38.0f;
constexpr float kTabGap = 4.0f;
constexpr float kTabInset = 5.0f;
constexpr float kDockHeight = 50.0f;
constexpr float kDockGap = 10.0f;
constexpr float kRail = 15.0f;

float easeOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u;
}

std::string formatPct(float norm01) {
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(std::clamp(norm01, 0.0f, 1.0f) * 100.0f)));
    return buf;
}
} // namespace

PlayPanel::PlayPanel(Context& ctx)
    : Panel(ctx), coreChip_(ctx, ParamId::coreMode, colour::reese),
      filt1Chip_(ctx, ParamId::filt1Mode, colour::sculpt), driveChip_(ctx, ParamId::driveType, colour::sculpt),
      dirChip_(ctx, ParamId::strataDir, colour::strata), filt1Row_(ctx, ParamId::filt1Hz, colour::sculpt, "Filter", true),
      driveRow_(ctx, ParamId::drive, colour::sculpt, "Drive", true),
      genMixRow_(ctx, ParamId::genMix, colour::strata, "Gen Mix", true),
      subRow_(ctx, ParamId::subLevel, colour::sub, "Sub", true) {
    setAcceptsKeystrokes(true);
    for (visage::Frame* child : std::initializer_list<visage::Frame*>{
             &coreChip_, &filt1Chip_, &driveChip_, &dirChip_, &filt1Row_, &driveRow_, &genMixRow_, &subRow_}) {
        addChild(child);
    }

    // Live modulation ghosts on the PLAY dock sliders
    filt1Row_.setModProvider(
        [this] { return toNormalised(spec(ParamId::filt1Hz), context_.model.visuals().effFilt1Hz); });
    driveRow_.setModProvider([this] { return toNormalised(spec(ParamId::drive), context_.model.visuals().effDrive); });
    genMixRow_.setModProvider(
        [this] { return toNormalised(spec(ParamId::genMix), context_.model.visuals().effGenMix); });

    program_ = activeProgram();
    programBefore_ = program_;
    motion_.x = context_.model.get(ParamId::padX);
    motion_.y = context_.model.get(ParamId::padY);
    syncDockAccents();
}

PlayPanel::~PlayPanel() { endPadTouch(); }

int PlayPanel::activeProgram() const {
    return std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::padProgram))), 0, kPadProgramCount - 1);
}

PlayPanel::Rect PlayPanel::tabRect(int index) const {
    const float gap = px(kTabGap);
    const float th = px(kTabHeight);
    const float tw = (width() - static_cast<float>(kPadProgramCount - 1) * gap) / static_cast<float>(kPadProgramCount);
    return {static_cast<float>(index) * (tw + gap), 0.0f, tw, th};
}

PlayPanel::Rect PlayPanel::sheetRect() const {
    const float top = px(kTabHeight);
    const float bottom = height() - px(kDockHeight + kDockGap);
    return {0.0f, top, width(), std::max(10.0f, bottom - top)};
}

PlayPanel::Rect PlayPanel::artRect() const {
    const Rect s = sheetRect();
    const float r = px(kRail);
    return {s.x + r, s.y + r, std::max(10.0f, s.w - 2.0f * r), std::max(10.0f, s.h - 2.0f * r)};
}

int PlayPanel::tabAt(float x, float y) const {
    if (y < 0.0f || y > px(kTabHeight))
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
    ripple_ = 0.0f;
    syncDockAccents();
    redraw();
}

void PlayPanel::syncDockAccents() {
    const unsigned col = colour::padAccent(activeProgram());
    filt1Chip_.setAccent(col);
    filt1Row_.setAccent(col);
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
    syncDockAccents();

    constexpr float dt = 1.0f / 60.0f;
    change_ = std::min(1.0f, change_ + dt / 0.18f);
    ripple_ = std::min(1.0f, ripple_ + dt / 0.36f);

    const auto& tel = context_.model.visuals();
    const float tx = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float ty = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    motion_.x += (tx - motion_.x) * std::min(1.0f, dt * 16.0f);
    motion_.y += (ty - motion_.y) * std::min(1.0f, dt * 16.0f);
    motion_.rms += (tel.rms - motion_.rms) * 0.25f;
    motion_.subPeak += (tel.subPeak - motion_.subPeak) * 0.25f;
    motion_.filt1Norm = toNormalised(spec(ParamId::filt1Hz), tel.effFilt1Hz);
    motion_.filt2Norm = toNormalised(spec(ParamId::filt2Hz), tel.effFilt2Hz);
    motion_.drive = tel.effDrive;
    motion_.tone = tel.effTone;
    motion_.genMix = tel.effGenMix;
    motion_.disperse = tel.effDisperse;
    motion_.density = tel.effDensity;
    motion_.width = tel.effWidth;
    motion_.subWarmth = tel.effSubWarmth;
    motion_.playheadNorm = tel.playheadNorm;
    motion_.loopStart = tel.loopStartNorm;
    motion_.loopEnd = tel.loopEndNorm;
    motion_.generation = tel.generation;
    motion_.hasPrint = tel.hasPrint;
    motion_.sounding = tel.sounding;
    motion_.scope = tel.scope;
    motion_.voicePhases = tel.voicePhases;
    motion_.printPeaks = context_.model.printPeaks();

    motion_.phase += dt * (tel.sounding ? 1.55f : 0.62f);
    if (motion_.phase > 10000.0f)
        motion_.phase = std::fmod(motion_.phase, 62.831853f);

    redraw();
}

void PlayPanel::draw(visage::Canvas& canvas) {
    const float sc = context_.scale;
    const auto& tel = context_.model.visuals();
    const unsigned sheetCol =
        colour::lerpArgb(colour::padAccent(programBefore_), colour::padAccent(program_), easeOut(change_));
    const bool engaged = tel.padActive || draggingPad_;

    // 1. Five Jewel-Coded Sculptural Folder Tabs
    for (int i = 0; i < kPadProgramCount; ++i) {
        const Rect tr = tabRect(i);
        const unsigned pCol = colour::padAccent(i);
        const bool isSelected = (i == program_);
        const bool isHovered = (i == hoveredTab_);

        const float inset = isSelected ? 0.0f : kTabInset * sc;
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
        canvas.topRoundedRectangle(tr.x, fy, tr.w, fh + 2.0f * sc, 9.0f * sc);
        if (!isSelected) {
            canvas.setColor(colour::withAlpha(pCol, isHovered ? 0.58f : 0.25f));
            canvas.roundedRectangleBorder(tr.x, fy, tr.w, fh, 9.0f * sc, 1.0f * sc);
        }

        const float textY = tr.y + kTabInset * sc;
        const float textH = tr.h - kTabInset * sc;
        drawLabel(canvas, context_, kTabNames[static_cast<std::size_t>(i)], tr.x + 12.0f * sc, textY, tr.w - 86.0f * sc,
                  textH, nameCol, visage::Font::kLeft, 12.5f, isSelected ? Weight::semibold : Weight::medium);
        const unsigned badgeCol =
            isSelected ? colour::withAlpha(colour::onColour, 0.72f) : colour::withAlpha(colour::textDim, 0.72f);
        drawLabel(canvas, context_, kTabBadges[static_cast<std::size_t>(i)], tr.x + tr.w - 78.0f * sc, textY,
                  66.0f * sc, textH, badgeCol, visage::Font::kRight, 9.2f, Weight::semibold);
    }

    // 2. Luminous Mineral Sheet Bezel
    const Rect sheet = sheetRect();
    canvas.setColor(sheetCol);
    canvas.bottomRoundedRectangle(sheet.x, sheet.y, sheet.w, sheet.h, 12.0f * sc);

    // 3. Giant 60fps Generative GPU Art Well
    const Rect art = artRect();
    if (change_ < 0.995f)
        drawStrataArt(canvas, programBefore_, art.x, art.y, art.w, art.h, motion_, sc, 1.0f);
    drawStrataArt(canvas, program_, art.x, art.y, art.w, art.h, motion_, sc, easeOut(change_));

    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.roundedRectangleBorder(art.x, art.y, art.w, art.h, 8.0f * sc, 2.0f * sc);

    // 4. Crosshair, Ripple Ring, Luminous Puck & Corner Axis Tags
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    const float mx = art.x + padX * art.w;
    const float my = art.y + (1.0f - padY) * art.h;

    canvas.saveState();
    canvas.trimClampBounds(art.x, art.y, art.w, art.h);

    canvas.setColor(colour::withAlpha(0xffffffffu, engaged ? 0.56f : 0.32f));
    canvas.rectangle(art.x, my - 0.5f * sc, art.w, 1.0f * sc);
    canvas.rectangle(mx - 0.5f * sc, art.y, 1.0f * sc, art.h);

    if (ripple_ < 0.995f) {
        const float r = (12.0f + easeOut(ripple_) * 48.0f) * sc;
        canvas.setColor(colour::withAlpha(0xffffffffu, (1.0f - ripple_) * 0.85f));
        canvas.ring(mx - r, my - r, 2.0f * r, 2.0f * sc);
    }

    if (engaged) {
        const float pr = (22.0f + 6.0f * std::clamp(tel.rms, 0.0f, 1.0f)) * sc;
        canvas.setColor(colour::withAlpha(sheetCol, 0.38f));
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 4.5f * sc);
        canvas.setColor(0xffffffffu);
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 1.8f * sc);
    }

    const float dotR = (10.5f + 2.0f * std::clamp(tel.subPeak, 0.0f, 1.0f)) * sc;
    canvas.setColor(colour::withAlpha(0xff000000u, 0.50f));
    canvas.circle(mx - dotR, my - dotR + 2.5f * sc, 2.0f * dotR);
    canvas.setColor(engaged ? colour::onColour : 0xffffffffu);
    canvas.circle(mx - dotR, my - dotR, 2.0f * dotR);
    canvas.setColor(engaged ? 0xffffffffu : colour::onColour);
    canvas.ring(mx - dotR, my - dotR, 2.0f * dotR, 2.4f * sc);

    if (!engaged && !tel.sounding) {
        const float hintW = 282.0f * sc;
        const float hintH = 24.0f * sc;
        const float hx = art.x + (art.w - hintW) * 0.5f;
        const float hy = art.y + 12.0f * sc;
        canvas.setColor(colour::withAlpha(0xff070a10u, 0.74f));
        canvas.roundedRectangle(hx, hy, hintW, hintH, 12.0f * sc);
        canvas.setColor(colour::withAlpha(sheetCol, 0.45f));
        canvas.roundedRectangleBorder(hx, hy, hintW, hintH, 12.0f * sc, 1.0f * sc);
        drawLabel(canvas, context_, "DRAG PAD TO PLAY  \xc2\xb7  PRESS D FOR DRONE", hx, hy, hintW, hintH, colour::text,
                  visage::Font::kCenter, 9.4f, Weight::semibold);
    }

    // Folded corner axis tags (Y top-left, X bottom-right)
    const auto caps = padCaptions(static_cast<PadProgram>(program_));
    const float tagH = 26.0f * sc;
    const float yTagW = 216.0f * sc;
    const float xTagW = 216.0f * sc;

    canvas.setColor(sheetCol);
    canvas.roundedRectangle(art.x - 4.0f * sc, art.y - 4.0f * sc, yTagW + 4.0f * sc, tagH + 4.0f * sc, 8.0f * sc);
    drawLabel(canvas, context_, std::string(caps.yLabel), art.x + 10.0f * sc, art.y, yTagW - 58.0f * sc, tagH,
              colour::onColour, visage::Font::kLeft, 10.8f, Weight::semibold);
    drawLabel(canvas, context_, formatPct(padY), art.x + yTagW - 52.0f * sc, art.y, 44.0f * sc, tagH, colour::onColour,
              visage::Font::kRight, 11.0f, Weight::semibold);

    const float xTagX = art.x + art.w - xTagW;
    const float xTagY = art.y + art.h - tagH;
    canvas.setColor(sheetCol);
    canvas.roundedRectangle(xTagX, xTagY, xTagW + 4.0f * sc, tagH + 4.0f * sc, 8.0f * sc);
    drawLabel(canvas, context_, std::string(caps.xLabel), xTagX + 10.0f * sc, xTagY, xTagW - 58.0f * sc, tagH,
              colour::onColour, visage::Font::kLeft, 10.8f, Weight::semibold);
    drawLabel(canvas, context_, formatPct(padX), xTagX + xTagW - 52.0f * sc, xTagY, 44.0f * sc, tagH, colour::onColour,
              visage::Font::kRight, 11.0f, Weight::semibold);

    canvas.restoreState();

    // 5. Four Inward Rail Marker Triangles
    const float mInset = 3.5f * sc;
    const float mLen = 6.5f * sc;
    const float mReach = 5.0f * sc;
    canvas.setColor(colour::onColour);
    canvas.triangle(mx, art.y + art.h + mInset, mx - mReach, art.y + art.h + mInset + mLen, mx + mReach,
                    art.y + art.h + mInset + mLen);
    canvas.triangle(mx, art.y - mInset, mx - mReach, art.y - mInset - mLen, mx + mReach, art.y - mInset - mLen);
    canvas.triangle(art.x + art.w + mInset, my, art.x + art.w + mInset + mLen, my - mReach,
                    art.x + art.w + mInset + mLen, my + mReach);
    canvas.triangle(art.x - mInset, my, art.x - mInset - mLen, my - mReach, art.x - mInset - mLen, my + mReach);

    // 6. Bottom Performance Dock Card
    const float dockY = height() - px(kDockHeight);
    drawPanel(canvas, context_, 0.0f, dockY, width(), px(kDockHeight), colour::panel, metric::radius);
}

void PlayPanel::resized() {
    const float dockY = height() - px(kDockHeight);
    const float bh = px(28.0f);
    const float cy = dockY + (px(kDockHeight) - bh) * 0.5f;
    const float gap = px(8.0f);

    // Left: Core, Filter 1 Mode, Shaper, Strata Dir
    float x = px(12.0f);
    coreChip_.setBounds(x, cy, px(124.0f), bh);
    x += px(124.0f) + gap;
    filt1Chip_.setBounds(x, cy, px(90.0f), bh);
    x += px(90.0f) + gap;
    driveChip_.setBounds(x, cy, px(84.0f), bh);
    x += px(84.0f) + gap;
    dirChip_.setBounds(x, cy, px(100.0f), bh);

    // Right: 4 high-impact tonal bar sliders (Filter, Drive, Gen Mix, Sub)
    const float sliderW = px(176.0f);
    const float sGap = px(10.0f);
    float rx = width() - px(12.0f);
    subRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    genMixRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    driveRow_.setBounds(rx - sliderW, cy, sliderW, bh);
    rx -= sliderW + sGap;
    filt1Row_.setBounds(rx - sliderW, cy, sliderW, bh);
}

void PlayPanel::updatePointer(float x, float y, bool shiftDown) {
    const Rect art = artRect();
    if (art.w <= 1.0f || art.h <= 1.0f)
        return;
    if (shiftDown && !fineDragging_) {
        dragOriginX_ = x;
        dragOriginY_ = y;
        dragStartX_ = context_.model.get(ParamId::padX);
        dragStartY_ = context_.model.get(ParamId::padY);
    }
    fineDragging_ = shiftDown;

    float nx = (x - art.x) / art.w;
    float ny = (art.y + art.h - y) / art.h;
    if (fineDragging_) {
        nx = dragStartX_ + (x - dragOriginX_) / art.w * 0.14f;
        ny = dragStartY_ + (dragOriginY_ - y) / art.h * 0.14f;
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
            context_.model.set(ParamId::padX, 0.5f);
            context_.model.set(ParamId::padY, 0.5f);
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
        dragStartX_ = context_.model.get(ParamId::padX);
        dragStartY_ = context_.model.get(ParamId::padY);

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
    const float step = key.isShiftDown() ? 0.005f : (key.isMainModifier() ? 0.10f : 0.025f);
    const auto nudge = [&](ParamId id, float delta) {
        context_.model.begin(id);
        context_.model.set(id, std::clamp(context_.model.get(id) + delta, 0.0f, 1.0f));
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
    return false;
}

std::unique_ptr<Panel> makePlayPanel(Context& context) { return std::make_unique<PlayPanel>(context); }

} // namespace ln::ui
