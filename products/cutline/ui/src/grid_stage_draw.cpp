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
constexpr std::array<unsigned, 5> kActColours{
    colour::emerald, colour::cyan, colour::coral, colour::iris, colour::amber};

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

GridStagePanel::Layout GridStagePanel::computeLayout(float w, float h) const {
    Layout lay{};
    const float pad = px(12.0f);
    const float hdrY = px(8.0f);
    const float hdrH = px(26.0f);

    // Top-left Clean Section Title (Mode & Scenes live in TopBar so zero duplicate chrome!)
    lay.modeDrum = {0.0f, 0.0f, 0.0f, 0.0f};
    lay.modeSlicer = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kSceneCount; ++i)
        lay.scenes[static_cast<std::size_t>(i)] = {0.0f, 0.0f, 0.0f, 0.0f};

    // 4 Quick Action Buttons aligned neatly with the grid left edge!
    float sx = pad + px(222.0f);
    lay.btnMutate = {sx, hdrY, px(68.0f), hdrH};
    sx += px(74.0f);
    lay.btnPoly = {sx, hdrY, px(82.0f), hdrH};
    sx += px(88.0f);
    lay.btnClone = {sx, hdrY, px(68.0f), hdrH};
    sx += px(74.0f);
    lay.btnClear = {sx, hdrY, px(54.0f), hdrH};

    lay.rulerY = hdrY + hdrH + px(6.0f);
    lay.sidebarX = pad;
    lay.sidebarW = px(214.0f);
    lay.gridX = lay.sidebarX + lay.sidebarW + px(8.0f);
    lay.gridW = std::max(px(320.0f), w - lay.gridX - pad);
    lay.stepW = lay.gridW / static_cast<float>(kMaxLaneSteps);

    lay.footerY = h - px(20.0f);
    lay.gridY = lay.rulerY + px(14.0f);
    lay.gridH = std::max(px(160.0f), lay.footerY - lay.gridY - px(4.0f));
    lay.rowH = lay.gridH / static_cast<float>(kDrumLaneCount);

    // Slicer mode split: Top chord/clip visualizer + Bottom interactive 32-step gate strip
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

GridStagePanel::Rect GridStagePanel::laneLockRect(int lane, const Layout& lay) const {
    const float y = lay.gridY + static_cast<float>(lane) * lay.rowH + px(6.0f);
    return {lay.sidebarX + px(158.0f), y, px(21.0f), lay.rowH - px(12.0f)};
}

float GridStagePanel::laneEndHandleX(int laneLen, const Layout& lay) const {
    return lay.gridX + static_cast<float>(std::clamp(laneLen, 1, kMaxLaneSteps)) * lay.stepW;
}

void GridStagePanel::drawHeader(visage::Canvas& canvas, const Layout& lay, const Visuals& vis, EngineMode mode) {
    // Clean Matrix Title on left aligned above track sidebar
    const bool drumOn = (mode == EngineMode::drum);
    const char* title = drumOn ? "8-LANE POLYMETRIC MATRIX" : "LIVE MIDI SLICER MATRIX";
    drawLabel(canvas, context_, title, lay.sidebarX, lay.btnMutate.y, lay.sidebarW, lay.btnMutate.h,
              drumOn ? colour::iris : colour::cyan, visage::Font::kLeft, 10.5f, Weight::semibold);

    // Quick Action buttons
    auto drawAct = [&](const Rect& r, const char* text, unsigned hue, int idx) {
        const bool hov = (hoverAction_ == idx);
        drawPill(canvas, context_, r.x, r.y, r.w, r.h, text,
                 colour::withAlpha(hue, hov ? 0.26f : 0.12f),
                 colour::withAlpha(hue, hov ? 0.90f : 0.45f), hov ? colour::text : hue, 10.0f, Weight::semibold);
    };
    drawAct(lay.btnMutate, "Mutate", colour::amber, 0);
    drawAct(lay.btnPoly, "Polymeter", colour::cyan, 1);
    drawAct(lay.btnClone, "Clone \u2192", colour::iris, 2);
    drawAct(lay.btnClear, "Clear", colour::textDim, 3);

    // Right-aligned live Activity LEDs + BPM readout
    float lx = width() - px(12.0f);
    const float ledH = px(18.0f);
    const float ledY = lay.btnMutate.y + (lay.btnMutate.h - ledH) * 0.5f;
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
        canvas.text(kActLabels[static_cast<std::size_t>(i)], font(context_, 8.5f, Weight::semibold), visage::Font::kCenter,
                    lx, ledY, ledW, ledH);
        lx -= px(4.0f);
    }

    char bpmBuf[64]{};
    const int styleIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::style) + 0.5f), 0, kDrumStyleCount - 1);
    const int rateIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::seqRate) + 0.5f), 0, 2);
    if (rateIdx == 1)
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM \u00b7 %s", vis.bpm,
                      std::string(choices::style[static_cast<std::size_t>(styleIdx)]).c_str());
    else
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM (%s) \u00b7 %s", vis.bpm,
                      std::string(choices::seqRate[static_cast<std::size_t>(rateIdx)]).c_str(),
                      std::string(choices::style[static_cast<std::size_t>(styleIdx)]).c_str());
    drawLabel(canvas, context_, bpmBuf, lx - px(188.0f), lay.btnMutate.y, px(182.0f), lay.btnMutate.h, colour::textDim,
              visage::Font::kRight, 10.5f, Weight::medium);
}

