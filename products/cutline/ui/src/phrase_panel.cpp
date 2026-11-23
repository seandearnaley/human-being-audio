// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline's central hero stage: structural Phrase Grammar Tape + Microtiming
// Vector Piano Roll (grid ticks, displacement stems, pitch-dive curves, live
// playhead) paired with the left grammar/scale/pocket rail.
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace cl::ui {

namespace {

constexpr std::array<const char*, 6> kDrumLabels{"KICK", "SNARE", "GHOST", "CH HAT", "OP HAT", "RATCH"};

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
    }
    return "";
}

class PhraseStage final : public visage::Frame, public Live {
  public:
    explicit PhraseStage(Context& context) : context_(context) { context_.live.push_back(this); }
    ~PhraseStage() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override { redraw(); }

    void draw(visage::Canvas& canvas) override {
        const auto& v = context_.model.visuals();
        const auto& plan = v.plan;
        const auto px = [this](float d) { return context_.px(d); };

        const float w = width();
        const float h = height();
        drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::window, metric::radiusSmall);

        const float padL = px(64.0f);
        const float padR = px(12.0f);
        const float plotW = std::max(1.0f, w - padL - padR);
        const double totalBeats = std::max(4.0, plan.phraseBeats);
        const auto xOf = [&](double beat) { return padL + static_cast<float>(beat / totalBeats) * plotW; };

        const float rulerY = px(8.0f);
        const float tapeY = px(28.0f);
        const float tapeH = px(26.0f);
        const float rollY = px(64.0f);
        const float rollH = std::max(px(80.0f), h - rollY - px(28.0f));
        const float footerY = h - px(22.0f);

        // Bar & beat vertical grid lines
        const int totalBeatCount = static_cast<int>(std::lround(totalBeats));
        for (int b = 0; b <= totalBeatCount; ++b) {
            const float x = xOf(static_cast<double>(b));
            const bool isBar = (b % 4) == 0;
            canvas.setColor(isBar ? colour::track : colour::withAlpha(colour::edge, 0.65f));
            canvas.fill(x, tapeY, px(isBar ? 1.2f : 0.8f), rollY + rollH - tapeY);
            if (isBar && b < totalBeatCount) {
                char barLabel[16]{};
                std::snprintf(barLabel, sizeof(barLabel), "BAR %d", b / 4 + 1);
                drawLabel(canvas, context_, barLabel, x + px(4.0f), rulerY, px(54.0f), px(16.0f), colour::textDim,
                          visage::Font::kLeft, type::small, Weight::semibold);
            }
        }

        // Left track headers
        drawLabel(canvas, context_, "PHRASE", px(6.0f), tapeY, padL - px(12.0f), tapeH, colour::textFaint,
                  visage::Font::kRight, type::small, Weight::semibold);

        const auto mode =
            static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
        drawLabel(canvas, context_, mode == OutputMode::drum ? "KIT" : "ROLL", px(6.0f), rollY + px(4.0f),
                  padL - px(10.0f), px(18.0f), colour::textFaint, visage::Font::kRight, type::small, Weight::semibold);

        // 1. Phrase Grammar Tape
        for (int i = 0; i < plan.spanCount; ++i) {
            const auto& span = plan.spans[static_cast<std::size_t>(i)];
            const float x1 = xOf(span.startBeats) + px(1.5f);
            const float x2 = xOf(span.endBeats) - px(1.5f);
            const float bw = std::max(px(4.0f), x2 - x1);
            const unsigned col = roleColour(span.role);
            canvas.setColor(colour::withAlpha(col, 0.18f));
            canvas.roundedRectangle(x1, tapeY, bw, tapeH, px(4.0f));
            canvas.setColor(col);
            canvas.roundedRectangleBorder(x1, tapeY, bw, tapeH, px(4.0f), px(1.2f));
            if (bw > px(42.0f)) {
                drawLabel(canvas, context_, roleName(span.role), x1 + px(6.0f), tapeY, bw - px(8.0f), tapeH,
                          colour::text, visage::Font::kLeft, type::small, Weight::semibold);
            }
        }

        // 2. Piano Roll / Drum Grid container
        canvas.setColor(colour::withAlpha(colour::panel, 0.72f));
        canvas.roundedRectangle(padL, rollY, plotW, rollH, px(4.0f));
        canvas.setColor(colour::edge);
        canvas.roundedRectangleBorder(padL, rollY, plotW, rollH, px(4.0f), px(1.0f));

