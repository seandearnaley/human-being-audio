// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace cl::ui {

namespace {

constexpr std::array<const char*, 5> kActLabels{"NOTE", "CC", "PAD", "SLICE", "CLIP"};
constexpr std::array<unsigned, 5> kActColours{colour::emerald, colour::cyan, colour::coral, colour::iris,
                                              colour::amber};
constexpr std::array<int, 3> kLengthValues{1, 2, 4};

void drawPill(visage::Canvas& canvas, const Context& ctx, float x, float y, float w, float h, const std::string& label,
              unsigned bg, unsigned border, unsigned fg, float fontSize = 10.5f, Weight weight = Weight::semibold) {
    const float r = ctx.px(4.5f);
    canvas.setColor(bg);
    canvas.roundedRectangle(x, y, w, h, r);
    canvas.setColor(border);
    canvas.roundedRectangleBorder(x, y, w, h, r, ctx.px(1.0f));
    canvas.setColor(fg);
    canvas.text(label, font(ctx, fontSize, weight), visage::Font::kCenter, x, y, w, h);
}

} // namespace

GridStagePanel::GridStagePanel(Context& context) : Panel(context) {}
GridStagePanel::~GridStagePanel() = default;

void GridStagePanel::tick() {
    const auto& vis = context_.model.visuals();
    for (int i = 0; i < 5; ++i) {
        if ((vis.activityMask & (1u << static_cast<unsigned>(i))) != 0u)
            activityHold_[static_cast<std::size_t>(i)] = 1.0f;
        else
            activityHold_[static_cast<std::size_t>(i)] *= 0.86f;
    }
    redraw();
}

int GridStagePanel::editScene() const {
    return std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
}

int GridStagePanel::gridSteps() const {
    if (fillView())
        return kStepsPerBar;
    return patternSteps(context_.model.groove().bank.scenes[static_cast<std::size_t>(editScene())]);
}

GridStagePanel::Layout GridStagePanel::computeLayout(float w, float h, int steps) const {
    Layout lay{};
    const float pad = px(12.0f);
    const float hdrY = px(8.0f);
    const float hdrH = px(26.0f);

    // Header actions, left to right from the grid's left edge: Clone, Clear, Fill, then the 1 | 2 | 4 pills.
    float sx = pad + px(222.0f);
    lay.btnClone = {sx, hdrY, px(68.0f), hdrH};
    sx += px(68.0f + 6.0f);
    lay.btnClear = {sx, hdrY, px(54.0f), hdrH};
    sx += px(54.0f + 6.0f);
    lay.btnFill = {sx, hdrY, px(48.0f), hdrH};
    sx += px(48.0f + 6.0f);
    for (auto& pill : lay.lengthPills) {
        pill = {sx, hdrY, px(26.0f), hdrH};
        sx += px(26.0f + 6.0f);
    }

    lay.rulerY = hdrY + hdrH + px(6.0f);
    lay.sidebarX = pad;
    lay.sidebarW = px(214.0f);
    lay.gridX = lay.sidebarX + lay.sidebarW + px(8.0f);
    lay.gridW = std::max(px(320.0f), w - lay.gridX - pad);
    lay.steps = std::max(1, steps);
    lay.stepW = lay.gridW / static_cast<float>(lay.steps);
    lay.slicerStepW = lay.gridW / static_cast<float>(kMaxSlicerSteps);

    lay.footerY = h - px(20.0f);
    lay.gridY = lay.rulerY + px(14.0f);
    lay.gridH = std::max(px(160.0f), lay.footerY - lay.gridY - px(4.0f));
    lay.rowH = lay.gridH / static_cast<float>(kDrumLaneCount);

    // Slicer mode split: the held-notes preview on top, the 32-step gate strip below.
    const float gap = px(10.0f);
    const float visH = lay.gridH * 0.44f;
    lay.slicerVisRect = {pad, lay.gridY, w - 2.0f * pad, visH};
    lay.slicerGateRect = {lay.gridX, lay.gridY + visH + gap, lay.gridW, lay.gridH - visH - gap};
    return lay;
}