void GridStagePanel::drawDrumGrid(visage::Canvas& canvas, const Layout& lay, const Visuals& vis) {
    const auto bank = context_.model.patternBank();
    const int scIdx = vis.playing ? vis.activeScene
                                  : std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0,
                                               kSceneCount - 1);
    const SceneData sc = effectiveSceneData(bank, scIdx);
    const auto pocket = static_cast<Attractor>(std::clamp(static_cast<int>(context_.model.get(ParamId::attractor) + 0.5f), 0, 4));
    const double groove = static_cast<double>(context_.model.get(ParamId::gravity));
    const bool anySolo = std::any_of(sc.lanes.begin(), sc.lanes.end(), [](const LaneConfig& l) { return l.solo; });

    // Beat / Bar ruler above the 32-step grid
    for (int st = 0; st < kMaxLaneSteps; ++st) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        if ((st % 4) == 0) {
            char bBuf[16]{};
            const int bar = (st / 16) + 1;
            const int beat = ((st % 16) / 4) + 1;
            if (beat == 1)
                std::snprintf(bBuf, sizeof(bBuf), "BAR %d", bar);
            else
                std::snprintf(bBuf, sizeof(bBuf), "%d.%d", bar, beat);
            drawLabel(canvas, context_, bBuf, sx + px(2.0f), lay.rulerY, lay.stepW * 3.5f, px(13.0f),
                      beat == 1 ? colour::text : colour::textFaint, visage::Font::kLeft, 9.0f,
                      beat == 1 ? Weight::semibold : Weight::medium);
        }
    }

    // Alternating beat column shading + curved/shifted Groove Pocket guide lines!
    for (int st = 0; st < kMaxLaneSteps; ++st) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        if (((st / 4) & 1) == 1) {
            canvas.setColor(0x09ffffff);
            canvas.rectangle(sx, lay.gridY, lay.stepW, lay.gridH);
        }
        // Visualize the exact Groove / MPC Swing warped timing position subtly on every step!
        const double rawB = static_cast<double>(st) * 0.25;
        const double warpedB = warpBeat(rawB, groove, pocket);
        const float wx = lay.gridX + static_cast<float>(warpedB * 4.0) * lay.stepW;
        const bool isBarLine = (st % 16) == 0;
        const bool isBeatLine = (st % 4) == 0;
        if (isBarLine) {
            canvas.setColor(colour::withAlpha(colour::iris, 0.34f));
            canvas.rectangle(sx, lay.gridY, px(1.5f), lay.gridH);
        } else if (isBeatLine) {
            canvas.setColor(0x20ffffff);
            canvas.rectangle(sx, lay.gridY, px(1.0f), lay.gridH);
        } else {
            // Subtle swung pocket vertical hairline
            const float delta = std::abs(wx - sx);
            canvas.setColor(delta > px(0.8f) ? colour::withAlpha(colour::vermilion, 0.24f) : 0x0dffffff);
            canvas.rectangle(wx, lay.gridY, px(1.0f), lay.gridH);
        }
    }

    // Subtle Fill turnaround shading on the right of Bar 2 when Fills > 8%
    const float fillsAmt = context_.model.get(ParamId::fills);
    if (fillsAmt >= 0.08f) {
        const int fillSteps = fillsAmt < 0.36f ? 4 : (fillsAmt < 0.72f ? 8 : 12);
        const float fx = lay.gridX + static_cast<float>(kMaxLaneSteps - fillSteps) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::emerald, 0.06f));
        canvas.rectangle(fx, lay.gridY, static_cast<float>(fillSteps) * lay.stepW, lay.gridH);
        canvas.setColor(colour::withAlpha(colour::emerald, 0.40f));
        canvas.rectangle(fx, lay.gridY, px(1.2f), lay.gridH);
    }

    // 8 Interactive Polymetric Drum Lanes
    const int playStep32 = static_cast<int>(std::floor(vis.playheadBeats * 4.0));
    for (int l = 0; l < kDrumLaneCount; ++l) {
        const auto li = static_cast<std::size_t>(l);
        const float ry = lay.gridY + static_cast<float>(l) * lay.rowH;
        const unsigned hue = colour::lane[li];
        const auto& laneCfg = sc.lanes[li];
        const bool audible = anySolo ? (laneCfg.solo && !laneCfg.muted) : !laneCfg.muted;
        const int laneLen = std::clamp(static_cast<int>(vis.plan.laneLengths[li]), 1, kMaxLaneSteps);
        const float flash = vis.laneFlash[li];

        // Row background stripe
        canvas.setColor((l & 1) ? 0x07ffffff : 0x00000000);
        canvas.rectangle(lay.sidebarX, ry, width() - 2.0f * lay.sidebarX, lay.rowH);
        canvas.setColor(0x16ffffff);
        canvas.rectangle(lay.sidebarX, ry + lay.rowH - px(1.0f), width() - 2.0f * lay.sidebarX, px(1.0f));

        // Left Track Header Sidebar
        // 1. Live Hit-Flash LED Bar
        const float ledW = px(5.0f);
        canvas.setColor(colour::withAlpha(hue, audible ? (0.30f + 0.70f * flash) : 0.12f));
        canvas.roundedRectangle(lay.sidebarX, ry + px(4.0f), ledW, lay.rowH - px(8.0f), px(2.5f));
        if (flash > 0.15f && audible) {
            canvas.setColor(colour::withAlpha(hue, 0.12f * flash));
            canvas.roundedRectangle(lay.sidebarX, ry + px(2.0f), lay.sidebarW, lay.rowH - px(4.0f), px(4.0f));
        }

        // 2. Lane Name (dynamic per active Kit Map!)
        const auto activeMap = static_cast<NoteMap>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::noteMap) + 0.5f), 0, kNoteMapCount - 1));
        drawLabel(canvas, context_, std::string(kitLaneName(activeMap, l)), lay.sidebarX + px(10.0f), ry, px(58.0f),
                  lay.rowH, audible ? colour::text : colour::textFaint, visage::Font::kLeft, 10.0f, Weight::semibold);

        // 3. MIDI Note Badge (clickable/draggable)
        const Rect nr = laneNoteRect(l, lay);
        const bool customNote = (laneCfg.midiNote >= 1);
        drawPill(canvas, context_, nr.x, nr.y, nr.w, nr.h, midiNoteLabel(vis.plan.laneMidiNotes[li]),
                 customNote ? colour::withAlpha(hue, 0.22f) : colour::raised,
                 customNote ? hue : colour::edge, customNote ? colour::text : colour::textDim, 9.0f, Weight::semibold);

        // 4. Mute / Solo / Lock pills
        const Rect mr = laneMuteRect(l, lay);
        drawPill(canvas, context_, mr.x, mr.y, mr.w, mr.h, "M",
                 laneCfg.muted ? colour::withAlpha(colour::vermilion, 0.36f) : colour::raised,
                 laneCfg.muted ? colour::vermilion : colour::edge,
                 laneCfg.muted ? colour::text : colour::textFaint, 9.0f, Weight::semibold);

        const Rect sr = laneSoloRect(l, lay);
        drawPill(canvas, context_, sr.x, sr.y, sr.w, sr.h, "S",
                 laneCfg.solo ? colour::withAlpha(colour::amber, 0.36f) : colour::raised,
                 laneCfg.solo ? colour::amber : colour::edge,
                 laneCfg.solo ? colour::text : colour::textFaint, 9.0f, Weight::semibold);

        const Rect lr = laneLockRect(l, lay);
        drawPill(canvas, context_, lr.x, lr.y, lr.w, lr.h, "L",
                 laneCfg.locked ? colour::withAlpha(colour::iris, 0.36f) : colour::raised,
                 laneCfg.locked ? colour::iris : colour::edge,
                 laneCfg.locked ? colour::text : colour::textFaint, 9.0f, Weight::semibold);

        // 5. Polymetric Length readout badge
        char lenBuf[8]{};
        std::snprintf(lenBuf, sizeof(lenBuf), "%d", laneLen);
        const bool isOddPoly = (laneLen != 16 && laneLen != 32);
        drawLabel(canvas, context_, lenBuf, lay.sidebarX + px(182.0f), ry, px(30.0f), lay.rowH,
                  isOddPoly ? colour::cyan : colour::textDim, visage::Font::kRight, 10.0f,
                  isOddPoly ? Weight::semibold : Weight::medium);

        // 32 Interactive Step Cells
        const int activePlayCell = vis.playing ? (playStep32 % laneLen) : -1;
        for (int st = 0; st < kMaxLaneSteps; ++st) {
            const float cx = lay.gridX + static_cast<float>(st) * lay.stepW + px(2.0f);
            const float cw = lay.stepW - px(4.0f);
            const float cy = ry + px(4.0f);
            const float ch = lay.rowH - px(8.0f);
            const bool beyondLen = (st >= laneLen);
            const bool isHover = (hoverLane_ == l && hoverStep_ == st && !beyondLen);

            if (beyondLen) {
                // Dimmed polymetric wrap region beyond this lane's loop length
                const int wrappedSt = st % laneLen;
                const std::uint8_t ghostV = vis.plan.gridVel[li][static_cast<std::size_t>(wrappedSt)];
                if (ghostV > 0 && audible) {
                    const float hFrac = 0.25f + 0.75f * (static_cast<float>(ghostV) / 127.0f);
                    const float bh = ch * hFrac;
                    canvas.setColor(colour::withAlpha(hue, 0.11f));
                    canvas.roundedRectangle(cx, cy + (ch - bh), cw, bh, px(3.0f));
                }
                continue;
            }

            const std::uint8_t vel = vis.plan.gridVel[li][static_cast<std::size_t>(st)];
            const std::uint8_t rat = vis.plan.gridRatchet[li][static_cast<std::size_t>(st)];
            const bool flam = vis.plan.gridFlam[li][static_cast<std::size_t>(st)];
            const auto& edit = laneCfg.steps[static_cast<std::size_t>(st)];
            const bool isPlayCell = (st == activePlayCell);

            // Empty cell slot background
            canvas.setColor(isHover ? 0x24ffffff : (isPlayCell ? 0x1affffff : 0x10ffffff));
            canvas.roundedRectangle(cx, cy, cw, ch, px(3.5f));

            if (vel > 0) {
                const float vNorm = std::clamp(static_cast<float>(vel) / 127.0f, 0.14f, 1.0f);
                const float barH = std::max(px(6.0f), ch * (0.22f + 0.78f * vNorm));
                const float by = cy + (ch - barH);
                const float alpha = audible ? (isPlayCell ? 0.96f : (0.52f + 0.42f * vNorm)) : 0.18f;

                canvas.setColor(colour::withAlpha(hue, alpha));
                canvas.roundedRectangle(cx, by, cw, barH, px(3.5f));

                // Bright crisp top crest on accented / active hits
                if (audible && (vel >= 108 || isPlayCell)) {
                    canvas.setColor(colour::withAlpha(0xffffffff, isPlayCell ? 0.85f : 0.45f));
                    canvas.roundedRectangle(cx + px(1.5f), by + px(1.0f), cw - px(3.0f), px(2.5f), px(1.2f));
                }

                // Explicit user-painted step subtle border
                if (edit.state == StepState::forceOn) {
                    canvas.setColor(colour::withAlpha(0xffffffff, 0.50f));
                    canvas.roundedRectangleBorder(cx, by, cw, barH, px(3.5f), px(1.0f));
                }

                // Flam pre-hit grace tick
                if (flam) {
                    canvas.setColor(0xffffffff);
                    canvas.roundedRectangle(cx - px(1.5f), by + px(3.0f), px(2.5f), std::max(px(4.0f), barH - px(6.0f)),
                                            px(1.0f));
                }

                // Ratchet badge (x2 / x3 / x4) + sub-hit dividers
                if (rat >= 2) {
                    char rBuf[8]{};
                    std::snprintf(rBuf, sizeof(rBuf), "x%d", static_cast<int>(rat));
                    canvas.setColor(0xd80d1018);
                    canvas.roundedRectangle(cx + px(2.0f), cy + px(1.0f), cw - px(4.0f), px(12.0f), px(2.5f));
                    canvas.setColor(colour::amber);
                    canvas.text(rBuf, font(context_, 8.5f, Weight::semibold), visage::Font::kCenter, cx, cy, cw, px(13.0f));
                }
            } else if (edit.state == StepState::forceOff) {
                // Explicitly muted auto step marker
                canvas.setColor(0x38ffffff);
                canvas.text("\u00d7", font(context_, 10.0f, Weight::semibold), visage::Font::kCenter, cx, cy, cw, ch);
            }
        }

        // Draggable Polymeter Right-Edge Handle for this lane!
        const float hx = laneEndHandleX(laneLen, lay);
        const bool hovHandle = (hoverLengthLane_ == l) || (dragMode_ == DragMode::laneLength && dragLane_ == l);
        canvas.setColor(colour::withAlpha(isOddPoly ? colour::cyan : hue, hovHandle ? 1.0f : 0.62f));
        canvas.roundedRectangle(hx - px(2.0f), ry + px(2.0f), px(4.0f), lay.rowH - px(4.0f), px(2.0f));
    }

    // Glowing live playhead across the 32-step window
    if (vis.playing) {
        const double beatIn2Bars = std::fmod(vis.playheadBeats, 8.0);
        const float pxPos = lay.gridX + static_cast<float>(beatIn2Bars * 4.0) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::vermilion, 0.20f));
        canvas.rectangle(pxPos - px(3.0f), lay.gridY, px(6.0f), lay.gridH);
        canvas.setColor(colour::vermilion);
        canvas.rectangle(pxPos - px(1.0f), lay.gridY, px(2.0f), lay.gridH);
    }
}

