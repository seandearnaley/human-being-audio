// SPDX-License-Identifier: AGPL-3.0-or-later
#include "phrase_stage.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace cl::ui {

unsigned roleColour(Role role) {
    switch (role) {
    case Role::statement:
        return colour::iris;
    case Role::answer:
        return colour::cyan;
    case Role::mutation:
        return colour::amber;
    case Role::ratchet:
        return colour::vermilion;
    case Role::turnaround:
        return colour::emerald;
    case Role::rest:
        return colour::textFaint;
    }
    return colour::iris;
}

const char* roleName(Role role) {
    switch (role) {
    case Role::statement:
        return "STATEMENT";
    case Role::answer:
        return "ANSWER";
    case Role::mutation:
        return "MUTATION";
    case Role::ratchet:
        return "RATCHET";
    case Role::turnaround:
        return "TURN";
    case Role::rest:
        return "REST";
    }
    return "";
}

std::string spanDisplayLabel(Role role, const SpanOverride& sp) {
    std::string label = roleName(role);
    if (sp.octaveShift > 0)
        label += " +" + std::to_string(static_cast<int>(sp.octaveShift)) + "o";
    else if (sp.octaveShift < 0)
        label += " " + std::to_string(static_cast<int>(sp.octaveShift)) + "o";
    if (sp.invert)
        label += " INV";
    if (sp.timeScale < 0)
        label += " \u00bdx";
    else if (sp.timeScale > 0)
        label += " 2x";
    return label;
}

bool isEditableNote(const CompiledNote& n) noexcept { return n.key.valid() || n.slotIndex >= 0; }

NoteOverride* ensureNoteOverride(PhraseOverrides& ov, NoteKey key, int slotIndex) noexcept {
    if (key.valid()) {
        if (const auto* legacy = ov.findNote(slotIndex)) {
            if (ov.findNote(key) == nullptr) {
                const NoteOverride copy = *legacy;
                ov.clearNote(slotIndex);
                if (auto* created = ov.findOrCreateNote(key)) {
                    *created = copy;
                    created->key = key;
                    created->slotIndex = -1;
                    return created;
                }
            }
        }
        return ov.findOrCreateNote(key);
    }
    return ov.findOrCreateNote(slotIndex);
}

void pruneNoteOverride(PhraseOverrides& ov, NoteKey key, int slotIndex, const NoteOverride* no) noexcept {
    if (no == nullptr || no->active())
        return;
    if (key.valid())
        ov.clearNote(key);
    else if (slotIndex >= 0)
        ov.clearNote(slotIndex);
}

PhraseStage::PhraseStage(Context& context) : context_(context) { context_.live.push_back(this); }
PhraseStage::~PhraseStage() { std::erase(context_.live, static_cast<Live*>(this)); }

void PhraseStage::tick() {
    const auto mask = context_.model.visuals().activityMask;
    for (std::size_t i = 0; i < activityHold_.size(); ++i) {
        if ((mask & (1u << static_cast<unsigned>(i))) != 0u)
            activityHold_[i] = 1.0f;
        else
            activityHold_[i] = std::max(0.0f, activityHold_[i] - 0.08f);
    }
    redraw();
}

