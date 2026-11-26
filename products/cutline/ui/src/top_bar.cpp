// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/cl_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <array>

namespace cl::ui {

namespace {
void drawPill(visage::Canvas& canvas, const Context& ctx, float x, float y, float w, float h, const std::string& label,
              unsigned bg, unsigned border, unsigned fg, float fontSize = 10.5f, Weight weight = Weight::semibold) {
    const float r = ctx.px(5.0f);
    canvas.setColor(bg);
    canvas.roundedRectangle(x, y, w, h, r);
    canvas.setColor(border);
    canvas.roundedRectangleBorder(x, y, w, h, r, ctx.px(1.0f));
    drawLabel(canvas, ctx, label, x, y, w, h, fg, visage::Font::kCenter, fontSize, weight);
}
} // namespace

TopBar::TopBar(Context& context, std::function<void()> showAbout, std::function<void(bool)> onSurfaceChange)
    : context_(context), onSurfaceChange_(std::move(onSurfaceChange)),
      previous_(context, "\u2039", [this] { context_.model.stepPreset(-1); }),
      name_(context, "", [this] { showPresetMenu(); }),
      next_(context, "\u203a", [this] { context_.model.stepPreset(1); }),
      save_(context, "Save", [this] { context_.model.savePreset(); }),
      undo_(context, "Undo", [this] { context_.model.undo(); }),
      redo_(context, "Redo", [this] { context_.model.redo(); }),
      ab_(context, "A", [this] { context_.model.toggleAB(); }) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout),
                                 [this](visage::Canvas& canvas) { drawWordmark(canvas); });
    for (auto* button : {&previous_, &name_, &next_, &save_, &undo_, &redo_, &ab_})
        addChild(button);
    save_.setAccent(colour::textDim);
    ab_.setOnRightClick(
        [this] { hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); }); });
    context_.live.push_back(this);
    tick();
}

TopBar::~TopBar() { std::erase(context_.live, static_cast<Live*>(this)); }

void TopBar::setGridMode(bool grid) {
    if (gridMode_ == grid)
        return;
    gridMode_ = grid;
    redraw();
}

void TopBar::tick() {
    auto current = context_.model.presetName();
    if (current != shownPreset_) {
        shownPreset_ = std::move(current);
        name_.setLabel(shownPreset_);
    }
    hba::ui::syncHistoryButtons(undo_, redo_, ab_, context_.model.canUndo(), context_.model.canRedo(),
                                context_.model.isSlotB(), colour::iris, colour::vermilion);
    redraw();
}

void TopBar::showPresetMenu() {
    hba::ui::showCategorizedPresetMenu(context_.model.presets(), &name_,
                                       [this](int i) { context_.model.loadPreset(i); });
}

void TopBar::drawWordmark(visage::Canvas& canvas) {
    const int prog = std::clamp(static_cast<int>(context_.model.get(ParamId::padProgram) + 0.5f), 0, kPerfPadCount - 1);
    const unsigned accent = gridMode_ ? colour::iris : colour::programArgb(prog);
    hba::ui::drawBrandWordmark(canvas, wordmark_, brand::cutline_logotype_on_dark_svg, 1640.0f / 334.0f, 24.0f, 0.0f,
                               context_.scale, accent);
}

void TopBar::computeRightRects(std::array<Rect, 3>& rates, std::array<Rect, kSceneCount>& scenes, Rect& drumRect,
                               Rect& slicerRect, Rect& playRect, Rect& gridRect) const {
    const auto px = [this](float v) { return context_.px(v); };
    const float bh = px(28.0f);
    const float y = (height() - bh) * 0.5f;

    float rx = width() - px(12.0f);

    // Rightmost: [PLAY | GRID] Surface Switcher
    const float surfHalf = px(68.0f);
    gridRect = {rx - surfHalf, y, surfHalf, bh};
    rx -= surfHalf + px(2.0f);
    playRect = {rx - surfHalf, y, surfHalf, bh};
    rx -= surfHalf + px(14.0f);

    // Middle-right: [DRUMS | SLICER] Engine Mode Switcher
    const float modeHalf = px(72.0f);
    slicerRect = {rx - modeHalf, y, modeHalf, bh};
    rx -= modeHalf + px(2.0f);
    drumRect = {rx - modeHalf, y, modeHalf, bh};
    rx -= modeHalf + px(14.0f);

    // Left of mode: 4 Scene Pads [A] [B] [C] [D]
    const float scW = px(28.0f);
    const float scGap = px(4.0f);
    for (int i = kSceneCount - 1; i >= 0; --i) {
        scenes[static_cast<std::size_t>(i)] = {rx - scW, y, scW, bh};
        rx -= scW + (i > 0 ? scGap : 0.0f);
    }

    // Left of scenes: Global Sequence Rate [0.5x | 1x | 2x]
    rx -= px(14.0f);
    const float rateW = px(34.0f);
    const float rateGap = px(2.0f);
    for (int i = 2; i >= 0; --i) {
        rates[static_cast<std::size_t>(i)] = {rx - rateW, y, rateW, bh};
        rx -= rateW + (i > 0 ? rateGap : 0.0f);
    }
}