void GridStagePanel::drawSlicerStage(visage::Canvas& canvas, const Layout& lay, const Visuals& vis) {
    const auto slicerStyle =
        static_cast<SlicerStyle>(std::clamp(static_cast<int>(context_.model.get(ParamId::slicerStyle) + 0.5f), 0, 4));
    const auto bank = context_.model.patternBank();
    const auto& vr = lay.slicerVisRect;
    const auto& gr = lay.slicerGateRect;

    // 1. Top Slicer Visualizer Box (Held Chords / Compiled Sliced Notes / Clip Ring Buffer)
    drawPanel(canvas, context_, vr.x, vr.y, vr.w, vr.h, colour::withAlpha(colour::window, 0.85f), 6.0f);

    if (slicerStyle == SlicerStyle::clipSlice) {
        char clipBuf[128]{};
        if (vis.capturedClipCount > 0) {
            std::snprintf(clipBuf, sizeof(clipBuf),
                          "LIVE CLIP RE-SLICER \u00b7 %d captured MIDI events in 1-bar ring buffer",
                          vis.capturedClipCount);
        } else {
            std::snprintf(clipBuf, sizeof(clipBuf),
                          "LIVE CLIP RE-SLICER \u00b7 Feed any MIDI clip from Ableton Live 12 to slice, swing & pad-perform");
        }
        drawLabel(canvas, context_, clipBuf, vr.x + px(14.0f), vr.y + px(10.0f), vr.w - px(28.0f), px(20.0f),
                  vis.capturedClipCount > 0 ? colour::cyan : colour::textDim, visage::Font::kLeft, 11.5f,
                  Weight::semibold);
    } else if (vis.heldNotes.count > 0) {
        drawLabel(canvas, context_, "ACTIVE INPUT CHORD / NOTES (100% YOUR PITCHES \u00b7 ZERO RANDOM NOTES)",
                  vr.x + px(14.0f), vr.y + px(8.0f), vr.w - px(28.0f), px(18.0f), colour::cyan, visage::Font::kLeft,
                  10.0f, Weight::semibold);
        float nx = vr.x + px(14.0f);
        const float ny = vr.y + px(30.0f);
        for (int i = 0; i < vis.heldNotes.count; ++i) {
            const int noteNum = static_cast<int>(vis.heldNotes.notes[static_cast<std::size_t>(i)]);
            const unsigned c = (i == 0 && slicerStyle == SlicerStyle::bassStab) ? colour::vermilion : colour::cyan;
            drawPill(canvas, context_, nx, ny, px(44.0f), px(24.0f), midiNoteLabel(noteNum),
                     colour::withAlpha(c, 0.24f), c, colour::text, 10.5f, Weight::semibold);
            nx += px(50.0f);
        }
        // Preview compiled sliced note bars below the note pills
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
        for (int i = 0; i < vis.plan.noteCount; ++i) {
            const auto& n = vis.plan.notes[static_cast<std::size_t>(i)];
            const float x0 = rollX + static_cast<float>(n.warpedBeats / vis.plan.phraseBeats) * rollW;
            const float nw = std::max(px(4.0f), static_cast<float>(n.durationBeats / vis.plan.phraseBeats) * rollW);
            const float frac = static_cast<float>(n.noteNumber - (lo - 2)) / static_cast<float>(span);
            const float ny2 = rollY + (1.0f - std::clamp(frac, 0.0f, 1.0f)) * (rollH - px(10.0f));
            const unsigned c = (n.lane == 0 && slicerStyle == SlicerStyle::bassStab) ? colour::vermilion : colour::cyan;
            canvas.setColor(colour::withAlpha(c, 0.75f));
            canvas.roundedRectangle(x0, ny2, nw, px(8.0f), px(2.5f));
        }
    } else {
        drawLabel(canvas, context_,
                  "Play or route a MIDI chord / bassline from Live 12 (or enable Hold MIDI) to slice & articulate",
                  vr.x + px(14.0f), vr.y + vr.h * 0.5f - px(12.0f), vr.w - px(28.0f), px(24.0f), colour::textDim,
                  visage::Font::kCenter, 12.0f, Weight::medium);
    }

    // 2. Bottom Interactive 32-Step Polymetric Slicer Gate / Accent / Ratchet Strip
    drawLabel(canvas, context_, "SLICE GATE", lay.sidebarX + px(8.0f), gr.y + px(8.0f), lay.sidebarW - px(12.0f),
              px(18.0f), colour::cyan, visage::Font::kLeft, 11.0f, Weight::semibold);
    char lenInfo[48]{};
    const int slLen = std::clamp(static_cast<int>(vis.plan.slicerLength), 1, kMaxLaneSteps);
    std::snprintf(lenInfo, sizeof(lenInfo), "Loop: %d steps (drag handle)", slLen);
    drawLabel(canvas, context_, lenInfo, lay.sidebarX + px(8.0f), gr.y + px(30.0f), lay.sidebarW - px(12.0f),
              px(16.0f), colour::textDim, visage::Font::kLeft, 9.5f, Weight::medium);
    drawLabel(canvas, context_, "Click: toggle slice\nDrag Y: accent velocity\nR-click: ratchet / strum",
              lay.sidebarX + px(8.0f), gr.y + px(56.0f), lay.sidebarW - px(12.0f), px(52.0f), colour::textFaint,
              visage::Font::kLeft, 9.5f, Weight::regular);

    const int playStep = vis.playing ? (static_cast<int>(std::floor(vis.playheadBeats * 4.0)) % slLen) : -1;
    for (int st = 0; st < kMaxLaneSteps; ++st) {
        const float cx = gr.x + static_cast<float>(st) * lay.stepW + px(2.0f);
        const float cw = lay.stepW - px(4.0f);
        const float cy = gr.y + px(6.0f);
        const float ch = gr.h - px(12.0f);
        const bool beyond = (st >= slLen);
        if (beyond) {
            const std::uint8_t gv = vis.plan.slicerVel[static_cast<std::size_t>(st % slLen)];
            if (gv > 0) {
                const float bh = ch * (0.25f + 0.75f * (static_cast<float>(gv) / 127.0f));
                canvas.setColor(colour::withAlpha(colour::cyan, 0.10f));
                canvas.roundedRectangle(cx, cy + (ch - bh), cw, bh, px(4.0f));
            }
            continue;
        }
        const bool isPlay = (st == playStep);
        const bool isHov = (hoverLane_ == 99 && hoverStep_ == st);
        canvas.setColor(isHov ? 0x24ffffff : (isPlay ? 0x1cffffff : 0x10ffffff));
        canvas.roundedRectangle(cx, cy, cw, ch, px(4.0f));

        const std::uint8_t vel = vis.plan.slicerVel[static_cast<std::size_t>(st)];
        const std::uint8_t rat = vis.plan.slicerRatchet[static_cast<std::size_t>(st)];
        const auto& ed = bank.slicerSteps[static_cast<std::size_t>(st)];
        if (vel > 0) {
            const float vNorm = std::clamp(static_cast<float>(vel) / 127.0f, 0.14f, 1.0f);
            const float bh = std::max(px(10.0f), ch * (0.20f + 0.80f * vNorm));
            const float by = cy + (ch - bh);
            canvas.setColor(colour::withAlpha(colour::cyan, isPlay ? 0.96f : (0.48f + 0.46f * vNorm)));
            canvas.roundedRectangle(cx, by, cw, bh, px(4.0f));
            if (rat >= 2) {
                char rBuf[8]{};
                std::snprintf(rBuf, sizeof(rBuf), "x%d", static_cast<int>(rat));
                canvas.setColor(0xd80d1018);
                canvas.roundedRectangle(cx + px(2.0f), cy + px(2.0f), cw - px(4.0f), px(14.0f), px(3.0f));
                canvas.setColor(colour::amber);
                canvas.text(rBuf, font(context_, 9.0f, Weight::semibold), visage::Font::kCenter, cx, cy + px(1.0f), cw,
                            px(15.0f));
            }
        } else if (ed.state == StepState::forceOff) {
            canvas.setColor(0x38ffffff);
            canvas.text("\u00d7", font(context_, 11.0f, Weight::semibold), visage::Font::kCenter, cx, cy, cw, ch);
        }
    }

    // Draggable Polymeter Slicer Length Handle
    const float hx = laneEndHandleX(slLen, lay);
    canvas.setColor(colour::withAlpha(colour::cyan, hoverSlicerLen_ ? 1.0f : 0.72f));
    canvas.roundedRectangle(hx - px(2.5f), gr.y + px(2.0f), px(5.0f), gr.h - px(4.0f), px(2.5f));
}

