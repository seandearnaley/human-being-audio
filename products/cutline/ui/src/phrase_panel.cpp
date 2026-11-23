// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 1.0 central hero stage: interactive Phrase Grammar Tape (lock/pin
// spans, draggable fill/turn dividers, span ops: roll/copy/octave/invert/time/role)
// + Per-Bar Scale-Locked Chord Progression Strip + Tactile Piano Roll 2.0
// (root-aware scale grid, visible curved gravity field, vertical pitch drag,
// right-edge note-length drag, Shift+drag micro-nudge, per-step ratchets, hover
// micro-HUD) + Direct-Draw Velocity Stem Brush Lane + Live MIDI Activity LEDs
// + Rebalanced Left Rail with Seed History & Drum Kit Style selector.
#include "panels.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace cl::ui {

namespace {

constexpr std::array<const char*, 6> kDrumLabels{"KICK", "SNARE", "GHOST", "CH HAT", "OP HAT", "RATCH"};
constexpr std::array<const char*, 12> kNoteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct ScaleInfo {
    std::array<int, 12> intervals{};
    std::array<const char*, 12> numerals{};
    int count = 7;
};

constexpr std::array<ScaleInfo, 6> kScaleInfo{{
    // 0: Phrygian
    {{{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"i", "bII", "bIII", "iv", "v\u00b0", "bVI", "bVII"}, 7},
    // 1: Natural Minor
    {{{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"i", "ii\u00b0", "bIII", "iv", "v", "bVI", "bVII"}, 7},
    // 2: Phrygian Dominant
    {{{0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"I", "bII", "iii\u00b0", "iv", "v\u00b0", "bVI", "bvii"}, 7},
    // 3: Dorian
    {{{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}}, {"i", "ii", "bIII", "IV", "v", "vi\u00b0", "bVII"}, 7},
    // 4: Pentatonic Minor
    {{{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}}, {"i", "bIII", "iv", "v", "bVII"}, 5},
    // 5: Chromatic
    {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
     {"0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11"},
     12},
}};

std::string chordBarLabel(int degreeOffset, int root, Scale scale) {
    const auto& sc = kScaleInfo[static_cast<std::size_t>(std::clamp(static_cast<int>(scale), 0, 5))];
    int d = degreeOffset;
    while (d < 0)
        d += sc.count;
    while (d >= sc.count)
        d -= sc.count;
    const int semi = sc.intervals[static_cast<std::size_t>(d)];
    const int pc = ((root + semi) % 12 + 12) % 12;
    const char* arrow = (degreeOffset >= sc.count) ? "\u2191" : (degreeOffset <= -sc.count ? "\u2193" : "");
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%s%s  %s", sc.numerals[static_cast<std::size_t>(d)], arrow,
                  kNoteNames[static_cast<std::size_t>(pc)]);
    return buf;
}

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

std::string midiNoteLabel(int midiNote) {
    const int n = std::clamp(midiNote, 0, 127);
    const int pc = n % 12;
    const int oct = n / 12 - 1;
    char buf[12]{};
    std::snprintf(buf, sizeof(buf), "%s%d", kNoteNames[static_cast<std::size_t>(pc)], oct);
    return buf;
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

class PhraseStage final : public visage::Frame, public Live {
  public:
    explicit PhraseStage(Context& context) : context_(context) { context_.live.push_back(this); }
    ~PhraseStage() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto mask = context_.model.visuals().activityMask;
        for (std::size_t i = 0; i < activityHold_.size(); ++i) {
            if ((mask & (1u << static_cast<unsigned>(i))) != 0u)
                activityHold_[i] = 1.0f;
            else
                activityHold_[i] = std::max(0.0f, activityHold_[i] - 0.08f);
        }
        redraw();
    }

    void draw(visage::Canvas& canvas) override {
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
            drawLabel(canvas, context_, "CHORD", px(6.0f), lay.chordY, lay.padL - px(12.0f), lay.chordH,
                      colour::textFaint, visage::Font::kRight, type::small, Weight::semibold);
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

            canvas.setColor(
                colour::withAlpha(col, isRest ? 0.08f : (span.locked ? 0.28f : (isHovered ? 0.24f : 0.16f))));
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

                drawLabel(canvas, context_, chordBarLabel(degOff, root, scale), x1 + px(4.0f), lay.chordY,
                          bw - px(8.0f), lay.chordH, shifted || hot ? colour::text : colour::textDim,
                          visage::Font::kCenter, type::small, shifted ? Weight::semibold : Weight::medium);
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
                drawLabel(canvas, context_, kDrumLabels[static_cast<std::size_t>(r)], px(4.0f), ry,
                          lay.padL - px(10.0f), rowH, colour::textDim, visage::Font::kRight, type::small,
                          Weight::medium);
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
                    drawLabel(canvas, context_, midiNoteLabel(m), px(4.0f), y - px(8.0f), lay.padL - px(10.0f),
                              px(16.0f), colour::cyan, visage::Font::kRight, type::small, Weight::medium);
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
                canvas.roundedRectangle(box.x - px(2.5f), box.y - px(2.5f), box.w + px(5.0f), box.h + px(5.0f),
                                        px(5.0f));
            }

            if (n.muted) {
                canvas.setColor(colour::withAlpha(col, 0.10f));
                canvas.roundedRectangle(box.x, box.y, box.w, box.h, px(3.0f));
                canvas.setColor(colour::withAlpha(isHovered ? colour::text : col, 0.45f));
                canvas.roundedRectangleBorder(box.x, box.y, box.w, box.h, px(3.0f), px(1.0f));
            } else {
                canvas.setColor(
                    colour::withAlpha(col, sounding ? 0.94f : (isHovered ? 0.85f : (0.30f + 0.52f * velNorm))));
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
                drawLabel(canvas, context_, rBuf, badgeX, badgeY, badgeW, badgeH, colour::text, visage::Font::kCenter,
                          8.0f, Weight::semibold);
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
                canvas.roundedRectangleBorder(stemX - px(1.2f), stemY - px(1.8f), px(4.2f), px(4.2f), px(2.1f),
                                              px(1.0f));
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
                    ? std::string(kDrumLabels[static_cast<std::size_t>(std::clamp<int>(n.drumRow, 0, 5))])
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
                std::snprintf(hudText, sizeof(hudText),
                              "%s  \u00b7  \u00d7%d ratchet  \u00b7  %+.1f ms  \u00b7  vel %d", pitchStr.c_str(),
                              static_cast<int>(n.ratchetCount), shiftMs, static_cast<int>(n.velocity));
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
            const float hudY = (box.y - hudH - px(6.0f) > lay.rollY + px(2.0f)) ? (box.y - hudH - px(6.0f))
                                                                                : (box.y + box.h + px(6.0f));
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
            const auto dsIdx = static_cast<std::size_t>(
                std::clamp(static_cast<int>(context_.model.get(ParamId::drumStyle) + 0.5f), 0, 5));
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
            canvas.setColor(colour::withAlpha(lit > 0.05f ? kLedCols[i] : colour::raised,
                                              lit > 0.05f ? (0.14f + 0.24f * lit) : 1.0f));
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

    void mouseMove(const visage::MouseEvent& e) override {
        updateHover(e.position.x, e.position.y);
        redraw();
    }

    void mouseExit(const visage::MouseEvent&) override {
        if (dragMode_ == DragMode::none) {
            hoveredNote_ = -1;
            hoveredNoteEdge_ = false;
            hoveredSpan_ = -1;
            hoveredChordBar_ = -1;
            hoveredDivider_ = 0;
            hoveredVelLane_ = false;
            hoveredVelNote_ = -1;
            hoveredReset_ = false;
            redraw();
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const float mx = e.position.x;
        const float my = e.position.y;
        updateHover(mx, my);

        // Reset edits pill
        if (hoveredReset_) {
            context_.model.clearOverrides();
            hoveredReset_ = false;
            redraw();
            return;
        }

        const auto& v = context_.model.visuals();
        const auto& plan = v.plan;
        const auto mode =
            static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
        const Layout lay = computeLayout(plan, mode, width(), height());

        // 1. Tape divider drag
        if (!e.shouldTriggerPopup() && hoveredDivider_ != 0) {
            dragMode_ = hoveredDivider_ == 1 ? DragMode::fillDivider : DragMode::turnDivider;
            return;
        }

        // 2. Tape span click (lock/pin) or right-click span ops menu
        if (hoveredSpan_ >= 0 && hoveredSpan_ < plan.spanCount && hoveredSpan_ < PhraseOverrides::kMaxSpanOverrides) {
            if (e.shouldTriggerPopup()) {
                showSpanMenu(hoveredSpan_);
                return;
            }
            auto ov = context_.model.overrides();
            auto& sp = ov.spans[static_cast<std::size_t>(hoveredSpan_)];
            sp.locked = !sp.locked;
            if (sp.locked)
                sp.lockedSeed = static_cast<std::uint16_t>(
                    std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::seed))), 1, 999));
            context_.model.setOverrides(ov);
            redraw();
            return;
        }

        // 2b. Chord progression bar pill (drag vertically for scale degree, double/Alt reset, right-click progressions)
        if (hoveredChordBar_ >= 0 && hoveredChordBar_ < PhraseOverrides::kMaxChordBars) {
            if (e.shouldTriggerPopup()) {
                showChordMenu(hoveredChordBar_);
                return;
            }
            if (e.isAltDown() || e.repeatClickCount() >= 2) {
                auto ov = context_.model.overrides();
                ov.barChordOffsets[static_cast<std::size_t>(hoveredChordBar_)] = 0;
                context_.model.setOverrides(ov);
                redraw();
                return;
            }
            dragMode_ = DragMode::chordBar;
            dragChordBar_ = hoveredChordBar_;
            dragStartY_ = my;
            dragBaseChordDeg_ =
                static_cast<int>(context_.model.overrides().barChordOffsets[static_cast<std::size_t>(dragChordBar_)]);
            return;
        }

        // 3. Direct Velocity Stem Brush in VEL lane
        if (!e.shouldTriggerPopup() && hoveredVelLane_) {
            dragMode_ = DragMode::velBrush;
            velBrushLastX_ = mx;
            captureVelBrushSnapshot(plan, lay);
            applyVelBrush(mx, mx, my, lay);
            return;
        }

        // 4. Piano roll note interaction
        if (hoveredNote_ >= 0 && hoveredNote_ < plan.noteCount) {
            const auto& n = plan.notes[static_cast<std::size_t>(hoveredNote_)];
            if (!isEditableNote(n))
                return;
            if (e.shouldTriggerPopup()) {
                showNoteMenu(n.key, n.slotIndex, n.muted);
                return;
            }
            if (e.isAltDown() || e.repeatClickCount() >= 2) {
                auto ov = context_.model.overrides();
                if (auto* no = ensureNoteOverride(ov, n.key, n.slotIndex)) {
                    no->muted = !no->muted;
                    pruneNoteOverride(ov, n.key, n.slotIndex, no);
                    context_.model.setOverrides(ov);
                    redraw();
                }
                return;
            }
            const auto ov = context_.model.overrides();
            const auto* existing = ov.findNoteFor(n.key, n.slotIndex);
            dragNoteIdx_ = hoveredNote_;
            dragNoteKey_ = n.key;
            dragSlotIndex_ = n.slotIndex;
            dragStartX_ = mx;
            dragStartY_ = my;

            if (hoveredNoteEdge_ && !n.muted) {
                dragMode_ = DragMode::noteLength;
                dragBaseLength_ = existing != nullptr ? static_cast<int>(existing->lengthSixteenths) : 0;
                return;
            }
            if (e.isShiftDown() && !n.muted) {
                dragMode_ = DragMode::noteNudge;
                dragBaseNudge_ = existing != nullptr ? static_cast<int>(existing->microNudgeTicks) : 0;
                return;
            }
            if (mode != OutputMode::drum) {
                dragMode_ = DragMode::notePitch;
                dragBasePitchDelta_ = existing != nullptr ? static_cast<int>(existing->pitchDelta) : 0;
                dragOriginalMidi_ = static_cast<int>(n.noteNumber) - dragBasePitchDelta_;
            }
        }
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragMode_ == DragMode::none)
            return;
        const auto& v = context_.model.visuals();
        const auto mode =
            static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
        const Layout lay = computeLayout(v.plan, mode, width(), height());

        if (dragMode_ == DragMode::fillDivider || dragMode_ == DragMode::turnDivider) {
            const float frac = std::clamp((e.position.x - lay.padL) / std::max(1.0f, lay.plotW), 0.50f, 0.96f);
            auto ov = context_.model.overrides();
            if (dragMode_ == DragMode::fillDivider) {
                const float maxFill = ov.turnStartFrac >= 0.0f ? (ov.turnStartFrac - 0.04f) : 0.88f;
                ov.fillStartFrac = std::clamp(frac, 0.52f, maxFill);
            } else {
                const float minTurn = ov.fillStartFrac >= 0.0f ? (ov.fillStartFrac + 0.04f) : 0.74f;
                ov.turnStartFrac = std::clamp(frac, minTurn, 0.96f);
            }
            context_.model.setOverrides(ov);
            redraw();
        } else if (dragMode_ == DragMode::chordBar && dragChordBar_ >= 0 &&
                   dragChordBar_ < PhraseOverrides::kMaxChordBars) {
            const float pxPerDeg = std::max(context_.px(10.0f), 8.0f);
            const int deltaDeg = static_cast<int>(std::lround((dragStartY_ - e.position.y) / pxPerDeg));
            const int target = std::clamp(dragBaseChordDeg_ + deltaDeg, -7, 7);
            auto ov = context_.model.overrides();
            if (ov.barChordOffsets[static_cast<std::size_t>(dragChordBar_)] != static_cast<std::int8_t>(target)) {
                ov.barChordOffsets[static_cast<std::size_t>(dragChordBar_)] = static_cast<std::int8_t>(target);
                context_.model.setOverrides(ov);
                redraw();
            }
        } else if (dragMode_ == DragMode::velBrush) {
            applyVelBrush(velBrushLastX_, e.position.x, e.position.y, lay);
            velBrushLastX_ = e.position.x;
        } else if (dragMode_ == DragMode::noteLength && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
            const double pxPerSixteenth = std::max(4.0, static_cast<double>(lay.plotW) / (lay.totalBeats * 4.0));
            const int deltaSixteenths =
                static_cast<int>(std::lround(static_cast<double>(e.position.x - dragStartX_) / pxPerSixteenth));
            const int newLen = std::clamp(dragBaseLength_ + deltaSixteenths, -16, 32);
            auto ov = context_.model.overrides();
            if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
                if (no->lengthSixteenths != static_cast<std::int8_t>(newLen)) {
                    no->lengthSixteenths = static_cast<std::int8_t>(newLen);
                    pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
                    context_.model.setOverrides(ov);
                    redraw();
                }
            }
        } else if (dragMode_ == DragMode::noteNudge && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
            const double pxPerBeat = std::max(8.0, static_cast<double>(lay.plotW) / lay.totalBeats);
            const double deltaBeats = static_cast<double>(e.position.x - dragStartX_) / pxPerBeat;
            const int deltaTicks = static_cast<int>(std::lround(deltaBeats * 96.0));
            const int newNudge = std::clamp(dragBaseNudge_ + deltaTicks, -24, 24);
            auto ov = context_.model.overrides();
            if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
                if (no->microNudgeTicks != static_cast<std::int8_t>(newNudge)) {
                    no->microNudgeTicks = static_cast<std::int8_t>(newNudge);
                    pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
                    context_.model.setOverrides(ov);
                    redraw();
                }
            }
        } else if (dragMode_ == DragMode::notePitch && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
            const float pxPerSemi = std::max(context_.px(6.0f), lay.rollH / 28.0f);
            const int rawDelta = static_cast<int>(std::lround((dragStartY_ - e.position.y) / pxPerSemi));
            const int targetRawMidi = std::clamp(dragOriginalMidi_ + dragBasePitchDelta_ + rawDelta, 12, 120);
            int finalMidi = targetRawMidi;
            if (!e.isShiftDown() && !e.isMainModifier()) {
                const int root = std::clamp(static_cast<int>(context_.model.get(ParamId::rootNote) + 0.5f), 0, 11);
                const auto scale =
                    static_cast<Scale>(std::clamp(static_cast<int>(context_.model.get(ParamId::scale) + 0.5f), 0, 5));
                finalMidi = snapNoteToScale(targetRawMidi, root, scale);
            }
            const int newPitchDelta = std::clamp(finalMidi - dragOriginalMidi_, -24, 24);
            auto ov = context_.model.overrides();
            if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
                no->pitchDelta = static_cast<std::int8_t>(newPitchDelta);
                pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
                context_.model.setOverrides(ov);
                redraw();
            }
        }
    }

    void mouseUp(const visage::MouseEvent& e) override {
        dragMode_ = DragMode::none;
        dragNoteIdx_ = -1;
        dragNoteKey_ = {};
        dragSlotIndex_ = -1;
        dragChordBar_ = -1;
        velBrushCount_ = 0;
        updateHover(e.position.x, e.position.y);
        redraw();
    }

  private:
    enum class DragMode { none, fillDivider, turnDivider, chordBar, notePitch, noteLength, noteNudge, velBrush };

    struct Layout {
        float padL, padR, plotW;
        double totalBeats;
        float rulerY, tapeY, tapeH;
        float chordY, chordH;
        float rollY, rollH;
        float velY, velH;
        float footerY;
        float fillDivX, turnDivX;
    };

    struct NoteBox {
        float x, y, w, h;
    };

    struct VelBrushItem {
        NoteKey key{};
        int slotIndex = -1;
        int planIndex = -1;
        float stemX = 0.0f;
        int baseVel = 100;
    };

    Layout computeLayout(const PhrasePlan& plan, OutputMode mode, float w, float h) const {
        const auto px = [this](float d) { return context_.px(d); };
        Layout lay{};
        lay.padL = px(64.0f);
        lay.padR = px(12.0f);
        lay.plotW = std::max(1.0f, w - lay.padL - lay.padR);
        lay.totalBeats = std::max(4.0, plan.phraseBeats);
        lay.rulerY = px(4.0f);
        lay.tapeY = px(20.0f);
        lay.tapeH = px(22.0f);
        if (mode != OutputMode::drum) {
            lay.chordY = px(46.0f);
            lay.chordH = px(19.0f);
            lay.rollY = px(69.0f);
        } else {
            lay.chordY = 0.0f;
            lay.chordH = 0.0f;
            lay.rollY = px(46.0f);
        }
        lay.velH = px(36.0f);
        lay.footerY = h - px(20.0f);
        lay.velY = lay.footerY - lay.velH - px(5.0f);
        lay.rollH = std::max(px(80.0f), lay.velY - lay.rollY - px(5.0f));
        lay.fillDivX = -1.0f;
        lay.turnDivX = -1.0f;
        const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
        for (int i = 0; i < plan.spanCount; ++i) {
            const auto& sp = plan.spans[static_cast<std::size_t>(i)];
            if (sp.role == Role::ratchet && i > 0)
                lay.fillDivX = xOf(sp.startBeats);
            if (sp.role == Role::turnaround && i > 0)
                lay.turnDivX = xOf(sp.startBeats);
        }
        return lay;
    }

    static void computePitchRange(const PhrasePlan& plan, OutputMode mode, int& minNote, int& maxNote) {
        minNote = 36;
        maxNote = 66;
        if (mode != OutputMode::drum && plan.noteCount > 0) {
            minNote = 127;
            maxNote = 0;
            for (int i = 0; i < plan.noteCount; ++i) {
                const int n = plan.notes[static_cast<std::size_t>(i)].noteNumber;
                minNote = std::min(minNote, n);
                maxNote = std::max(maxNote, n);
            }
            minNote = std::max(12, minNote - 3);
            maxNote = std::min(120, std::max(minNote + 14, maxNote + 3));
        }
    }

    NoteBox noteBoxOf(const CompiledNote& n, const Layout& lay, OutputMode mode, int minNote, int maxNote) const {
        const auto px = [this](float d) { return context_.px(d); };
        const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
        const float xWarped = xOf(n.warpedBeats);
        const float xEnd = xOf(std::min(lay.totalBeats, n.warpedBeats + n.durationBeats));
        const float nw = std::max(px(7.0f), xEnd - xWarped);
        const float velNorm = static_cast<float>(n.velocity) / 127.0f;

        float ny = lay.rollY;
        float nh = px(12.0f);
        if (mode == OutputMode::drum) {
            const float rowH = lay.rollH / 6.0f;
            const int row = std::clamp(static_cast<int>(n.drumRow), 0, 5);
            nh = std::max(px(10.0f), rowH * (0.42f + 0.34f * velNorm));
            ny = lay.rollY + static_cast<float>(5 - row) * rowH + (rowH - nh) * 0.5f;
        } else {
            nh = px(8.5f + 6.5f * velNorm);
            const float norm =
                static_cast<float>(n.noteNumber - minNote) / static_cast<float>(std::max(1, maxNote - minNote));
            ny = lay.rollY + lay.rollH - norm * (lay.rollH - px(16.0f)) - px(8.0f) - nh * 0.5f;
        }
        return {xWarped, ny, nw, nh};
    }

    void captureVelBrushSnapshot(const PhrasePlan& plan, const Layout& lay) {
        const auto ov = context_.model.overrides();
        const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
        velBrushCount_ = 0;
        for (int i = 0; i < plan.noteCount && velBrushCount_ < kMaxPhraseNotes; ++i) {
            const auto& n = plan.notes[static_cast<std::size_t>(i)];
            if (!isEditableNote(n) || n.muted)
                continue;
            const auto* existing = ov.findNoteFor(n.key, n.slotIndex);
            const int curDelta = existing != nullptr ? static_cast<int>(existing->velocityDelta) : 0;
            auto& item = velBrushItems_[static_cast<std::size_t>(velBrushCount_++)];
            item.key = n.key;
            item.slotIndex = n.slotIndex;
            item.planIndex = i;
            item.stemX = xOf(n.warpedBeats) + context_.px(2.0f);
            item.baseVel = std::clamp(static_cast<int>(n.velocity) - curDelta, 1, 127);
        }
    }

    void applyVelBrush(float xA, float xB, float my, const Layout& lay) {
        const float top = lay.velY + context_.px(4.0f);
        const float bot = lay.velY + lay.velH - context_.px(3.0f);
        const float norm = std::clamp((bot - my) / std::max(1.0f, bot - top), 0.0f, 1.0f);
        const int targetVel = std::clamp(static_cast<int>(std::lround(16.0f + norm * 111.0f)), 16, 127);
        const float slop = context_.px(7.0f);
        const float loX = std::min(xA, xB) - slop;
        const float hiX = std::max(xA, xB) + slop;

        auto ov = context_.model.overrides();
        bool changed = false;
        int lastHitIdx = -1;
        for (int i = 0; i < velBrushCount_; ++i) {
            const auto& item = velBrushItems_[static_cast<std::size_t>(i)];
            if (item.stemX >= loX && item.stemX <= hiX) {
                if (auto* no = ensureNoteOverride(ov, item.key, item.slotIndex)) {
                    const auto nextDelta = static_cast<std::int8_t>(std::clamp(targetVel - item.baseVel, -64, 64));
                    if (no->velocityDelta != nextDelta) {
                        no->velocityDelta = nextDelta;
                        pruneNoteOverride(ov, item.key, item.slotIndex, no);
                        changed = true;
                    }
                    lastHitIdx = item.planIndex;
                }
            }
        }
        if (lastHitIdx >= 0)
            dragNoteIdx_ = lastHitIdx;
        if (changed) {
            context_.model.setOverrides(ov);
            redraw();
        }
    }

    void updateHover(float mx, float my) {
        hoveredReset_ = resetBtnX_ >= 0.0f && mx >= resetBtnX_ && mx <= resetBtnX_ + resetBtnW_ && my >= resetBtnY_ &&
                        my <= resetBtnY_ + resetBtnH_;
        hoveredSpan_ = -1;
        hoveredChordBar_ = -1;
        hoveredDivider_ = 0;
        hoveredNote_ = -1;
        hoveredNoteEdge_ = false;
        hoveredVelLane_ = false;
        hoveredVelNote_ = -1;

        const auto& v = context_.model.visuals();
        const auto& plan = v.plan;
        const auto mode =
            static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
        const Layout lay = computeLayout(plan, mode, width(), height());
        const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };

        if (my >= lay.tapeY && my <= lay.tapeY + lay.tapeH) {
            const float grab = context_.px(7.0f);
            if (lay.fillDivX > 0.0f && std::abs(mx - lay.fillDivX) <= grab) {
                hoveredDivider_ = 1;
                return;
            }
            if (lay.turnDivX > 0.0f && std::abs(mx - lay.turnDivX) <= grab) {
                hoveredDivider_ = 2;
                return;
            }
            for (int i = 0; i < plan.spanCount; ++i) {
                const auto& sp = plan.spans[static_cast<std::size_t>(i)];
                if (mx >= xOf(sp.startBeats) && mx <= xOf(sp.endBeats)) {
                    hoveredSpan_ = i;
                    return;
                }
            }
            return;
        }

        if (lay.chordH > 0.0f && my >= lay.chordY && my <= lay.chordY + lay.chordH && mx >= lay.padL &&
            mx <= lay.padL + lay.plotW) {
            const int barCount =
                std::clamp(static_cast<int>(std::lround(lay.totalBeats)) / 4, 1, PhraseOverrides::kMaxChordBars);
            const int bar = std::clamp(
                static_cast<int>(((mx - lay.padL) / std::max(1.0f, lay.plotW)) * static_cast<float>(barCount)), 0,
                barCount - 1);
            hoveredChordBar_ = bar;
            return;
        }

        if (my >= lay.rollY && my <= lay.rollY + lay.rollH) {
            int minNote = 36;
            int maxNote = 66;
            computePitchRange(plan, mode, minNote, maxNote);
            const float slop = context_.px(3.0f);
            const float edgeGrab = context_.px(6.0f);
            for (int i = plan.noteCount - 1; i >= 0; --i) {
                const auto& n = plan.notes[static_cast<std::size_t>(i)];
                if (!isEditableNote(n))
                    continue;
                const NoteBox box = noteBoxOf(n, lay, mode, minNote, maxNote);
                if (mx >= box.x - slop && mx <= box.x + box.w + slop && my >= box.y - slop &&
                    my <= box.y + box.h + slop) {
                    hoveredNote_ = i;
                    hoveredNoteEdge_ = !n.muted && box.w >= context_.px(10.0f) && mx >= box.x + box.w - edgeGrab;
                    return;
                }
            }
            return;
        }

        if (my >= lay.velY && my <= lay.velY + lay.velH && mx >= lay.padL && mx <= lay.padL + lay.plotW) {
            hoveredVelLane_ = true;
            float bestDist = context_.px(10.0f);
            for (int i = 0; i < plan.noteCount; ++i) {
                const auto& n = plan.notes[static_cast<std::size_t>(i)];
                if (!isEditableNote(n))
                    continue;
                const float stemX = xOf(n.warpedBeats) + context_.px(2.0f);
                const float d = std::abs(mx - stemX);
                if (d < bestDist) {
                    bestDist = d;
                    hoveredVelNote_ = i;
                }
            }
        }
    }

    void showChordMenu(int barIdx) {
        const auto ov = context_.model.overrides();
        const int cur = static_cast<int>(ov.barChordOffsets[static_cast<std::size_t>(barIdx)]);
        const auto& plan = context_.model.visuals().plan;
        const int barCount = std::clamp(static_cast<int>(std::lround(std::max(4.0, plan.phraseBeats))) / 4, 1,
                                        PhraseOverrides::kMaxChordBars);

        visage::PopupMenu menu;
        menu.addOption(1, "Bar: Tonic i (0)").select(cur == 0);
        menu.addOption(2, "Bar: +1 scale degree");
        menu.addOption(3, "Bar: -1 scale degree");
        menu.addOption(4, "Bar: bVI (-2 deg)").select(cur == -2);
        menu.addOption(5, "Bar: bVII (-1 deg)").select(cur == -1);
        menu.addOption(6, "Bar: iv (+3 deg)").select(cur == 3);
        menu.addOption(7, "Bar: v (+4 deg)").select(cur == 4);
        menu.addOption(10, "Progression: Pedal (i \u2013 i \u2013 i \u2013 i)");
        menu.addOption(11, "Progression: Dark Neuro (i \u2013 i \u2013 bII \u2013 i)");
        menu.addOption(12, "Progression: Epic Minor (i \u2013 bVI \u2013 bVII \u2013 i)");
        menu.addOption(13, "Progression: Liquid Roll (i \u2013 bIII \u2013 bVI \u2013 bVII)");
        menu.addOption(14, "Progression: Dorian Lift (i \u2013 iv \u2013 bVII \u2013 v)");
        menu.addOption(15, "Progression: Tension Climb (i \u2013 bII \u2013 bIII \u2013 bII)");
        menu.addOption(20, "Reset All Chords").enable(ov.hasChordProgression());

        menu.onSelection() = [this, barIdx, barCount](int opt) {
            auto next = context_.model.overrides();
            const auto applyPattern = [&](std::array<std::int8_t, 4> pat) {
                next.barChordOffsets = {};
                for (int b = 0; b < barCount; ++b)
                    next.barChordOffsets[static_cast<std::size_t>(b)] = pat[static_cast<std::size_t>(b % 4)];
            };
            const auto idx = static_cast<std::size_t>(barIdx);
            if (opt == 1)
                next.barChordOffsets[idx] = 0;
            else if (opt == 2)
                next.barChordOffsets[idx] =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(next.barChordOffsets[idx]) + 1, -7, 7));
            else if (opt == 3)
                next.barChordOffsets[idx] =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(next.barChordOffsets[idx]) - 1, -7, 7));
            else if (opt == 4)
                next.barChordOffsets[idx] = -2;
            else if (opt == 5)
                next.barChordOffsets[idx] = -1;
            else if (opt == 6)
                next.barChordOffsets[idx] = 3;
            else if (opt == 7)
                next.barChordOffsets[idx] = 4;
            else if (opt == 10 || opt == 20)
                next.barChordOffsets = {};
            else if (opt == 11)
                applyPattern({0, 0, 1, 0});
            else if (opt == 12)
                applyPattern({0, -2, -1, 0});
            else if (opt == 13)
                applyPattern({0, 2, -2, -1});
            else if (opt == 14)
                applyPattern({0, 3, -1, 4});
            else if (opt == 15)
                applyPattern({0, 1, 2, 1});
            context_.model.setOverrides(next);
            redraw();
        };
        menu.show(this);
    }

    void showSpanMenu(int spanIdx) {
        const auto ov = context_.model.overrides();
        const auto& sp = ov.spans[static_cast<std::size_t>(spanIdx)];
        const auto& plan = context_.model.visuals().plan;
        const bool canCopyNext = (spanIdx + 1 < plan.spanCount && spanIdx + 1 < PhraseOverrides::kMaxSpanOverrides);

        visage::PopupMenu menu;
        menu.addOption(100, sp.locked ? "Unlock Span" : "Lock Span (Pin Seed)");
        menu.addOption(101, "Roll This Span (New Seed)");
        menu.addOption(102, "Copy Span \u2192 Next").enable(canCopyNext);
        menu.addOption(110, "Octave: +1").enable(sp.octaveShift < 2);
        menu.addOption(111, "Octave: -1").enable(sp.octaveShift > -2);
        menu.addOption(112, "Octave: Reset (0)").enable(sp.octaveShift != 0);
        menu.addOption(115, "Invert Contour").select(sp.invert);
        menu.addOption(120, "Time: Half (\u00bd\u00d7)").select(sp.timeScale < 0);
        menu.addOption(121, "Time: Normal (1\u00d7)").select(sp.timeScale == 0);
        menu.addOption(122, "Time: Double (2\u00d7)").select(sp.timeScale > 0);
        menu.addOption(0, "Role: Auto").select(sp.roleOverride < 0);
        menu.addOption(1, "Role: Statement").select(sp.roleOverride == static_cast<int>(Role::statement));
        menu.addOption(2, "Role: Answer").select(sp.roleOverride == static_cast<int>(Role::answer));
        menu.addOption(3, "Role: Mutation").select(sp.roleOverride == static_cast<int>(Role::mutation));
        menu.addOption(4, "Role: Ratchet").select(sp.roleOverride == static_cast<int>(Role::ratchet));
        menu.addOption(5, "Role: Turnaround").select(sp.roleOverride == static_cast<int>(Role::turnaround));
        menu.addOption(6, "Role: Rest (Drop-out)").select(sp.roleOverride == static_cast<int>(Role::rest));
        menu.addOption(199, "Reset This Span").enable(sp.active());
        menu.addOption(200, "Reset All Phrase Edits").enable(ov.hasAny());

        menu.onSelection() = [this, spanIdx](int opt) {
            if (opt == 200) {
                context_.model.clearOverrides();
                redraw();
                return;
            }
            auto next = context_.model.overrides();
            auto& target = next.spans[static_cast<std::size_t>(spanIdx)];
            const auto globalSeed = static_cast<std::uint16_t>(
                std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::seed))), 1, 999));

            if (opt == 100) {
                target.locked = !target.locked;
                if (target.locked)
                    target.lockedSeed = globalSeed;
            } else if (opt == 101) {
                const std::uint32_t base = target.locked && target.lockedSeed > 0 ? target.lockedSeed : globalSeed;
                const auto rolled = static_cast<std::uint16_t>(
                    1U +
                    (hba::dsp::laneHash(base * 2654435761U + static_cast<std::uint32_t>(spanIdx + 1) * 97U) % 999U));
                target.locked = true;
                target.lockedSeed = rolled == base ? static_cast<std::uint16_t>((rolled % 999U) + 1U) : rolled;
            } else if (opt == 102 && spanIdx + 1 < PhraseOverrides::kMaxSpanOverrides) {
                const auto& curPlan = context_.model.visuals().plan;
                if (!target.locked) {
                    target.locked = true;
                    target.lockedSeed = globalSeed;
                }
                next.spans[static_cast<std::size_t>(spanIdx + 1)] = target;
                if (spanIdx + 1 < curPlan.spanCount) {
                    const double beatDelta = curPlan.spans[static_cast<std::size_t>(spanIdx + 1)].startBeats -
                                             curPlan.spans[static_cast<std::size_t>(spanIdx)].startBeats;
                    const int tickDelta = static_cast<int>(std::lround(beatDelta * 24.0));
                    const auto snapshotNotes = next.notes;
                    for (const auto& n : snapshotNotes) {
                        if (!n.active() || !n.key.valid() || n.key.spanIdx != spanIdx)
                            continue;
                        NoteKey dstKey = n.key;
                        dstKey.spanIdx = static_cast<std::int8_t>(spanIdx + 1);
                        dstKey.stepTick =
                            static_cast<std::int16_t>(std::max(0, static_cast<int>(n.key.stepTick) + tickDelta));
                        if (auto* dst = next.findOrCreateNote(dstKey)) {
                            *dst = n;
                            dst->key = dstKey;
                            dst->slotIndex = -1;
                        }
                    }
                }
            } else if (opt == 110) {
                target.octaveShift =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(target.octaveShift) + 1, -2, 2));
            } else if (opt == 111) {
                target.octaveShift =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(target.octaveShift) - 1, -2, 2));
            } else if (opt == 112) {
                target.octaveShift = 0;
            } else if (opt == 115) {
                target.invert = !target.invert;
            } else if (opt == 120) {
                target.timeScale = -1;
            } else if (opt == 121) {
                target.timeScale = 0;
            } else if (opt == 122) {
                target.timeScale = 1;
            } else if (opt == 0) {
                target.roleOverride = -1;
            } else if (opt >= 1 && opt <= 6) {
                target.roleOverride = static_cast<std::int8_t>(opt - 1);
            } else if (opt == 199) {
                target = {};
            }
            context_.model.setOverrides(next);
            redraw();
        };
        menu.show(this);
    }

    void showNoteMenu(NoteKey key, int slotIndex, bool currentlyMuted) {
        const auto ov = context_.model.overrides();
        const auto* existing = ov.findNoteFor(key, slotIndex);
        const bool hasNote = existing != nullptr;
        const std::uint8_t curRatchet = existing != nullptr ? existing->ratchetCount : 0;

        visage::PopupMenu menu;
        menu.addOption(1, currentlyMuted ? "Unmute Note" : "Mute Note");
        menu.addOption(2, "Accent (+28 vel)");
        menu.addOption(3, "Ghost (-28 vel)");
        menu.addOption(4, "Flip / Add Pitch Dive");
        menu.addOption(10, "Ratchet: Off").select(curRatchet == 0);
        menu.addOption(12, "Ratchet: \u00d72 Burst").select(curRatchet == 2);
        menu.addOption(13, "Ratchet: \u00d73 Triplet").select(curRatchet == 3);
        menu.addOption(14, "Ratchet: \u00d74 Stutter").select(curRatchet == 4);
        menu.addOption(16, "Ratchet: \u00d76 Roll").select(curRatchet == 6);
        menu.addOption(5, "Reset This Note").enable(hasNote);
        menu.addOption(6, "Reset All Phrase Edits").enable(ov.hasAny());

        menu.onSelection() = [this, key, slotIndex](int opt) {
            if (opt == 6) {
                context_.model.clearOverrides();
                redraw();
                return;
            }
            auto next = context_.model.overrides();
            if (opt == 5) {
                if (key.valid())
                    next.clearNote(key);
                if (slotIndex >= 0)
                    next.clearNote(slotIndex);
            } else if (auto* no = ensureNoteOverride(next, key, slotIndex)) {
                if (opt == 1)
                    no->muted = !no->muted;
                else if (opt == 2)
                    no->velocityDelta =
                        static_cast<std::int8_t>(std::clamp(static_cast<int>(no->velocityDelta) + 28, -64, 64));
                else if (opt == 3)
                    no->velocityDelta =
                        static_cast<std::int8_t>(std::clamp(static_cast<int>(no->velocityDelta) - 28, -64, 64));
                else if (opt == 4)
                    no->flipBend = !no->flipBend;
                else if (opt == 10)
                    no->ratchetCount = 0;
                else if (opt == 12)
                    no->ratchetCount = 2;
                else if (opt == 13)
                    no->ratchetCount = 3;
                else if (opt == 14)
                    no->ratchetCount = 4;
                else if (opt == 16)
                    no->ratchetCount = 6;
                pruneNoteOverride(next, key, slotIndex, no);
            }
            context_.model.setOverrides(next);
            redraw();
        };
        menu.show(this);
    }

    Context& context_;
    std::array<float, 5> activityHold_{};
    int hoveredSpan_ = -1;
    int hoveredChordBar_ = -1;
    int hoveredDivider_ = 0;
    int hoveredNote_ = -1;
    bool hoveredNoteEdge_ = false;
    bool hoveredVelLane_ = false;
    int hoveredVelNote_ = -1;
    bool hoveredReset_ = false;
    float resetBtnX_ = -1.0f;
    float resetBtnY_ = 0.0f;
    float resetBtnW_ = 0.0f;
    float resetBtnH_ = 0.0f;

    DragMode dragMode_ = DragMode::none;
    int dragNoteIdx_ = -1;
    NoteKey dragNoteKey_{};
    int dragSlotIndex_ = -1;
    int dragChordBar_ = -1;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    int dragBasePitchDelta_ = 0;
    int dragBaseLength_ = 0;
    int dragBaseNudge_ = 0;
    int dragBaseChordDeg_ = 0;
    int dragOriginalMidi_ = 41;

    std::array<VelBrushItem, kMaxPhraseNotes> velBrushItems_{};
    int velBrushCount_ = 0;
    float velBrushLastX_ = 0.0f;
};