GridStagePanel::Rect GridStagePanel::laneNoteRect(int lane, const Layout& lay) const {
    const float y = lay.gridY + static_cast<float>(lane) * lay.rowH + px(6.0f);
    return {lay.sidebarX + px(70.0f), y, px(36.0f), lay.rowH - px(12.0f)};
}

GridStagePanel::Rect GridStagePanel::laneMuteRect(int lane, const Layout& lay) const {
    const float y = lay.gridY + static_cast<float>(lane) * lay.rowH + px(6.0f);
    return {lay.sidebarX + px(110.0f), y, px(21.0f), lay.rowH - px(12.0f)};
}

GridStagePanel::Rect GridStagePanel::laneSoloRect(int lane, const Layout& lay) const {
    const float y = lay.gridY + static_cast<float>(lane) * lay.rowH + px(6.0f);
    return {lay.sidebarX + px(134.0f), y, px(21.0f), lay.rowH - px(12.0f)};
}

float GridStagePanel::laneEndHandleX(int laneLen, const Layout& lay) const {
    return lay.gridX + static_cast<float>(std::clamp(laneLen, 1, lay.steps)) * lay.stepW;
}

float GridStagePanel::slicerEndHandleX(int length, const Layout& lay) const {
    return lay.gridX + static_cast<float>(std::clamp(length, 1, kMaxSlicerSteps)) * lay.slicerStepW;
}

void GridStagePanel::drawHeader(visage::Canvas& canvas, const Layout& lay, const Visuals& vis, EngineMode mode) {
    const bool drumOn = (mode == EngineMode::drum);
    const char* title = !drumOn ? "MIDI SLICER" : (fillView() ? "FILL" : "PATTERN");
    drawLabel(canvas, context_, title, lay.sidebarX, lay.btnClone.y, lay.sidebarW, lay.btnClone.h,
              !drumOn ? colour::cyan : (fillView() ? colour::amber : colour::iris), visage::Font::kLeft, 10.5f,
              Weight::semibold);

    auto drawAct = [&](const Rect& r, const char* text, unsigned hue, int idx) {
        const bool hov = (hoverAction_ == idx);
        drawPill(canvas, context_, r.x, r.y, r.w, r.h, text, colour::withAlpha(hue, hov ? 0.26f : 0.12f),
                 colour::withAlpha(hue, hov ? 0.90f : 0.45f), hov ? colour::text : hue, 10.0f, Weight::semibold);
    };
    drawAct(lay.btnClone, "Clone \u2192", colour::iris, 0);
    drawAct(lay.btnClear, "Clear", colour::textDim, 1);

    if (drumOn) {
        if (fillView())
            drawPill(canvas, context_, lay.btnFill.x, lay.btnFill.y, lay.btnFill.w, lay.btnFill.h, "Fill",
                     colour::amber, colour::amber, colour::onColour, 10.0f, Weight::semibold);
        else
            drawAct(lay.btnFill, "Fill", colour::amber, 2);

        // Bars in the pattern view, fill beats in the fill view.
        const Pattern& pattern = context_.model.groove().bank.scenes[static_cast<std::size_t>(editScene())];
        const int lit = fillView() ? (kStepsPerBar - fillWindowStart(pattern.fill)) / 4 : patternBars(pattern);
        const unsigned hue = fillView() ? colour::amber : colour::iris;
        for (std::size_t i = 0; i < lay.lengthPills.size(); ++i) {
            const Rect& r = lay.lengthPills[i];
            const bool on = kLengthValues[i] == lit;
            const bool hov = hoverAction_ == 10 + static_cast<int>(i);
            drawPill(canvas, context_, r.x, r.y, r.w, r.h, std::to_string(kLengthValues[i]),
                     on ? hue : colour::withAlpha(hue, hov ? 0.26f : 0.10f),
                     on ? hue : colour::withAlpha(hue, hov ? 0.90f : 0.40f),
                     on ? colour::onColour : (hov ? colour::text : colour::textDim), 10.0f, Weight::semibold);
        }
        const Rect& last = lay.lengthPills.back();
        drawLabel(canvas, context_, fillView() ? "beats" : "bars", last.x + last.w + px(6.0f), last.y, px(40.0f),
                  last.h, colour::textFaint, visage::Font::kLeft, 9.5f, Weight::medium);
    }

    // Right-aligned live activity LEDs and the BPM readout.
    float lx = width() - px(12.0f);
    const float ledH = px(18.0f);
    const float ledY = lay.btnClone.y + (lay.btnClone.h - ledH) * 0.5f;
    for (int i = 4; i >= 0; --i) {
        const float ledW = px(i == 3 ? 42.0f : 38.0f);
        lx -= ledW;
        const float glow = activityHold_[static_cast<std::size_t>(i)];
        const unsigned c = kActColours[static_cast<std::size_t>(i)];
        canvas.setColor(colour::withAlpha(c, 0.08f + 0.34f * glow));
        canvas.roundedRectangle(lx, ledY, ledW, ledH, px(3.5f));
        canvas.setColor(colour::withAlpha(c, 0.25f + 0.75f * glow));
        canvas.roundedRectangleBorder(lx, ledY, ledW, ledH, px(3.5f), px(1.0f));
        canvas.setColor(glow > 0.25f ? colour::text : colour::textDim);
        canvas.text(kActLabels[static_cast<std::size_t>(i)], font(context_, 8.5f, Weight::semibold),
                    visage::Font::kCenter, lx, ledY, ledW, ledH);
        lx -= px(4.0f);
    }

    char bpmBuf[64]{};
    const int rateIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::seqRate) + 0.5f), 0, 2);
    if (rateIdx == 1)
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM", vis.bpm);
    else
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM (%s)", vis.bpm,
                      std::string(choices::seqRate[static_cast<std::size_t>(rateIdx)]).c_str());
    drawLabel(canvas, context_, bpmBuf, lx - px(188.0f), lay.btnClone.y, px(182.0f), lay.btnClone.h, colour::textDim,
              visage::Font::kRight, 10.5f, Weight::medium);
}