void TopBar::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

    std::array<Rect, 3> rates{};
    std::array<Rect, kSceneCount> scenes{};
    Rect drumRect{}, slicerRect{}, playRect{}, gridRect{};
    computeRightRects(rates, scenes, drumRect, slicerRect, playRect, gridRect);

    const auto& v = context_.model.visuals();
    const int prog = std::clamp((v.padEngaged || v.pendingPadProgram >= 0)
                                    ? v.activePadProgram
                                    : static_cast<int>(context_.model.get(ParamId::padProgram) + 0.5f),
                                0, kPerfPadCount - 1);
    const unsigned progCol = colour::programArgb(prog);
    const unsigned activeCol = gridMode_ ? colour::iris : progCol;

    // 0. Global Sequence Rate [0.5x | 1x | 2x]
    const int curRate = std::clamp(static_cast<int>(context_.model.get(ParamId::seqRate) + 0.5f), 0, 2);
    const std::array<unsigned, 3> rateCols{colour::amber, activeCol, colour::cyan};
    for (int i = 0; i < 3; ++i) {
        const auto& r = rates[static_cast<std::size_t>(i)];
        const bool isActive = (curRate == i);
        const bool hov = (hoveredTag_ == 30 + i);
        const unsigned rc = rateCols[static_cast<std::size_t>(i)];
        const unsigned bg = isActive ? colour::withAlpha(rc, i == 1 ? 0.24f : 0.34f)
                                     : (hov ? colour::withAlpha(rc, 0.16f) : colour::raised);
        const unsigned bd = isActive ? rc : colour::edge;
        const unsigned fg = isActive ? colour::text : (hov ? colour::text : colour::textDim);
        drawPill(canvas, context_, r.x, r.y, r.w, r.h, std::string(choices::seqRate[static_cast<std::size_t>(i)]), bg,
                 bd, fg, 10.0f, Weight::semibold);
    }

    // 1. Scenes [A B C D]
    const int reqScene = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    const char* scLabels[4]{"A", "B", "C", "D"};
    for (int i = 0; i < kSceneCount; ++i) {
        const auto& r = scenes[static_cast<std::size_t>(i)];
        const bool isActive = (v.playing ? v.activeScene : reqScene) == i;
        const bool isPending = v.playing && (v.pendingScene == i);
        const bool hov = (hoveredTag_ == i);
        const unsigned bg = isActive ? colour::withAlpha(activeCol, 0.34f)
                                     : (isPending ? colour::withAlpha(colour::cyan, 0.25f)
                                                  : (hov ? colour::withAlpha(activeCol, 0.16f) : colour::raised));
        const unsigned bd = isActive ? activeCol : (isPending ? colour::cyan : colour::edge);
        const unsigned fg = isActive ? colour::text : (isPending ? colour::cyan : colour::textDim);
        drawPill(canvas, context_, r.x, r.y, r.w, r.h, scLabels[i], bg, bd, fg, 11.0f, Weight::semibold);
    }

    // 2. [DRUMS | SLICER] Engine Mode Pill
    const bool isDrum = static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f) == 0;
    drawPill(canvas, context_, drumRect.x, drumRect.y, drumRect.w, drumRect.h, "DRUMS",
             isDrum ? colour::withAlpha(colour::iris, 0.30f) : (hoveredTag_ == 10 ? 0x20ffffffu : colour::raised),
             isDrum ? colour::iris : colour::edge, isDrum ? colour::text : colour::textDim, 10.5f, Weight::semibold);
    drawPill(canvas, context_, slicerRect.x, slicerRect.y, slicerRect.w, slicerRect.h, "SLICER",
             !isDrum ? colour::withAlpha(colour::cyan, 0.30f) : (hoveredTag_ == 11 ? 0x20ffffffu : colour::raised),
             !isDrum ? colour::cyan : colour::edge, !isDrum ? colour::text : colour::textDim, 10.5f, Weight::semibold);

    // 3. Hero [PLAY | GRID] Surface Switcher
    drawPill(canvas, context_, playRect.x, playRect.y, playRect.w, playRect.h, "PLAY",
             !gridMode_ ? progCol : (hoveredTag_ == 20 ? colour::withAlpha(progCol, 0.22f) : colour::raised),
             !gridMode_ ? progCol : colour::edge, !gridMode_ ? colour::onColour : colour::textDim, 11.0f,
             Weight::semibold);
    drawPill(canvas, context_, gridRect.x, gridRect.y, gridRect.w, gridRect.h, "GRID",
             gridMode_ ? colour::iris : (hoveredTag_ == 21 ? colour::withAlpha(colour::iris, 0.22f) : colour::raised),
             gridMode_ ? colour::iris : colour::edge, gridMode_ ? colour::onColour : colour::textDim, 11.0f,
             Weight::semibold);
}