void GridStagePanel::draw(visage::Canvas& canvas) {
    drawChrome(canvas);
    const auto& vis = context_.model.visuals();
    const auto mode =
        static_cast<EngineMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f), 0, 1));
    const Layout lay = computeLayout(width(), height());

    drawHeader(canvas, lay, vis, mode);
    if (mode == EngineMode::drum)
        drawDrumGrid(canvas, lay, vis);
    else
        drawSlicerStage(canvas, lay, vis);

    const char* footer =
        mode == EngineMode::drum
            ? "Click/drag: paint hits \u00b7 Drag Y on hit: velocity \u00b7 Right-click: ratchet/flam/Euclidean \u00b7 Drag right edge: per-lane polymeter (1\u201332)"
            : "Slicer Mode: 100% pitch-transparent rhythmic slicing of your Live 12 chords, basslines & clips \u00b7 Drag right edge for polymetric gate length";
    drawLabel(canvas, context_, footer, lay.sidebarX, lay.footerY, width() - 2.0f * lay.sidebarX, px(16.0f),
              colour::textFaint, visage::Font::kLeft, 9.5f, Weight::regular);
}

std::unique_ptr<Panel> makeGridStagePanel(Context& context) { return std::make_unique<GridStagePanel>(context); }

} // namespace cl::ui