void GridStagePanel::drawDrumGrid(visage::Canvas& canvas, const Layout& lay, const Visuals& vis) {
    const GrooveState& groove = context_.model.groove();
    // The grid always shows the scene that edits go to. While a quantized scene switch is pending, the
    // engine is still playing the previous scene, so the playhead, live fill and Repeat window are hidden
    // until the engine reports this scene as the active one.
    const int scene = editScene();
    const bool live = vis.playing && vis.activeScene == scene;
    const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(scene)];
    const float density = context_.model.get(ParamId::density);
    const float dynamics = context_.model.get(ParamId::dynamics);
    const auto noteMap = static_cast<NoteMap>(
        std::clamp(static_cast<int>(context_.model.get(ParamId::noteMap) + 0.5f), 0, kNoteMapCount - 1));
    const bool anySolo =
        std::any_of(pattern.lanes.begin(), pattern.lanes.end(), [](const DrumLane& l) { return l.solo; });

    // What sounds, from the same function and the same maps as the engine.
    const int liveFillBar = (live && vis.position.fillLive) ? vis.position.barInPattern : -1;
    const grid::View view = grid::resolveView(groove, scene, density, dynamics, liveFillBar);
    const ResolvedBar fillBar = grid::resolveFillView(groove, scene, density, dynamics);
    const int fillFrom = fillWindowStart(pattern.fill);
    const int steps = lay.steps;

    // Ruler.
    for (int st = 0; st < steps; st += 4) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        const int bar = st / kStepsPerBar + 1;
        const int beat = (st % kStepsPerBar) / 4 + 1;
        char label[16]{};
        if (beat == 1 && fillView())
            std::snprintf(label, sizeof(label), "FILL");
        else if (beat == 1)
            std::snprintf(label, sizeof(label), "BAR %d", bar);
        else
            std::snprintf(label, sizeof(label), "%d.%d", bar, beat);
        if (steps <= 32 || beat == 1)
            drawLabel(canvas, context_, label, sx + px(2.0f), lay.rulerY, lay.stepW * 3.5f, px(13.0f),
                      beat == 1 ? colour::text : colour::textFaint, visage::Font::kLeft, 9.0f,
                      beat == 1 ? Weight::semibold : Weight::medium);
    }

    // Beat shading and bar lines.
    for (int st = 0; st < steps; ++st) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        if (((st / 4) & 1) == 1) {
            canvas.setColor(0x09ffffff);
            canvas.rectangle(sx, lay.gridY, lay.stepW, lay.gridH);
        }
        if ((st % kStepsPerBar) == 0) {
            canvas.setColor(colour::withAlpha(colour::iris, 0.34f));
            canvas.rectangle(sx, lay.gridY, px(1.5f), lay.gridH);
        } else if ((st % 4) == 0) {
            canvas.setColor(0x20ffffff);
            canvas.rectangle(sx, lay.gridY, px(1.0f), lay.gridH);
        }
    }

    // The fill window: always in the fill view, and over the live bar while a fill plays.
    // Telemetry can lag a bar-count change by a block, so every overlay stays inside the visible columns.
    const int shadeBar = fillView() ? 0 : liveFillBar;
    if (shadeBar >= 0 && shadeBar * kStepsPerBar < steps) {
        const float fx = lay.gridX + static_cast<float>(shadeBar * kStepsPerBar + fillFrom) * lay.stepW;
        const float fw = static_cast<float>(kStepsPerBar - fillFrom) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::amber, 0.07f));
        canvas.rectangle(fx, lay.gridY, fw, lay.gridH);
        canvas.setColor(colour::withAlpha(colour::amber, 0.45f));
        canvas.rectangle(fx, lay.gridY, px(1.2f), lay.gridH);
    }

    // The Repeat window, while the pad holds it.
    if (!fillView() && live && vis.position.repeatStartStep >= 0 && vis.position.repeatStartStep < steps) {
        const float rx = lay.gridX + static_cast<float>(vis.position.repeatStartStep) * lay.stepW;
        const float windowSteps = std::min(static_cast<float>(vis.position.repeatBeats * 4.0),
                                           static_cast<float>(steps - vis.position.repeatStartStep));
        const float rw = windowSteps * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::coral, 0.16f));
        canvas.rectangle(rx, lay.gridY, rw, lay.gridH);
    }

    const int playCol = !live ? -1
                              : (fillView() ? (vis.position.fillLive ? vis.position.stepInBar : -1)
                                            : vis.position.barInPattern * kStepsPerBar + vis.position.stepInBar);

    for (int l = 0; l < kDrumLaneCount; ++l) {
        const auto li = static_cast<std::size_t>(l);
        const float ry = lay.gridY + static_cast<float>(l) * lay.rowH;
        const unsigned hue = colour::lane[li];
        const DrumLane& lane = pattern.lanes[li];
        const bool audible = anySolo ? (lane.solo && !lane.muted) : !lane.muted;
        const float flash = vis.laneFlash[li];
        const int loop = laneLoopSteps(pattern, l);

        // Row stripe and separator.
        canvas.setColor((l & 1) ? 0x07ffffff : 0x00000000);
        canvas.rectangle(lay.sidebarX, ry, width() - 2.0f * lay.sidebarX, lay.rowH);
        canvas.setColor(0x16ffffff);
        canvas.rectangle(lay.sidebarX, ry + lay.rowH - px(1.0f), width() - 2.0f * lay.sidebarX, px(1.0f));

        // Sidebar: hit LED, lane name, note badge, mute, solo, loop length.
        canvas.setColor(colour::withAlpha(hue, audible ? (0.30f + 0.70f * flash) : 0.12f));
        canvas.roundedRectangle(lay.sidebarX, ry + px(4.0f), px(5.0f), lay.rowH - px(8.0f), px(2.5f));
        drawLabel(canvas, context_, std::string(kitLaneName(noteMap, l)), lay.sidebarX + px(10.0f), ry, px(58.0f),
                  lay.rowH, audible ? colour::text : colour::textFaint, visage::Font::kLeft, 10.0f, Weight::semibold);

        const Rect nr = laneNoteRect(l, lay);
        const bool customNote = lane.midiNote >= 1;
        drawPill(canvas, context_, nr.x, nr.y, nr.w, nr.h,
                 midiNoteLabel(effectiveLaneMidiNote(noteMap, l, lane.midiNote)),
                 customNote ? colour::withAlpha(hue, 0.22f) : colour::raised, customNote ? hue : colour::edge,
                 customNote ? colour::text : colour::textDim, 9.0f, Weight::semibold);
        const Rect mr = laneMuteRect(l, lay);
        drawPill(canvas, context_, mr.x, mr.y, mr.w, mr.h, "M",
                 lane.muted ? colour::withAlpha(colour::vermilion, 0.36f) : colour::raised,
                 lane.muted ? colour::vermilion : colour::edge, lane.muted ? colour::text : colour::textFaint, 9.0f,
                 Weight::semibold);
        const Rect sr = laneSoloRect(l, lay);
        drawPill(canvas, context_, sr.x, sr.y, sr.w, sr.h, "S",
                 lane.solo ? colour::withAlpha(colour::amber, 0.36f) : colour::raised,
                 lane.solo ? colour::amber : colour::edge, lane.solo ? colour::text : colour::textFaint, 9.0f,
                 Weight::semibold);
        if (!fillView()) {
            char lenBuf[8]{};
            std::snprintf(lenBuf, sizeof(lenBuf), "%d", loop);
            const bool looped = loop != patternSteps(pattern);
            drawLabel(canvas, context_, lenBuf, lay.sidebarX + px(182.0f), ry, px(30.0f), lay.rowH,
                      looped ? colour::cyan : colour::textDim, visage::Font::kRight, 10.0f,
                      looped ? Weight::semibold : Weight::medium);
        }

        // Cells.
        for (int st = 0; st < steps; ++st) {
            const float cx = lay.gridX + static_cast<float>(st) * lay.stepW + px(2.0f);
            const float cw = std::max(px(2.0f), lay.stepW - px(4.0f));
            const float cy = ry + px(4.0f);
            const float ch = lay.rowH - px(8.0f);
            const bool editable = !fillView() || st >= fillFrom;
            const bool isHover = editable && hoverLane_ == l && hoverStep_ == st;
            const bool isPlay = st == playCol;
            const ResolvedStep& resolved = fillView() ? fillBar.lanes[li][static_cast<std::size_t>(st)]
                                                      : view.bar[static_cast<std::size_t>(st / kStepsPerBar)]
                                                            .lanes[li][static_cast<std::size_t>(st % kStepsPerBar)];
            // In the fill view only fill steps are lit. The main pattern's steps (before the window, and
            // inside it on lanes that draw nothing in the fill) are dim outlines: context, not content.
            const grid::Cell cell = fillView() ? grid::fillCellOf(resolved) : grid::cellOf(resolved);
            const float dim = (fillView() && cell.look == grid::CellLook::outline) ? 0.45f : 1.0f;

            canvas.setColor(isHover ? 0x24ffffff : (isPlay ? 0x1affffff : 0x10ffffff));
            canvas.roundedRectangle(cx, cy, cw, ch, px(3.5f));
            if (cell.look == grid::CellLook::empty)
                continue;

            const bool isFill = cell.look == grid::CellLook::fill;
            const float barH = std::max(px(6.0f), ch * (isFill ? 1.0f : grid::heightFraction(cell.layer)));
            const float by = cy + (ch - barH);
            const unsigned tone = isFill ? colour::amber : hue;

            if (cell.look == grid::CellLook::outline) {
                // Written but silent: Density, mute, or another lane's solo.
                canvas.setColor(colour::withAlpha(tone, 0.45f * dim));
                canvas.roundedRectangleBorder(cx, by, cw, barH, px(3.5f), px(1.0f));
            } else {
                const float vNorm = std::clamp(static_cast<float>(cell.velocity) / 127.0f, 0.14f, 1.0f);
                canvas.setColor(colour::withAlpha(tone, (isPlay ? 0.96f : (0.46f + 0.48f * vNorm)) * dim));
                canvas.roundedRectangle(cx, by, cw, barH, px(3.5f));
                if (isPlay) {
                    canvas.setColor(colour::withAlpha(0xffffffff, 0.85f));
                    canvas.roundedRectangle(cx + px(1.5f), by + px(1.0f), cw - px(3.0f), px(2.5f), px(1.2f));
                }
            }
            if (cell.flam) {
                canvas.setColor(colour::withAlpha(0xffffffff, dim));
                canvas.roundedRectangle(cx - px(1.5f), by + px(3.0f), px(2.5f), std::max(px(4.0f), barH - px(6.0f)),
                                        px(1.0f));
            }
            if (cell.ratchet >= 2 && cw >= px(12.0f)) {
                char rBuf[8]{};
                std::snprintf(rBuf, sizeof(rBuf), "x%d", static_cast<int>(cell.ratchet));
                canvas.setColor(0xd80d1018);
                canvas.roundedRectangle(cx + px(1.0f), cy + px(1.0f), cw - px(2.0f), px(12.0f), px(2.5f));
                canvas.setColor(colour::amber);
                canvas.text(rBuf, font(context_, 8.5f, Weight::semibold), visage::Font::kCenter, cx, cy, cw, px(13.0f));
            }
        }

        // Loop-length handle, in the pattern view.
        if (!fillView()) {
            const float hx = laneEndHandleX(loop, lay);
            const bool hot = (hoverLengthLane_ == l) || (dragMode_ == DragMode::laneLength && dragLane_ == l);
            canvas.setColor(colour::withAlpha(loop != patternSteps(pattern) ? colour::cyan : hue, hot ? 1.0f : 0.50f));
            canvas.roundedRectangle(hx - px(2.0f), ry + px(2.0f), px(4.0f), lay.rowH - px(4.0f), px(2.0f));
        }
    }

    // Playhead: the left edge of the step telemetry reports.
    if (playCol >= 0 && playCol < steps) {
        const float pxPos = lay.gridX + static_cast<float>(playCol) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::vermilion, 0.20f));
        canvas.rectangle(pxPos - px(3.0f), lay.gridY, px(6.0f), lay.gridH);
        canvas.setColor(colour::vermilion);
        canvas.rectangle(pxPos - px(1.0f), lay.gridY, px(2.0f), lay.gridH);
    }
}