        int minNote = 36;
        int maxNote = 66;
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

        if (mode == OutputMode::drum) {
            const float rowH = rollH / 6.0f;
            for (int r = 0; r < 6; ++r) {
                const float ry = rollY + static_cast<float>(5 - r) * rowH;
                canvas.setColor(colour::withAlpha(colour::edge, 0.7f));
                canvas.fill(padL, ry, plotW, px(1.0f));
                drawLabel(canvas, context_, kDrumLabels[static_cast<std::size_t>(r)], px(4.0f), ry, padL - px(10.0f),
                          rowH, colour::textDim, visage::Font::kRight, type::small, Weight::medium);
            }
        } else {
            for (int m = minNote; m <= maxNote; ++m) {
                if ((m % 12) == 5) { // F octave anchor guides
                    const float norm = static_cast<float>(m - minNote) / static_cast<float>(maxNote - minNote);
                    const float y = rollY + rollH - norm * (rollH - px(16.0f)) - px(8.0f);
                    canvas.setColor(colour::withAlpha(colour::edge, 0.7f));
                    canvas.fill(padL, y, plotW, px(1.0f));
                    char octBuf[8]{};
                    std::snprintf(octBuf, sizeof(octBuf), "F%d", m / 12 - 1);
                    drawLabel(canvas, context_, octBuf, px(4.0f), y - px(8.0f), padL - px(10.0f), px(16.0f),
                              colour::textFaint, visage::Font::kRight, type::small, Weight::regular);
                }
            }
        }

        // Draw compiled notes + microtiming displacement stems + pitch bend trajectories
        for (int i = 0; i < plan.noteCount; ++i) {
            const auto& n = plan.notes[static_cast<std::size_t>(i)];
            const float xGrid = xOf(n.startBeats);
            const float xWarped = xOf(n.warpedBeats);
            const float xEnd = xOf(std::min(totalBeats, n.warpedBeats + n.durationBeats));
            const float nw = std::max(px(6.0f), xEnd - xWarped);
            const unsigned col = roleColour(n.role);
            const float velNorm = static_cast<float>(n.velocity) / 127.0f;

            float ny = rollY;
            float nh = px(12.0f);
            if (mode == OutputMode::drum) {
                const float rowH = rollH / 6.0f;
                const int row = std::clamp(static_cast<int>(n.drumRow), 0, 5);
                nh = std::max(px(10.0f), rowH * (0.42f + 0.34f * velNorm));
                ny = rollY + static_cast<float>(5 - row) * rowH + (rowH - nh) * 0.5f;
            } else {
                nh = px(8.0f + 7.0f * velNorm);
                const float norm = static_cast<float>(n.noteNumber - minNote) / static_cast<float>(maxNote - minNote);
                ny = rollY + rollH - norm * (rollH - px(20.0f)) - px(10.0f) - nh * 0.5f;
            }

            // Grid tick & microtiming shift vector stem
            if (std::abs(xWarped - xGrid) > px(1.0f)) {
                canvas.setColor(colour::textFaint);
                canvas.fill(xGrid, ny - px(3.0f), px(1.0f), nh + px(6.0f));
                const float stemX = std::min(xGrid, xWarped);
                const float stemW = std::abs(xWarped - xGrid);
                canvas.setColor(colour::vermilion);
                canvas.fill(stemX, ny + nh * 0.5f - px(1.0f), stemW, px(2.0f));
            }

            // Highlight currently sounding MIDI notes
            const bool sounding =
                (n.noteNumber < 64)
                    ? ((v.activeNotesLow & (std::uint64_t{1} << static_cast<unsigned>(n.noteNumber))) != 0ULL)
                    : ((v.activeNotesHigh & (std::uint64_t{1} << static_cast<unsigned>(n.noteNumber - 64))) != 0ULL);

            canvas.setColor(colour::withAlpha(col, sounding ? 0.92f : (0.28f + 0.52f * velNorm)));
            canvas.roundedRectangle(xWarped, ny, nw, nh, px(3.0f));
            canvas.setColor(sounding ? colour::text : col);
            canvas.roundedRectangleBorder(xWarped, ny, nw, nh, px(3.0f), px(sounding ? 1.6f : 1.0f));

            if (mode == OutputMode::mpe && nw > px(24.0f)) {
                char chBuf[8]{};
                std::snprintf(chBuf, sizeof(chBuf), "C%d", static_cast<int>(n.channel));
                drawLabel(canvas, context_, chBuf, xWarped + px(3.0f), ny, nw - px(4.0f), nh, colour::text,
                          visage::Font::kLeft, type::small, Weight::semibold);
            }

            // Pitch bend curve ribbon
            if (mode != OutputMode::drum && std::abs(n.bendSemitones) > 0.25f) {
                const float dy = -std::clamp(n.bendSemitones * px(3.0f), -px(38.0f), px(38.0f));
                const float y0 = ny + nh * 0.5f;
                const float y1 = std::clamp(y0 + dy, rollY + px(5.0f), rollY + rollH - px(5.0f));
                visage::Path bend;
                bend.moveTo(xWarped + px(2.0f), y0);
                for (int s = 1; s <= 8; ++s) {
                    const float frac = static_cast<float>(s) / 8.0f;
                    bend.lineTo(xWarped + nw * frac, y0 + (y1 - y0) * (frac * frac));
                }
                canvas.setColor(colour::withAlpha(colour::text, 0.85f));
                canvas.stroke(bend, 0.0f, 0.0f, w, h, px(1.4f));
            }
        }