void PhraseStage::draw(visage::Canvas& canvas) {
    const auto& v = context_.model.visuals();
    const auto& plan = v.plan;
    const auto overrides = context_.model.overrides();
    const auto px = [this](float d) { return context_.px(d); };

    const float w = width();
    const float h = height();
    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::window, metric::radiusSmall);

    const auto mode =
        static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
    const int root = std::clamp(static_cast<int>(context_.model.get(ParamId::rootNote) + 0.5f), 0, 11);
    const auto scale =
        static_cast<Scale>(std::clamp(static_cast<int>(context_.model.get(ParamId::scale) + 0.5f), 0, 5));
    const double gravity = static_cast<double>(std::clamp(context_.model.get(ParamId::gravity), -1.0f, 1.0f));
    const auto attractor =
        static_cast<Attractor>(std::clamp(static_cast<int>(context_.model.get(ParamId::attractor) + 0.5f), 0, 3));

    const Layout lay = computeLayout(plan, mode, w, h);
    const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };

    // 0. Bar ruler + beat grid lines
    const int totalBeatCount = static_cast<int>(std::lround(lay.totalBeats));
    const int barCount = std::clamp(totalBeatCount / 4, 1, PhraseOverrides::kMaxChordBars);
    for (int b = 0; b <= totalBeatCount; ++b) {
        const float x = xOf(static_cast<double>(b));
        const bool isBar = (b % 4) == 0;
        canvas.setColor(isBar ? colour::track : colour::withAlpha(colour::edge, 0.50f));
        canvas.fill(x, lay.tapeY, px(isBar ? 1.2f : 0.8f), lay.velY + lay.velH - lay.tapeY);
        if (isBar && b < totalBeatCount) {
            char barLabel[16]{};
            std::snprintf(barLabel, sizeof(barLabel), "BAR %d", b / 4 + 1);
            drawLabel(canvas, context_, barLabel, x + px(4.0f), lay.rulerY, px(54.0f), px(15.0f), colour::textDim,
                      visage::Font::kLeft, type::small, Weight::semibold);
        }
    }

    // Left track headers
    drawLabel(canvas, context_, "PHRASE", px(6.0f), lay.tapeY, lay.padL - px(12.0f), lay.tapeH, colour::textFaint,
              visage::Font::kRight, type::small, Weight::semibold);
    if (lay.chordH > 0.0f) {
        drawLabel(canvas, context_, "CHORD", px(6.0f), lay.chordY, lay.padL - px(12.0f), lay.chordH, colour::textFaint,
                  visage::Font::kRight, type::small, Weight::semibold);
    } else {
        drawLabel(canvas, context_, "KIT", px(6.0f), lay.rulerY, lay.padL - px(12.0f), px(15.0f), colour::textFaint,
                  visage::Font::kRight, type::small, Weight::semibold);
    }
    drawLabel(canvas, context_, "VEL", px(6.0f), lay.velY, lay.padL - px(12.0f), lay.velH, colour::textFaint,
              visage::Font::kRight, type::small, Weight::semibold);

    // 1. Interactive Phrase Grammar Tape
    for (int i = 0; i < plan.spanCount; ++i) {
        const auto& span = plan.spans[static_cast<std::size_t>(i)];
        const SpanOverride spOv =
            i < PhraseOverrides::kMaxSpanOverrides ? overrides.spans[static_cast<std::size_t>(i)] : SpanOverride{};
        const float x1 = xOf(span.startBeats) + px(1.5f);
        const float x2 = xOf(span.endBeats) - px(1.5f);
        const float bw = std::max(px(4.0f), x2 - x1);
        const unsigned col = roleColour(span.role);
        const bool isHovered = (hoveredSpan_ == i);
        const bool isRest = (span.role == Role::rest);

        canvas.setColor(colour::withAlpha(col, isRest ? 0.08f : (span.locked ? 0.28f : (isHovered ? 0.24f : 0.16f))));
        canvas.roundedRectangle(x1, lay.tapeY, bw, lay.tapeH, px(4.0f));
        canvas.setColor(span.locked ? colour::text : (isHovered ? colour::text : col));
        canvas.roundedRectangleBorder(x1, lay.tapeY, bw, lay.tapeH, px(4.0f),
                                      px(span.locked ? 1.5f : (isHovered ? 1.3f : 1.1f)));

        if (bw > px(42.0f)) {
            const float textRightPad = span.locked ? px(24.0f) : px(8.0f);
            const std::string tag = spanDisplayLabel(span.role, spOv);
            drawLabel(canvas, context_, tag, x1 + px(6.0f), lay.tapeY, std::max(px(12.0f), bw - textRightPad),
                      lay.tapeH, isRest ? colour::textFaint : colour::text, visage::Font::kLeft, type::small,
                      Weight::semibold);
        }
        // Crisp lock indicator badge on pinned spans
        if (span.locked && bw > px(26.0f)) {
            const float bx = x2 - px(16.0f);
            const float by = lay.tapeY + (lay.tapeH - px(10.0f)) * 0.5f;
            canvas.setColor(col);
            canvas.roundedRectangle(bx, by + px(3.5f), px(9.0f), px(6.5f), px(1.8f));
            canvas.roundedRectangleBorder(bx + px(2.0f), by, px(5.0f), px(5.0f), px(2.0f), px(1.2f));
        }
    }

    // Draggable fill & turnaround divider handles on the tape
    const auto drawDividerHandle = [&](float x, bool hot) {
        canvas.setColor(hot ? colour::text : colour::withAlpha(colour::textDim, 0.7f));
        for (int d = -1; d <= 1; ++d)
            canvas.circle(x - px(1.5f), lay.tapeY + lay.tapeH * 0.5f + static_cast<float>(d) * px(4.0f) - px(1.5f),
                          px(3.0f));
    };
    if (lay.fillDivX > 0.0f)
        drawDividerHandle(lay.fillDivX, dragMode_ == DragMode::fillDivider || hoveredDivider_ == 1);
    if (lay.turnDivX > 0.0f)
        drawDividerHandle(lay.turnDivX, dragMode_ == DragMode::turnDivider || hoveredDivider_ == 2);

    // 1b. Per-Bar Scale-Locked Chord Progression Strip (melodic modes)
    if (lay.chordH > 0.0f) {
        for (int b = 0; b < barCount; ++b) {
            const float x1 = xOf(static_cast<double>(b) * 4.0) + px(1.5f);
            const float x2 = xOf(static_cast<double>(b + 1) * 4.0) - px(1.5f);
            const float bw = std::max(px(16.0f), x2 - x1);
            const int degOff = static_cast<int>(overrides.barChordOffsets[static_cast<std::size_t>(b)]);
            const bool shifted = (degOff != 0);
            const bool hot = (hoveredChordBar_ == b || dragChordBar_ == b);
            const unsigned accent = shifted ? colour::cyan : colour::edge;

            canvas.setColor(shifted ? colour::withAlpha(colour::cyan, hot ? 0.28f : 0.17f)
                                    : colour::withAlpha(colour::panel, hot ? 0.95f : 0.72f));
            canvas.roundedRectangle(x1, lay.chordY, bw, lay.chordH, px(3.5f));
            canvas.setColor(hot ? colour::text : accent);
            canvas.roundedRectangleBorder(x1, lay.chordY, bw, lay.chordH, px(3.5f),
                                          px(hot ? 1.2f : (shifted ? 1.0f : 0.8f)));

            drawLabel(canvas, context_, chordBarLabel(degOff, root, scale), x1 + px(4.0f), lay.chordY, bw - px(8.0f),
                      lay.chordH, shifted || hot ? colour::text : colour::textDim, visage::Font::kCenter, type::small,
                      shifted ? Weight::semibold : Weight::medium);
        }
    }

    // 2. Piano Roll / Drum Grid container
    canvas.setColor(colour::withAlpha(colour::panel, 0.78f));
    canvas.roundedRectangle(lay.padL, lay.rollY, lay.plotW, lay.rollH, px(4.0f));
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(lay.padL, lay.rollY, lay.plotW, lay.rollH, px(4.0f), px(1.0f));

    // Pitch range (computed consistently for rendering & hit-testing)
    int minNote = 36;
    int maxNote = 66;
    computePitchRange(plan, mode, minNote, maxNote);

    // Root-aware scale shading & octave guides
    if (mode == OutputMode::drum) {
        const float rowH = lay.rollH / 6.0f;
        for (int r = 0; r < 6; ++r) {
            const float ry = lay.rollY + static_cast<float>(5 - r) * rowH;
            if ((r % 2) == 0) {
                canvas.setColor(colour::withAlpha(colour::text, 0.02f));
                canvas.fill(lay.padL + px(1.0f), ry, lay.plotW - px(2.0f), rowH);
            }
            canvas.setColor(colour::withAlpha(colour::edge, 0.7f));
            canvas.fill(lay.padL, ry, lay.plotW, px(1.0f));
            drawLabel(canvas, context_, kDrumLaneLabels[static_cast<std::size_t>(r)], px(4.0f), ry,
                      lay.padL - px(10.0f), rowH, colour::textDim, visage::Font::kRight, type::small, Weight::medium);
        }
    } else {
        const int noteSpan = std::max(1, maxNote - minNote);
        const float semiH = (lay.rollH - px(16.0f)) / static_cast<float>(noteSpan);
        for (int m = minNote; m <= maxNote; ++m) {
            const float norm = static_cast<float>(m - minNote) / static_cast<float>(noteSpan);
            const float y = lay.rollY + lay.rollH - norm * (lay.rollH - px(16.0f)) - px(8.0f);
            const bool isRoot = ((m - root) % 12 + 12) % 12 == 0;
            const bool inScale = isNoteInScale(m, root, scale);

            if (isRoot) {
                canvas.setColor(colour::withAlpha(colour::cyan, 0.07f));
                canvas.fill(lay.padL + px(1.0f), y - semiH * 0.5f, lay.plotW - px(2.0f), semiH);
                canvas.setColor(colour::withAlpha(colour::cyan, 0.28f));
                canvas.fill(lay.padL, y, lay.plotW, px(1.0f));
                drawLabel(canvas, context_, midiNoteLabel(m), px(4.0f), y - px(8.0f), lay.padL - px(10.0f), px(16.0f),
                          colour::cyan, visage::Font::kRight, type::small, Weight::medium);
            } else if (inScale) {
                canvas.setColor(colour::withAlpha(colour::text, 0.022f));
                canvas.fill(lay.padL + px(1.0f), y - semiH * 0.48f, lay.plotW - px(2.0f), semiH * 0.96f);
            }
        }
    }

    // Visible Gravitational Warp Field (curves vertical beat grid lines toward active Pocket!)
    if (std::abs(gravity) > 0.02) {
        const int subSteps = totalBeatCount * 2; // 1/8-note field lines
        const float alpha = static_cast<float>(0.10 + 0.24 * std::abs(gravity));
        for (int k = 1; k < subSteps; ++k) {
            const double b = static_cast<double>(k) * 0.5;
            const double wb = warpBeat(b, gravity, attractor);
            const float xg = xOf(b);
            const float xw = xOf(wb);
            if (std::abs(xw - xg) < px(0.8f))
                continue;
            visage::Path fieldLine;
            fieldLine.moveTo(xg, lay.rollY + px(2.0f));
            fieldLine.lineTo(xw, lay.rollY + px(18.0f));
            fieldLine.lineTo(xw, lay.rollY + lay.rollH - px(18.0f));
            fieldLine.lineTo(xg, lay.rollY + lay.rollH - px(2.0f));
            canvas.setColor(colour::withAlpha(gravity > 0.0 ? colour::vermilion : colour::cyan, alpha));
            canvas.stroke(fieldLine, 0.0f, 0.0f, w, h, px(1.0f));
        }
    }

    // 3. Velocity Stem Lane container (interactive brushable lane!)
    const bool velHot = (dragMode_ == DragMode::velBrush || hoveredVelLane_);
    canvas.setColor(colour::withAlpha(colour::panel, velHot ? 0.88f : 0.72f));
    canvas.roundedRectangle(lay.padL, lay.velY, lay.plotW, lay.velH, px(4.0f));
    canvas.setColor(velHot ? colour::withAlpha(colour::cyan, 0.55f) : colour::edge);
    canvas.roundedRectangleBorder(lay.padL, lay.velY, lay.plotW, lay.velH, px(4.0f), px(1.0f));
    // Subtle 50% velocity guide line inside VEL lane
    canvas.setColor(colour::withAlpha(colour::edge, 0.45f));
    canvas.fill(lay.padL + px(2.0f), lay.velY + lay.velH * 0.5f, lay.plotW - px(4.0f), px(0.8f));

    // 4. Draw compiled notes + microtiming displacement stems + pitch bend curves + velocity stems
    for (int i = 0; i < plan.noteCount; ++i) {
        const auto& n = plan.notes[static_cast<std::size_t>(i)];
        const NoteBox box = noteBoxOf(n, lay, mode, minNote, maxNote);
        const float xGrid = xOf(n.startBeats);
        const unsigned col = roleColour(n.role);
        const float velNorm = static_cast<float>(n.velocity) / 127.0f;
        const bool isHovered = (hoveredNote_ == i || dragNoteIdx_ == i || hoveredVelNote_ == i);
        const bool hasEdit = overrides.findNoteFor(n.key, n.slotIndex) != nullptr;
        const bool edgeHot =
            (hoveredNote_ == i && hoveredNoteEdge_) || (dragMode_ == DragMode::noteLength && dragNoteIdx_ == i);

        // Grid tick & microtiming shift vector stem
        if (!n.muted && std::abs(box.x - xGrid) > px(1.0f)) {
            canvas.setColor(colour::textFaint);
            canvas.fill(xGrid, box.y - px(2.5f), px(1.0f), box.h + px(5.0f));
            const float stemX = std::min(xGrid, box.x);
            const float stemW = std::abs(box.x - xGrid);
            canvas.setColor(colour::vermilion);
            canvas.fill(stemX, box.y + box.h * 0.5f - px(1.0f), stemW, px(2.0f));
        }

        // Highlight currently sounding MIDI notes
        const bool sounding =
            !n.muted &&
            ((n.noteNumber < 64)
                 ? ((v.activeNotesLow & (std::uint64_t{1} << static_cast<unsigned>(n.noteNumber))) != 0ULL)
                 : ((v.activeNotesHigh & (std::uint64_t{1} << static_cast<unsigned>(n.noteNumber - 64))) != 0ULL));

        if (sounding) {
            canvas.setColor(colour::withAlpha(col, 0.24f));
            canvas.roundedRectangle(box.x - px(2.5f), box.y - px(2.5f), box.w + px(5.0f), box.h + px(5.0f), px(5.0f));
        }

        if (n.muted) {
            canvas.setColor(colour::withAlpha(col, 0.10f));
            canvas.roundedRectangle(box.x, box.y, box.w, box.h, px(3.0f));
            canvas.setColor(colour::withAlpha(isHovered ? colour::text : col, 0.45f));
            canvas.roundedRectangleBorder(box.x, box.y, box.w, box.h, px(3.0f), px(1.0f));
        } else {
            canvas.setColor(colour::withAlpha(col, sounding ? 0.94f : (isHovered ? 0.85f : (0.30f + 0.52f * velNorm))));
            canvas.roundedRectangle(box.x, box.y, box.w, box.h, px(3.0f));
            canvas.setColor(sounding || isHovered ? colour::text : (n.isRatchet ? colour::vermilion : col));
            canvas.roundedRectangleBorder(box.x, box.y, box.w, box.h, px(3.0f),
                                          px(sounding || isHovered ? 1.6f : 1.0f));
        }

        // Right-edge note-length resize bar when hovering edge or dragging length
        if (edgeHot && !n.muted) {
            canvas.setColor(colour::text);
            canvas.roundedRectangle(box.x + box.w - px(2.8f), box.y + px(1.0f), px(2.6f),
                                    std::max(px(4.0f), box.h - px(2.0f)), px(1.0f));
        }

        // Subtle pip on user-edited notes (offset left of edge handle)
        if (hasEdit && !n.muted) {
            canvas.setColor(colour::text);
            canvas.circle(box.x + box.w - px(5.2f), box.y + px(1.5f), px(2.8f));
        }

        // Per-step ratchet burst badge on parent note (×2 / ×3 / ×4 / ×6)
        if (!n.muted && n.ratchetCount >= 2) {
            char rBuf[8]{};
            std::snprintf(rBuf, sizeof(rBuf), "\u00d7%d", static_cast<int>(n.ratchetCount));
            const float badgeW = px(18.0f);
            const float badgeH = px(11.0f);
            const float badgeX = std::clamp(box.x, lay.padL + px(2.0f), lay.padL + lay.plotW - badgeW - px(2.0f));
            const float badgeY = std::max(lay.rollY + px(2.0f), box.y - badgeH - px(2.0f));
            canvas.setColor(colour::withAlpha(colour::vermilion, 0.92f));
            canvas.roundedRectangle(badgeX, badgeY, badgeW, badgeH, px(2.5f));
            drawLabel(canvas, context_, rBuf, badgeX, badgeY, badgeW, badgeH, colour::text, visage::Font::kCenter, 8.0f,
                      Weight::semibold);
        }

        if (!n.muted && mode == OutputMode::mpe && box.w > px(24.0f)) {
            char chBuf[8]{};
            std::snprintf(chBuf, sizeof(chBuf), "C%d", static_cast<int>(n.channel));
            drawLabel(canvas, context_, chBuf, box.x + px(3.0f), box.y, box.w - px(4.0f), box.h, colour::text,
                      visage::Font::kLeft, type::small, Weight::semibold);
        }

        // Pitch bend curve ribbon
        if (!n.muted && mode != OutputMode::drum && std::abs(n.bendSemitones) > 0.25f) {
            const float dy = -std::clamp(n.bendSemitones * px(3.0f), -px(36.0f), px(36.0f));
            const float y0 = box.y + box.h * 0.5f;
            const float y1 = std::clamp(y0 + dy, lay.rollY + px(5.0f), lay.rollY + lay.rollH - px(5.0f));
            visage::Path bend;
            bend.moveTo(box.x + px(2.0f), y0);
            for (int s = 1; s <= 8; ++s) {
                const float frac = static_cast<float>(s) / 8.0f;
                bend.lineTo(box.x + box.w * frac, y0 + (y1 - y0) * (frac * frac));
            }
            canvas.setColor(colour::withAlpha(colour::text, 0.85f));
            canvas.stroke(bend, 0.0f, 0.0f, w, h, px(1.4f));
        }

        // Velocity stem in bottom VEL lane
        const float stemH = std::max(px(3.0f), (lay.velH - px(8.0f)) * velNorm);
        const float stemY = lay.velY + lay.velH - px(3.0f) - stemH;
        const float stemX = box.x + px(2.0f);
        canvas.setColor(colour::withAlpha(col, n.muted ? 0.22f : (isHovered ? 0.98f : 0.72f)));
        canvas.fill(stemX, stemY, px(isHovered ? 2.4f : 1.8f), stemH);
        if (n.muted) {
            canvas.roundedRectangleBorder(stemX - px(1.2f), stemY - px(1.8f), px(4.2f), px(4.2f), px(2.1f), px(1.0f));
        } else {
            const float head = px(isHovered ? 5.2f : 4.2f);
            canvas.circle(stemX - (head - px(1.8f)) * 0.5f, stemY - head * 0.45f, head);
        }
    }

    // 5. Live playhead cursor sweeping Tape -> Chords -> Roll -> Velocity lane
    if (v.playing || v.playheadBeats > 0.0) {
        const float pxHead = xOf(std::clamp(v.playheadBeats, 0.0, lay.totalBeats));
        canvas.setColor(colour::withAlpha(colour::vermilion, 0.16f));
        canvas.fill(pxHead - px(8.0f), lay.rollY, px(8.0f), lay.rollH);
        canvas.setColor(colour::vermilion);
        canvas.fill(pxHead - px(1.0f), lay.tapeY, px(2.0f), lay.velY + lay.velH - lay.tapeY);
    }

    // 6. Floating hover Micro-HUD pill over hovered/dragged note
    const int hudIdx = dragNoteIdx_ >= 0 ? dragNoteIdx_ : (hoveredNote_ >= 0 ? hoveredNote_ : hoveredVelNote_);
    if (hudIdx >= 0 && hudIdx < plan.noteCount) {
        const auto& n = plan.notes[static_cast<std::size_t>(hudIdx)];
        const NoteBox box = noteBoxOf(n, lay, mode, minNote, maxNote);
        const double safeBpm = std::clamp(v.bpm, 20.0, 400.0);
        const double shiftMs = (n.warpedBeats - n.startBeats) * (60000.0 / safeBpm);
        const double dur16 = n.durationBeats * 4.0;
        const std::string pitchStr =
            mode == OutputMode::drum
                ? std::string(kDrumLaneLabels[static_cast<std::size_t>(std::clamp<int>(n.drumRow, 0, 5))])
                : midiNoteLabel(n.noteNumber);
        char hudText[128]{};
        if (n.muted) {
            std::snprintf(hudText, sizeof(hudText), "%s  \u00b7  MUTED  (Alt-click to unmute)", pitchStr.c_str());
        } else if (dragMode_ == DragMode::noteLength) {
            std::snprintf(hudText, sizeof(hudText), "%s  \u00b7  len %.1f/16  \u00b7  %.2f beats", pitchStr.c_str(),
                          dur16, n.durationBeats);
        } else if (dragMode_ == DragMode::noteNudge) {
            std::snprintf(hudText, sizeof(hudText), "%s  \u00b7  nudge %+.1f ms  \u00b7  %.2f b", pitchStr.c_str(),
                          shiftMs, n.warpedBeats);
        } else if (n.ratchetCount >= 2) {
            std::snprintf(hudText, sizeof(hudText), "%s  \u00b7  \u00d7%d ratchet  \u00b7  %+.1f ms  \u00b7  vel %d",
                          pitchStr.c_str(), static_cast<int>(n.ratchetCount), shiftMs, static_cast<int>(n.velocity));
        } else if (mode != OutputMode::drum && std::abs(n.bendSemitones) > 0.25f) {
            std::snprintf(hudText, sizeof(hudText),
                          "%s  \u00b7  %+.1f ms  \u00b7  vel %d  \u00b7  %+.1f st  \u00b7  Ch %d", pitchStr.c_str(),
                          shiftMs, static_cast<int>(n.velocity), static_cast<double>(n.bendSemitones),
                          static_cast<int>(n.channel));
        } else {
            std::snprintf(hudText, sizeof(hudText), "%s  \u00b7  %+.1f ms  \u00b7  vel %d  \u00b7  Ch %d",
                          pitchStr.c_str(), shiftMs, static_cast<int>(n.velocity), static_cast<int>(n.channel));
        }
        const float hudW = px(244.0f);
        const float hudH = px(20.0f);
        const float hudX = std::clamp(box.x, lay.padL + px(4.0f), lay.padL + lay.plotW - hudW - px(4.0f));
        const float hudY =
            (box.y - hudH - px(6.0f) > lay.rollY + px(2.0f)) ? (box.y - hudH - px(6.0f)) : (box.y + box.h + px(6.0f));
        canvas.setColor(colour::withAlpha(colour::window, 0.94f));
        canvas.roundedRectangle(hudX, hudY, hudW, hudH, px(4.0f));
        canvas.setColor(roleColour(n.role));
        canvas.roundedRectangleBorder(hudX, hudY, hudW, hudH, px(4.0f), px(1.0f));
        drawLabel(canvas, context_, hudText, hudX + px(6.0f), hudY, hudW - px(12.0f), hudH, colour::text,
                  visage::Font::kCenter, type::small, Weight::medium);
    }

    // 7. Musical Footer + Reset Edits Pill + Live MIDI Activity LEDs
    int activeCount = 0;
    for (int i = 0; i < plan.noteCount; ++i) {
        if (!plan.notes[static_cast<std::size_t>(i)].muted)
            ++activeCount;
    }
    const char* modeLabel =
        mode == OutputMode::mono ? "MONO CH1" : (mode == OutputMode::mpe ? "MPE CH2-15" : "DRUM KIT");
    const auto scaleIdx = static_cast<std::size_t>(std::clamp(static_cast<int>(scale), 0, 5));
    const bool latchActive =
        context_.model.get(ParamId::latch) >= 0.5f &&
        static_cast<int>(context_.model.get(ParamId::inputMode) + 0.5f) != static_cast<int>(InputMode::seed);

    char footer[128]{};
    if (mode == OutputMode::drum) {
        const auto dsIdx =
            static_cast<std::size_t>(std::clamp(static_cast<int>(context_.model.get(ParamId::drumStyle) + 0.5f), 0, 5));
        std::snprintf(footer, sizeof(footer), "%.0f BPM  \u00b7  %s Kit  \u00b7  %d hits  \u00b7  %s",
                      std::clamp(v.bpm, 20.0, 400.0), std::string(choices::drumStyle[dsIdx]).c_str(), activeCount,
                      modeLabel);
    } else {
        std::snprintf(footer, sizeof(footer), "%.0f BPM  \u00b7  %s %s  \u00b7  %d notes  \u00b7  %s%s",
                      std::clamp(v.bpm, 20.0, 400.0), kNoteNames[static_cast<std::size_t>(root)],
                      std::string(choices::scale[scaleIdx]).c_str(), activeCount, modeLabel,
                      latchActive ? "  \u00b7  LATCH" : "");
    }
    drawLabel(canvas, context_, footer, lay.padL, lay.footerY, lay.plotW - px(270.0f), px(18.0f), colour::textDim,
              visage::Font::kLeft, type::small, Weight::medium);

    // Live pulsing MIDI stream LEDs (NOTE, PB, CC74, CC1, AT)
    constexpr std::array<const char*, 5> kLedNames{"NOTE", "PB", "CC74", "CC1", "AT"};
    constexpr std::array<unsigned, 5> kLedCols{colour::emerald, colour::cyan, colour::iris, colour::amber,
                                               colour::vermilion};
    const float ledW = px(38.0f);
    const float ledH = px(15.0f);
    const float ledGap = px(4.0f);
    float ledX = lay.padL + lay.plotW - 5.0f * ledW - 4.0f * ledGap;
    const float ledY = lay.footerY + px(1.5f);
    for (std::size_t i = 0; i < 5; ++i) {
        const float lit = activityHold_[i];
        canvas.setColor(
            colour::withAlpha(lit > 0.05f ? kLedCols[i] : colour::raised, lit > 0.05f ? (0.14f + 0.24f * lit) : 1.0f));
        canvas.roundedRectangle(ledX, ledY, ledW, ledH, px(3.0f));
        canvas.setColor(lit > 0.05f ? colour::withAlpha(kLedCols[i], 0.45f + 0.55f * lit) : colour::edge);
        canvas.roundedRectangleBorder(ledX, ledY, ledW, ledH, px(3.0f), px(0.9f));
        drawLabel(canvas, context_, kLedNames[i], ledX, ledY, ledW, ledH,
                  lit > 0.15f ? colour::text : colour::textFaint, visage::Font::kCenter, 9.0f, Weight::semibold);
        ledX += ledW + ledGap;
    }

    // Compact "Reset edits" pill when overrides are active
    resetBtnX_ = -1.0f;
    if (overrides.hasAny()) {
        const float rw = px(68.0f);
        const float rx = lay.padL + lay.plotW - 5.0f * ledW - 4.0f * ledGap - rw - px(10.0f);
        resetBtnX_ = rx;
        resetBtnY_ = ledY;
        resetBtnW_ = rw;
        resetBtnH_ = ledH;
        canvas.setColor(colour::withAlpha(colour::amber, hoveredReset_ ? 0.26f : 0.14f));
        canvas.roundedRectangle(rx, ledY, rw, ledH, px(3.0f));
        canvas.setColor(hoveredReset_ ? colour::text : colour::amber);
        canvas.roundedRectangleBorder(rx, ledY, rw, ledH, px(3.0f), px(0.9f));
        drawLabel(canvas, context_, "Reset edits", rx, ledY, rw, ledH, hoveredReset_ ? colour::text : colour::amber,
                  visage::Font::kCenter, 9.0f, Weight::semibold);
    }
}

} // namespace cl::ui