void GridStagePanel::drawSlicerStage(visage::Canvas& canvas, const Layout& lay, const Visuals& vis) {
    const auto slicerStyle =
        static_cast<SlicerStyle>(std::clamp(static_cast<int>(context_.model.get(ParamId::slicerStyle) + 0.5f), 0, 4));
    const GrooveBank& bank = context_.model.groove().bank;
    const auto& vr = lay.slicerVisRect;
    const auto& gr = lay.slicerGateRect;

    // 1. The held notes and the phrase the slicer makes from them (or the captured clip).
    drawPanel(canvas, context_, vr.x, vr.y, vr.w, vr.h, colour::withAlpha(colour::window, 0.85f), 6.0f);

    if (slicerStyle == SlicerStyle::clipSlice) {
        char clipBuf[128]{};
        if (vis.capturedClipCount > 0) {
            std::snprintf(clipBuf, sizeof(clipBuf),
                          "LIVE CLIP RE-SLICER \u00b7 %d captured MIDI events in 1-bar ring buffer",
                          vis.capturedClipCount);
        } else {
            std::snprintf(
                clipBuf, sizeof(clipBuf),
                "LIVE CLIP RE-SLICER \u00b7 Feed any MIDI clip from Ableton Live 12 to slice, swing & pad-perform");
        }
        drawLabel(canvas, context_, clipBuf, vr.x + px(14.0f), vr.y + px(10.0f), vr.w - px(28.0f), px(20.0f),
                  vis.capturedClipCount > 0 ? colour::cyan : colour::textDim, visage::Font::kLeft, 11.5f,
                  Weight::semibold);
    } else if (vis.heldNotes.count > 0) {
        drawLabel(canvas, context_, "HELD NOTES", vr.x + px(14.0f), vr.y + px(8.0f), vr.w - px(28.0f), px(18.0f),
                  colour::cyan, visage::Font::kLeft, 10.0f, Weight::semibold);
        float nx = vr.x + px(14.0f);
        const float ny = vr.y + px(30.0f);
        for (int i = 0; i < vis.heldNotes.count; ++i) {
            const int noteNum = static_cast<int>(vis.heldNotes.notes[static_cast<std::size_t>(i)]);
            const unsigned c = (i == 0 && slicerStyle == SlicerStyle::bassStab) ? colour::vermilion : colour::cyan;
            drawPill(canvas, context_, nx, ny, px(44.0f), px(24.0f), midiNoteLabel(noteNum),
                     colour::withAlpha(c, 0.24f), c, colour::text, 10.5f, Weight::semibold);
            nx += px(50.0f);
        }
        // The compiled phrase, below the note pills.
        const float rollY = vr.y + px(62.0f);
        const float rollH = vr.h - px(72.0f);
        const float rollX = vr.x + px(14.0f);
        const float rollW = vr.w - px(28.0f);
        int lo = 127, hi = 0;
        for (int i = 0; i < vis.heldNotes.count; ++i) {
            lo = std::min(lo, static_cast<int>(vis.heldNotes.notes[static_cast<std::size_t>(i)]));
            hi = std::max(hi, static_cast<int>(vis.heldNotes.notes[static_cast<std::size_t>(i)]));
        }
        const int span = std::max(12, hi - lo + 4);
        const double phraseBeats = std::max(1.0e-6, vis.slicerPlan.phraseBeats);
        for (int i = 0; i < vis.slicerPlan.noteCount; ++i) {
            const SlicerNote& n = vis.slicerPlan.notes[static_cast<std::size_t>(i)];
            const float x0 = rollX + static_cast<float>(n.beat / phraseBeats) * rollW;
            const float nw = std::max(px(4.0f), static_cast<float>(n.durBeats / phraseBeats) * rollW);
            const float frac = static_cast<float>(static_cast<int>(n.note) - (lo - 2)) / static_cast<float>(span);
            const float ny2 = rollY + (1.0f - std::clamp(frac, 0.0f, 1.0f)) * (rollH - px(10.0f));
            const unsigned c =
                (n.voice == 0 && slicerStyle == SlicerStyle::bassStab) ? colour::vermilion : colour::cyan;
            canvas.setColor(colour::withAlpha(c, 0.75f));
            canvas.roundedRectangle(x0, ny2, nw, px(8.0f), px(2.5f));
        }
    } else {
        drawLabel(canvas, context_,
                  "Play or route a MIDI chord / bassline from Live 12 (or enable Hold MIDI) to slice & articulate",
                  vr.x + px(14.0f), vr.y + vr.h * 0.5f - px(12.0f), vr.w - px(28.0f), px(24.0f), colour::textDim,
                  visage::Font::kCenter, 12.0f, Weight::medium);
    }

    // 2. The gate strip: literal steps, 32 columns whatever its length.
    drawLabel(canvas, context_, "SLICE GATE", lay.sidebarX + px(8.0f), gr.y + px(8.0f), lay.sidebarW - px(12.0f),
              px(18.0f), colour::cyan, visage::Font::kLeft, 11.0f, Weight::semibold);
    char lenInfo[48]{};
    const int slLen = std::clamp(static_cast<int>(bank.slicerLength), 1, kMaxSlicerSteps);
    std::snprintf(lenInfo, sizeof(lenInfo), "Loop: %d steps (drag handle)", slLen);
    drawLabel(canvas, context_, lenInfo, lay.sidebarX + px(8.0f), gr.y + px(30.0f), lay.sidebarW - px(12.0f), px(16.0f),
              colour::textDim, visage::Font::kLeft, 9.5f, Weight::medium);
    // One label per line: drawLabel draws a single line.
    constexpr std::array<const char*, 3> kHelp{"Click: toggle", "Drag: velocity", "Right-click: ratchet, strum"};
    for (std::size_t i = 0; i < kHelp.size(); ++i)
        drawLabel(canvas, context_, kHelp[i], lay.sidebarX + px(8.0f), gr.y + px(56.0f + 15.0f * static_cast<float>(i)),
                  lay.sidebarW - px(12.0f), px(15.0f), colour::textFaint, visage::Font::kLeft, 9.5f, Weight::regular);

    const int playStep = vis.playing ? (vis.position.barInPattern * kStepsPerBar + vis.position.stepInBar) % slLen : -1;
    for (int st = 0; st < kMaxSlicerSteps; ++st) {
        const float cx = gr.x + static_cast<float>(st) * lay.slicerStepW + px(2.0f);
        const float cw = std::max(px(2.0f), lay.slicerStepW - px(4.0f));
        const float cy = gr.y + px(6.0f);
        const float ch = gr.h - px(12.0f);
        if (st >= slLen) {
            // Beyond the gate's length: an empty, dimmed slot.
            canvas.setColor(0x08ffffff);
            canvas.roundedRectangle(cx, cy, cw, ch, px(4.0f));
            continue;
        }
        const bool isPlay = (st == playStep);
        const bool isHov = (hoverLane_ == 99 && hoverStep_ == st);
        canvas.setColor(isHov ? 0x24ffffff : (isPlay ? 0x1cffffff : 0x10ffffff));
        canvas.roundedRectangle(cx, cy, cw, ch, px(4.0f));

        const Step& step = bank.slicerSteps[static_cast<std::size_t>(st)];
        if (!step.on())
            continue;
        const float vNorm = std::clamp(static_cast<float>(step.velocity) / 127.0f, 0.14f, 1.0f);
        const float bh = std::max(px(10.0f), ch * (0.20f + 0.80f * vNorm));
        const float by = cy + (ch - bh);
        canvas.setColor(colour::withAlpha(colour::cyan, isPlay ? 0.96f : (0.48f + 0.46f * vNorm)));
        canvas.roundedRectangle(cx, by, cw, bh, px(4.0f));
        if (step.flam) {
            // Chord strum mark.
            canvas.setColor(0xffffffff);
            canvas.roundedRectangle(cx - px(1.5f), by + px(3.0f), px(2.5f), std::max(px(4.0f), bh - px(6.0f)),
                                    px(1.0f));
        }
        if (step.ratchet >= 2) {
            char rBuf[8]{};
            std::snprintf(rBuf, sizeof(rBuf), "x%d", static_cast<int>(step.ratchet));
            canvas.setColor(0xd80d1018);
            canvas.roundedRectangle(cx + px(2.0f), cy + px(2.0f), cw - px(4.0f), px(14.0f), px(3.0f));
            canvas.setColor(colour::amber);
            canvas.text(rBuf, font(context_, 9.0f, Weight::semibold), visage::Font::kCenter, cx, cy + px(1.0f), cw,
                        px(15.0f));
        }
    }

    // The gate length handle.
    const float hx = slicerEndHandleX(slLen, lay);
    const bool hot = hoverSlicerLen_ || dragMode_ == DragMode::slicerLength;
    canvas.setColor(colour::withAlpha(colour::cyan, hot ? 1.0f : 0.72f));
    canvas.roundedRectangle(hx - px(2.5f), gr.y + px(2.0f), px(5.0f), gr.h - px(4.0f), px(2.5f));
}

void GridStagePanel::draw(visage::Canvas& canvas) {
    drawChrome(canvas);
    const auto& vis = context_.model.visuals();
    const auto mode =
        static_cast<EngineMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f), 0, 1));
    const Layout lay = computeLayout(width(), height(), gridSteps());

    drawHeader(canvas, lay, vis, mode);
    if (mode == EngineMode::drum)
        drawDrumGrid(canvas, lay, vis);
    else
        drawSlicerStage(canvas, lay, vis);

    const char* footer = "Click: step \u00b7 Drag: velocity \u00b7 Shift-click: ratchet \u00b7 Right-click: layer";
    if (mode != EngineMode::drum)
        footer = "MIDI Slicer: your held notes, sliced by the gate strip";
    else if (fillView())
        footer = "Click: step \u00b7 Drag: velocity \u00b7 Shift-click: ratchet"; // a fill step has no layer
    drawLabel(canvas, context_, footer, lay.sidebarX, lay.footerY, width() - 2.0f * lay.sidebarX, px(16.0f),
              colour::textFaint, visage::Font::kLeft, 9.5f, Weight::regular);
}

std::unique_ptr<Panel> makeGridStagePanel(Context& context) { return std::make_unique<GridStagePanel>(context); }

} // namespace cl::ui