        // Live playhead cursor
        if (v.playing || v.playheadBeats > 0.0) {
            const float pxHead = xOf(std::clamp(v.playheadBeats, 0.0, totalBeats));
            canvas.setColor(colour::vermilion);
            canvas.fill(pxHead - px(1.0f), tapeY, px(2.0f), rollY + rollH - tapeY);
        }

        // Telemetry footer strip
        char footer[128]{};
        const char* modeLabel =
            mode == OutputMode::mono ? "MONO CH1" : (mode == OutputMode::mpe ? "MPE CH2-15" : "DRUM KIT");
        std::snprintf(footer, sizeof(footer), "%d notes   \u00b7   %s   \u00b7   %d ev/blk (cap 256)", plan.noteCount,
                      modeLabel, v.lastBlockEvents);
        drawLabel(canvas, context_, footer, padL, footerY, plotW, px(18.0f), colour::textDim, visage::Font::kLeft,
                  type::small, Weight::medium);
    }

  private:
    Context& context_;
};

class PhrasePanel final : public Panel {
  public:
    explicit PhrasePanel(Context& context)
        : Panel(context), outputMode_(context, ParamId::outputMode, colour::iris),
          inputMode_(context, ParamId::inputMode, colour::cyan),
          phraseBars_(context, ParamId::phraseBars, colour::iris),
          seed_(context, ParamId::seed, colour::iris, {}, true),
          rollSeed_(context, "Roll seed", [this] { context_.model.rollSeed(); }),
          attractor_(context, ParamId::attractor, colour::vermilion),
          rootNote_(context, ParamId::rootNote, colour::cyan), octave_(context, ParamId::octave, colour::cyan),
          scale_(context, ParamId::scale, colour::cyan), stage_(context) {
        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &seed_, &attractor_, &rootNote_, &octave_, &scale_})
            addChild(r);
        rollSeed_.setAccent(colour::iris);
        addChild(&rollSeed_);
        addChild(&stage_);
    }

    void resized() override {
        const float pad = context_.px(metric::pad);
        const float railW = context_.px(216.0f);
        const float gap = context_.px(metric::gap);
        const float rowH = context_.px(28.0f);
        float y = pad;

        for (auto* r : {&outputMode_, &inputMode_, &phraseBars_, &seed_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + context_.px(6.0f);
        }
        rollSeed_.setBounds(pad, y, railW, context_.px(26.0f));
        y += context_.px(36.0f);

        for (auto* r : {&attractor_, &rootNote_, &octave_, &scale_}) {
            r->setBounds(pad, y, railW, rowH);
            y += rowH + context_.px(6.0f);
        }

        const float stageX = pad + railW + gap * 1.5f;
        stage_.setBounds(stageX, pad, width() - stageX - pad, height() - 2.0f * pad);
    }

  private:
    Row outputMode_;
    Row inputMode_;
    Row phraseBars_;
    Row seed_;
    Button rollSeed_;
    Row attractor_;
    Row rootNote_;
    Row octave_;
    Row scale_;
    PhraseStage stage_;
};

} // namespace

std::unique_ptr<Panel> makePhrasePanel(Context& context) { return std::make_unique<PhrasePanel>(context); }

} // namespace cl::ui