class PhrasePanel final : public Panel {
  public:
    explicit PhrasePanel(Context& context)
        : Panel(context), outputMode_(context, ParamId::outputMode, colour::iris),
          inputMode_(context, ParamId::inputMode, colour::cyan),
          phraseBars_(context, ParamId::phraseBars, colour::iris),
          phraseMode_(context, ParamId::phraseMode, colour::amber),
          seed_(context, ParamId::seed, colour::iris, {}, true),
          seedPrev_(context, "\u2039", [this] { context_.model.stepSeedHistory(-1); }),
          rollSeed_(context, "Roll seed", [this] { context_.model.rollSeed(); }),
          seedNext_(context, "\u203a", [this] { context_.model.stepSeedHistory(1); }),
          attractor_(context, ParamId::attractor, colour::vermilion),
          rootNote_(context, ParamId::rootNote, colour::cyan), octave_(context, ParamId::octave, colour::cyan),
          scale_(context, ParamId::scale, colour::cyan), contour_(context, ParamId::contour, colour::iris),
          drumStyle_(context, ParamId::drumStyle, colour::vermilion), latch_(context, ParamId::latch, colour::cyan),
          stage_(context) {
        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &phraseMode_, &seed_, &attractor_, &rootNote_,
                        &octave_, &scale_, &contour_, &drumStyle_, &latch_})
            addChild(r);
        seedPrev_.setAccent(colour::textDim);
        rollSeed_.setAccent(colour::iris);
        seedNext_.setAccent(colour::textDim);
        addChild(&seedPrev_);
        addChild(&rollSeed_);
        addChild(&seedNext_);
        addChild(&stage_);
        syncModeVisibility();
    }

    void tick() override {
        seedPrev_.setAccent(context_.model.canStepSeedHistory(-1) ? colour::text : colour::textFaint);
        seedNext_.setAccent(context_.model.canStepSeedHistory(1) ? colour::text : colour::textFaint);
        syncModeVisibility();
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float pad = context_.px(metric::pad);
        const float railW = context_.px(216.0f);
        const float hintY = height() - pad - context_.px(34.0f);
        const float hintH = context_.px(34.0f);
        canvas.setColor(colour::withAlpha(colour::window, 0.55f));
        canvas.roundedRectangle(pad, hintY, railW, hintH, context_.px(5.0f));
        canvas.setColor(colour::edge);
        canvas.roundedRectangleBorder(pad, hintY, railW, hintH, context_.px(5.0f), context_.px(0.8f));
        drawLabel(canvas, context_, "Drag pitch \u00b7 Edge len \u00b7 Shift nudge", pad + context_.px(6.0f),
                  hintY + context_.px(3.0f), railW - context_.px(12.0f), context_.px(14.0f),
                  colour::withAlpha(colour::textDim, 0.82f), visage::Font::kCenter, 9.0f, Weight::medium);
        drawLabel(canvas, context_, "Brush VEL \u00b7 Drag chords \u00b7 R-click ops", pad + context_.px(6.0f),
                  hintY + context_.px(17.0f), railW - context_.px(12.0f), context_.px(14.0f),
                  colour::withAlpha(colour::textDim, 0.82f), visage::Font::kCenter, 9.0f, Weight::medium);
    }

    void resized() override {
        const float pad = context_.px(metric::pad);
        const float railW = context_.px(216.0f);
        const float gap = context_.px(metric::gap);
        const float rowH = context_.px(23.0f);
        const float rowGap = context_.px(3.5f);
        float y = pad;

        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &phraseMode_, &seed_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + rowGap;
        }

        const float navW = context_.px(28.0f);
        const float navGap = context_.px(4.0f);
        const float btnH = context_.px(24.0f);
        y += context_.px(2.0f);
        seedPrev_.setBounds(pad, y, navW, btnH);
        rollSeed_.setBounds(pad + navW + navGap, y, railW - 2.0f * navW - 2.0f * navGap, btnH);
        seedNext_.setBounds(pad + railW - navW, y, navW, btnH);
        y += btnH + context_.px(10.0f);

        for (auto* r : {&attractor_, &rootNote_, &octave_, &scale_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + rowGap;
        }
        // Contour (Mono/MPE) and Kit style (Drum Kit) share the exact same rail slot.
        contour_.setBounds(pad, y, railW, rowH);
        drumStyle_.setBounds(pad, y, railW, rowH);
        y += rowH + rowGap;

        latch_.setBounds(pad, y, railW, rowH);

        const float stageX = pad + railW + gap * 1.5f;
        stage_.setBounds(stageX, pad, width() - stageX - pad, height() - 2.0f * pad);
    }

  private:
    void syncModeVisibility() {
        const bool isDrum =
            static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f) == static_cast<int>(OutputMode::drum);
        contour_.setVisible(!isDrum);
        drumStyle_.setVisible(isDrum);
    }

    Row outputMode_;
    Row inputMode_;
    Row phraseBars_;
    Row phraseMode_;
    Row seed_;
    Button seedPrev_;
    Button rollSeed_;
    Button seedNext_;
    Row attractor_;
    Row rootNote_;
    Row octave_;
    Row scale_;
    Row contour_;
    Row drumStyle_;
    Row latch_;
    PhraseStage stage_;
};

} // namespace

std::unique_ptr<Panel> makePhrasePanel(Context& context) { return std::make_unique<PhrasePanel>(context); }

} // namespace cl::ui