void TopBar::mouseMove(const visage::MouseEvent& e) {
    std::array<Rect, 3> rates{};
    std::array<Rect, kSceneCount> scenes{};
    Rect drumRect{}, slicerRect{}, playRect{}, gridRect{};
    computeRightRects(rates, scenes, drumRect, slicerRect, playRect, gridRect);

    int next = -1;
    for (int i = 0; i < 3; ++i)
        if (rates[static_cast<std::size_t>(i)].contains(e.position.x, e.position.y))
            next = 30 + i;
    for (int i = 0; i < kSceneCount; ++i)
        if (scenes[static_cast<std::size_t>(i)].contains(e.position.x, e.position.y))
            next = i;
    if (drumRect.contains(e.position.x, e.position.y))
        next = 10;
    else if (slicerRect.contains(e.position.x, e.position.y))
        next = 11;
    else if (playRect.contains(e.position.x, e.position.y))
        next = 20;
    else if (gridRect.contains(e.position.x, e.position.y))
        next = 21;

    if (next != hoveredTag_) {
        hoveredTag_ = next;
        redraw();
    }
}

void TopBar::mouseExit(const visage::MouseEvent&) {
    if (hoveredTag_ != -1) {
        hoveredTag_ = -1;
        redraw();
    }
}

void TopBar::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    std::array<Rect, 3> rates{};
    std::array<Rect, kSceneCount> scenes{};
    Rect drumRect{}, slicerRect{}, playRect{}, gridRect{};
    computeRightRects(rates, scenes, drumRect, slicerRect, playRect, gridRect);

    for (int i = 0; i < 3; ++i) {
        if (rates[static_cast<std::size_t>(i)].contains(e.position.x, e.position.y)) {
            context_.model.begin(ParamId::seqRate);
            context_.model.set(ParamId::seqRate, static_cast<float>(i));
            context_.model.end(ParamId::seqRate);
            return;
        }
    }
    for (int i = 0; i < kSceneCount; ++i) {
        if (scenes[static_cast<std::size_t>(i)].contains(e.position.x, e.position.y)) {
            context_.model.begin(ParamId::scene);
            context_.model.set(ParamId::scene, static_cast<float>(i));
            context_.model.end(ParamId::scene);
            return;
        }
    }
    if (drumRect.contains(e.position.x, e.position.y)) {
        context_.model.begin(ParamId::engineMode);
        context_.model.set(ParamId::engineMode, static_cast<float>(EngineMode::drum));
        context_.model.end(ParamId::engineMode);
        return;
    }
    if (slicerRect.contains(e.position.x, e.position.y)) {
        context_.model.begin(ParamId::engineMode);
        context_.model.set(ParamId::engineMode, static_cast<float>(EngineMode::slicer));
        context_.model.end(ParamId::engineMode);
        return;
    }
    if (playRect.contains(e.position.x, e.position.y) && onSurfaceChange_) {
        onSurfaceChange_(false);
        return;
    }
    if (gridRect.contains(e.position.x, e.position.y) && onSurfaceChange_) {
        onSurfaceChange_(true);
        return;
    }
}

void TopBar::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float buttonHeight = px(28.0f);
    const float y = (height() - buttonHeight) * 0.5f;
    const float arrow = px(26.0f);
    const float gap = px(4.0f);

    wordmark_.setBounds(px(12.0f), 0.0f, px(134.0f), height());
    float x = px(156.0f);
    previous_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + gap;
    name_.setBounds(x, y, px(214.0f), buttonHeight);
    x += px(214.0f) + gap;
    next_.setBounds(x, y, arrow, buttonHeight);
    x += arrow + px(10.0f);
    save_.setBounds(x, y, px(48.0f), buttonHeight);
    x += px(48.0f) + px(10.0f);
    undo_.setBounds(x, y, px(44.0f), buttonHeight);
    x += px(44.0f) + gap;
    redo_.setBounds(x, y, px(44.0f), buttonHeight);
    x += px(44.0f) + px(6.0f);
    ab_.setBounds(x, y, px(34.0f), buttonHeight);
}

} // namespace cl::ui
