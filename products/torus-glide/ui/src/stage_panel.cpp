// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <hba/dsp/control.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace tg::ui {

namespace {
constexpr std::array<const char*, 12> kNoteNames{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

std::string midiNoteName(float midi) {
    const int n = static_cast<int>(std::lround(midi));
    const int pc = ((n % 12) + 12) % 12;
    const int oct = (n / 12) - 1;
    return std::string(kNoteNames[static_cast<std::size_t>(pc)]) + std::to_string(oct);
}

class StagePanel final : public Panel, public Live {
  public:
    explicit StagePanel(Context& ctx)
        : Panel(ctx), capA_(ctx, "Cap A", [this] { context_.model.captureHeldChord(0); }),
          capB_(ctx, "Cap B", [this] { context_.model.captureHeldChord(1); }),
          capC_(ctx, "Cap C", [this] { context_.model.captureHeldChord(2); }),
          capD_(ctx, "Cap D", [this] { context_.model.captureHeldChord(3); }),
          optBtn_(ctx, "Auto-Lead", [this] { context_.model.optimizeVoiceLeading(); }),
          resetBtn_(ctx, "Reset", [this] { context_.model.resetSceneMatrix(); }) {
        optBtn_.setAccent(colour::copedent);
        resetBtn_.setAccent(colour::textDim);
        for (auto* b : {&capA_, &capB_, &capC_, &capD_, &optBtn_, &resetBtn_})
            addChild(b);
        context_.live.push_back(this);
    }

    ~StagePanel() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        const bool showCopedentTools = (modeIdx != 0);
        const bool hasHeld = context_.model.visuals().heldNoteCount > 0;
        const unsigned capCol = hasHeld ? colour::mobius : colour::textFaint;

        for (auto* b : {&capA_, &capB_, &capC_, &capD_}) {
            b->setVisible(showCopedentTools);
            b->setAccent(capCol);
            b->setActive(hasHeld);
        }
        optBtn_.setVisible(showCopedentTools);
        resetBtn_.setVisible(showCopedentTools);
        redraw();
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float bh = px(22.0f);
        const float y = px(8.0f);
        const float gap = px(4.0f);

        float rx = width() - px(14.0f);
        const float resetW = px(52.0f);
        rx -= resetW;
        resetBtn_.setBounds(rx, y, resetW, bh);

        const float optW = px(78.0f);
        rx -= gap + optW;
        optBtn_.setBounds(rx, y, optW, bh);

        rx -= px(8.0f);
        const float capW = px(46.0f);
        for (auto* b : {&capD_, &capC_, &capB_, &capA_}) {
            rx -= capW;
            b->setBounds(rx, y, capW, bh);
            rx -= px(3.0f);
        }
    }

    void mouseMove(const visage::MouseEvent& e) override {
        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        if (modeIdx == 0) {
            hoverScene_ = -1;
            hoverVoice_ = -1;
            return;
        }
        const auto plot = plotRect();
        const auto hit = hitTestNode(plot, modeIdx == 2, e.position);
        hoverScene_ = hit.first;
        hoverVoice_ = hit.second;
    }

    void mouseExit(const visage::MouseEvent&) override {
        hoverScene_ = -1;
        hoverVoice_ = -1;
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        const auto plot = plotRect();

        // Clickable scene footer strip below ribbon plot -> jump straight to Scene A / B / C / D!
        if (modeIdx != 0 && e.position.y > plot.y + plot.h && e.position.x >= plot.x &&
            e.position.x <= plot.x + plot.w) {
            const int segTotal = (modeIdx == 2) ? kSceneCount : (kSceneCount - 1);
            const float normX = std::clamp((e.position.x - plot.x) / std::max(1.0f, plot.w), 0.0f, 1.0f);
            const int scene = static_cast<int>(std::lround(normX * static_cast<float>(segTotal))) % kSceneCount;
            context_.model.jumpToScene(scene);
            return;
        }

        if (e.position.x < plot.x || e.position.x > plot.x + plot.w || e.position.y < plot.y ||
            e.position.y > plot.y + plot.h)
            return;

        if (modeIdx == 0) {
            draggingBell_ = true;
            bellDragPos_ = e.position;
            context_.model.begin(ParamId::bellCenter);
            context_.model.begin(ParamId::bellWidth);
            applyBellDrag(e.position);
            return;
        }

        // First hit-test the 4x5 draggable chord nodes
        const auto hit = hitTestNode(plot, modeIdx == 2, e.position);
        if (hit.first >= 0) {
            dragScene_ = hit.first;
            dragVoice_ = hit.second;
            dragMatrix_ = context_.model.sceneMatrix();
            return;
        }

        // Otherwise scrub Morph directly across the ribbon like a slide-table touch strip!
        scrubbingMorph_ = true;
        if (context_.model.get(ParamId::morphAuto) >= 0.5f) {
            context_.model.begin(ParamId::morphAuto);
            context_.model.set(ParamId::morphAuto, 0.0f);
            context_.model.end(ParamId::morphAuto);
        }
        context_.model.begin(ParamId::morph);
        applyMorphScrub(e.position);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (draggingBell_) {
            bellDragPos_ = e.position;
            applyBellDrag(e.position);
            return;
        }
        if (scrubbingMorph_) {
            applyMorphScrub(e.position);
            return;
        }
        if (dragScene_ >= 0 && dragVoice_ >= 0) {
            const auto plot = plotRect();
            float rawOffset = yToOffset(plot, e.position.y);
            if (!e.isShiftDown())
                rawOffset = std::round(rawOffset);
            rawOffset = std::clamp(rawOffset, -16.0f, 36.0f);
            dragMatrix_.offsets[static_cast<std::size_t>(dragScene_)][static_cast<std::size_t>(dragVoice_)] = rawOffset;
            dragMatrix_.isCustom = true;
            context_.model.setSceneMatrix(dragMatrix_);
            redraw();
        }
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (draggingBell_) {
            draggingBell_ = false;
            context_.model.end(ParamId::bellCenter);
            context_.model.end(ParamId::bellWidth);
        }
        if (scrubbingMorph_) {
            scrubbingMorph_ = false;
            context_.model.end(ParamId::morph);
        }
        dragScene_ = -1;
        dragVoice_ = -1;
    }

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        const unsigned accent = modeAccent(modeIdx);
        const auto& tel = context_.model.visuals();

        // Top accent hairline
        canvas.setColor(colour::withAlpha(accent, 0.88f));
        canvas.roundedRectangle(context_.px(10.0f), 0.0f, width() - context_.px(20.0f), context_.px(2.0f),
                                context_.px(1.0f));

        // Header title & telemetry readout
        const char* modeTitle =
            modeIdx == 0   ? "SPIRAL MANIFOLD  ·  BAR-LOCKED SHEPARD-RISSET BELL (DRAG TO SHAPE)"
            : modeIdx == 1 ? "COPEDENT RIBBONS  ·  DRAG NODES TO EDIT CHORDS · DRAG SPACE TO SCRUB MORPH"
                           : "M\xc3\x96"
                             "BIUS TORUS  ·  INFINITE SHEPARD-CHORD GLIDE · CLICK A-D OR HOLD CHORD TO CAPTURE";
        drawLabel(canvas, context_, modeTitle, context_.px(14.0f), context_.px(9.0f), width() * 0.52f,
                  context_.px(20.0f), accent, visage::Font::kLeft, 10.0f, Weight::semibold);

        char hud[96]{};
        if (tel.heldNoteCount > 0 && modeIdx != 0) {
            std::snprintf(hud, sizeof(hud), "%d HELD · READY TO CAP  ·  BAR %d.%d", tel.heldNoteCount, tel.barNumber,
                          tel.beatNumber);
        } else {
            std::snprintf(hud, sizeof(hud), "BAR %d.%d  ·  %.0f BPM  ·  %s", tel.barNumber, tel.beatNumber,
                          static_cast<double>(tel.bpm), tel.sounding ? "ACTIVE" : "READY");
        }
        const float hudRight = modeIdx == 0 ? (width() - context_.px(14.0f)) : (width() - context_.px(364.0f));
        drawLabel(canvas, context_, hud, hudRight - context_.px(240.0f), context_.px(9.0f), context_.px(240.0f),
                  context_.px(20.0f),
                  (tel.heldNoteCount > 0 && modeIdx != 0) ? colour::mobius
                                                          : (tel.sounding ? colour::text : colour::textDim),
                  visage::Font::kRight, 9.8f, Weight::medium);

        const auto plot = plotRect();
        canvas.setColor(0xff080b10u);
        canvas.roundedRectangle(plot.x, plot.y, plot.w, plot.h, context_.px(metric::radiusSmall));
        canvas.setColor(colour::edge);
        canvas.roundedRectangleBorder(plot.x, plot.y, plot.w, plot.h, context_.px(metric::radiusSmall),
                                      context_.px(1.0f));

        if (modeIdx == 0)
            drawSpiralStage(canvas, plot, tel);
        else
            drawCopedentStage(canvas, plot, tel, modeIdx == 2);
    }

  private:
    struct Rect {
        float x, y, w, h;
    };

    Rect plotRect() const {
        const float pad = context_.px(14.0f);
        const float top = context_.px(36.0f);
        const float bot = context_.px(26.0f);
        return {pad, top, std::max(40.0f, width() - 2.0f * pad), std::max(40.0f, height() - top - bot)};
    }

    static constexpr float kMinOffset = -14.0f;
    static constexpr float kMaxOffset = 34.0f;

    float offsetToY(const Rect& r, float offset) const {
        const float norm = std::clamp((offset - kMinOffset) / (kMaxOffset - kMinOffset), 0.0f, 1.0f);
        return r.y + r.h - context_.px(12.0f) - norm * (r.h - context_.px(24.0f));
    }

    float yToOffset(const Rect& r, float y) const {
        const float innerH = std::max(1.0f, r.h - context_.px(24.0f));
        const float norm = std::clamp((r.y + r.h - context_.px(12.0f) - y) / innerH, 0.0f, 1.0f);
        return kMinOffset + norm * (kMaxOffset - kMinOffset);
    }

    std::pair<int, int> hitTestNode(const Rect& plot, bool mobius, visage::Point pt) const {
        const int segTotal = mobius ? kSceneCount : (kSceneCount - 1);
        const auto mat = context_.model.sceneMatrix();
        const float hitR = context_.px(14.0f);
        int bestS = -1, bestV = -1;
        float bestD2 = hitR * hitR;
        for (int s = 0; s < kSceneCount; ++s) {
            const float sx = plot.x + (static_cast<float>(s) / static_cast<float>(segTotal)) * plot.w;
            for (int v = 0; v < kVoiceCount; ++v) {
                const float sy = offsetToY(plot, mat.offsets[static_cast<std::size_t>(s)][static_cast<std::size_t>(v)]);
                const float dx = pt.x - sx;
                const float dy = pt.y - sy;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    bestS = s;
                    bestV = v;
                }
            }
        }
        return {bestS, bestV};
    }

    void applyBellDrag(visage::Point pt) {
        const auto plot = plotRect();
        const float nx = std::clamp((pt.x - plot.x) / std::max(1.0f, plot.w), 0.0f, 1.0f);
        const float ny = std::clamp(1.0f - (pt.y - plot.y) / std::max(1.0f, plot.h), 0.0f, 1.0f);
        context_.model.set(ParamId::bellCenter, fromNormalised(specOf(ParamId::bellCenter), nx));
        context_.model.set(ParamId::bellWidth, fromNormalised(specOf(ParamId::bellWidth), ny));
    }

    void applyMorphScrub(visage::Point pt) {
        const auto plot = plotRect();
        const float nx = std::clamp((pt.x - plot.x) / std::max(1.0f, plot.w), 0.0f, 1.0f);
        context_.model.set(ParamId::morph, nx * 100.0f);
    }

    void drawSpiralStage(visage::Canvas& canvas, const Rect& r, const TelemetrySnapshot& tel) {
        const int rootPc = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::rootKey))), 0, 11);
        const float subAnchor = context_.model.get(ParamId::subAnchor) * 0.01f;
        const float bellCenterHz = tel.sounding ? tel.effectiveBellHz : context_.model.get(ParamId::bellCenter);
        const float bellWidth = context_.model.get(ParamId::bellWidth) * 0.01f;
        const int ratioIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::spiralRatio))), 0, 4);
        const double ratioSemi = kSpiralRatioSemitones[static_cast<std::size_t>(ratioIdx)];
        const SpiralChord chord =
            spiralChordForStack(static_cast<int>(std::lround(context_.model.get(ParamId::spiralStack))));

        // Shaded Sub-Anchor Floor zone
        if (subAnchor > 0.02f) {
            const float sx1 = r.x + (0.65f / 7.0f) * r.w;
            const float sx2 = r.x + (2.15f / 7.0f) * r.w;
            const float subGlow = std::clamp(tel.subPeak * 0.14f, 0.0f, 0.14f);
            canvas.setColor(colour::withAlpha(colour::mobius, 0.07f + 0.10f * subAnchor + subGlow));
            canvas.rectangle(sx1, r.y + context_.px(1.0f), sx2 - sx1, r.h - context_.px(2.0f));
            drawLabel(canvas, context_, "SUB-ANCHOR MONO FLOOR", sx1 + context_.px(6.0f), r.y + context_.px(6.0f),
                      sx2 - sx1 + context_.px(44.0f), context_.px(14.0f), colour::withAlpha(colour::mobius, 0.88f),
                      visage::Font::kLeft, 9.0f, Weight::semibold);
        }

        // Vertical octave gridlines & frequency footer labels
        const double baseMidi = 12.0 + static_cast<double>(rootPc);
        for (int o = 0; o <= kOctaveCount; ++o) {
            const float gx = r.x + (static_cast<float>(o) / static_cast<float>(kOctaveCount)) * r.w;
            canvas.setColor(colour::withAlpha(colour::text, 0.07f));
            canvas.rectangle(gx, r.y, context_.px(1.0f), r.h);

            const double hz = 440.0 * std::pow(2.0, (baseMidi + static_cast<double>(o) * ratioSemi - 69.0) / 12.0);
            char lbl[24]{};
            if (hz >= 1000.0)
                std::snprintf(lbl, sizeof(lbl), "%.1fk", hz / 1000.0);
            else
                std::snprintf(lbl, sizeof(lbl), "%.0f Hz", hz);
            drawLabel(canvas, context_, lbl, gx - context_.px(24.0f), r.y + r.h + context_.px(4.0f), context_.px(48.0f),
                      context_.px(16.0f), colour::textDim, visage::Font::kCenter, 9.2f, Weight::regular);
        }

        // Spectral bell curve with live audio RMS shimmer
        const double baseHz = 440.0 * std::pow(2.0, (baseMidi - 69.0) / 12.0);
        const double centerU = std::clamp(
            (12.0 * std::log2(std::max(16.0, static_cast<double>(bellCenterHz)) / baseHz)) / ratioSemi, 0.75, 6.25);
        const double sigma = 0.65 + static_cast<double>(bellWidth) * 2.25;

        std::array<float, 128> bellVals{};
        for (std::size_t i = 0; i < bellVals.size(); ++i) {
            const double u = (static_cast<double>(i) / static_cast<double>(bellVals.size() - 1)) * 7.0;
            const double edge = std::sin(hba::dsp::kPi * (u / 7.0));
            const double tilt = std::exp(-0.5 * ((u - centerU) / sigma) * ((u - centerU) / sigma));
            const double hz = baseHz * std::pow(2.0, (u * ratioSemi) / 12.0);
            const double subW = (hz >= 18.0 && hz <= 180.0)
                                    ? std::exp(-0.5 * std::pow(std::log2(hz / 52.0) / 0.44, 2.0)) * subAnchor * 0.42
                                    : 0.0;
            bellVals[i] = static_cast<float>(std::clamp(edge * edge * tilt * 0.88 + subW, 0.0, 1.0));
        }

        const float rmsGlow = 0.14f + std::clamp(tel.rmsLevel * 0.28f, 0.0f, 0.22f);
        hba::ui::drawCurve(canvas, context_.scale, bellVals, r.x, r.y + context_.px(14.0f), r.w,
                           r.h - context_.px(18.0f), colour::spiral, 0.0f, 1.0f, rmsGlow, 0.0f, 2.0f);

        // Animated gliding octave & chordal partial pillars (pulsing cleanly with Risset Gate!)
        const float gateVis = 0.45f + 0.55f * std::clamp(tel.gateLevel, 0.0f, 1.0f);
        for (int c = 0; c < chord.count; ++c) {
            const unsigned baseCol =
                c == 0 ? colour::spiral : colour::voices[static_cast<std::size_t>(c % kVoiceCount)];
            const double chordShiftU = chord.intervals[static_cast<std::size_t>(c)] / ratioSemi;
            for (int k = 0; k < kOctaveCount; ++k) {
                const double uBase =
                    hba::dsp::frac((static_cast<double>(k) + static_cast<double>(tel.spiralPos)) / 7.0) * 7.0;
                const double u = uBase + chordShiftU;
                if (u < 0.0 || u > 7.0)
                    continue;
                const double edge = std::sin(hba::dsp::kPi * (uBase / 7.0));
                const double tilt = std::exp(-0.5 * ((uBase - centerU) / sigma) * ((uBase - centerU) / sigma));
                const float amp = static_cast<float>(std::clamp(edge * edge * tilt * 0.92, 0.0, 1.0));
                const bool inSubZone = (c == 0 && subAnchor > 0.05f && u >= 0.65 && u <= 2.15);
                const unsigned col = inSubZone ? colour::mobius : baseCol;

                const float px = r.x + static_cast<float>(u / 7.0) * r.w;
                const float barH = amp * (r.h - context_.px(22.0f)) * (inSubZone ? 1.0f : gateVis);
                const float topY = r.y + r.h - context_.px(4.0f) - barH;
                const float barW = context_.px(c == 0 ? 4.0f : 2.6f);

                canvas.setColor(colour::withAlpha(col, 0.25f + 0.72f * amp));
                canvas.roundedRectangle(px - barW * 0.5f, topY, barW, barH, barW * 0.5f);

                const float dot = context_.px(c == 0 ? 8.0f : 5.5f);
                canvas.setColor(col);
                canvas.circle(px - dot * 0.5f, topY - dot * 0.5f, dot);
            }
        }

        // Crosshair + Hz readout pill while dragging the Spectral Bell directly!
        if (draggingBell_) {
            const float cx = std::clamp(bellDragPos_.x, r.x, r.x + r.w);
            const float cy = std::clamp(bellDragPos_.y, r.y, r.y + r.h);
            canvas.setColor(colour::withAlpha(colour::text, 0.35f));
            canvas.rectangle(cx, r.y, context_.px(1.0f), r.h);
            canvas.rectangle(r.x, cy, r.w, context_.px(1.0f));
            char tip[64]{};
            std::snprintf(tip, sizeof(tip), "Bell %.0f Hz · Width %.0f%%", static_cast<double>(bellCenterHz),
                          static_cast<double>(bellWidth * 100.0f));
            const float tw = context_.px(156.0f);
            const float th = context_.px(20.0f);
            const float tx =
                std::clamp(cx + context_.px(10.0f), r.x + context_.px(4.0f), r.x + r.w - tw - context_.px(4.0f));
            const float ty =
                std::clamp(cy - th - context_.px(6.0f), r.y + context_.px(4.0f), r.y + r.h - th - context_.px(4.0f));
            canvas.setColor(colour::withAlpha(colour::window, 0.92f));
            canvas.roundedRectangle(tx, ty, tw, th, context_.px(4.0f));
            canvas.setColor(colour::spiral);
            canvas.roundedRectangleBorder(tx, ty, tw, th, context_.px(4.0f), context_.px(1.0f));
            drawLabel(canvas, context_, tip, tx, ty, tw, th, colour::text, visage::Font::kCenter, 9.4f,
                      Weight::semibold);
        }

        // Bar progress ribbon at bottom edge of scope
        const float progH = context_.px(3.0f);
        canvas.setColor(colour::withAlpha(colour::spiral, 0.22f));
        canvas.rectangle(r.x, r.y + r.h - progH, r.w, progH);
        canvas.setColor(colour::spiral);
        canvas.rectangle(r.x, r.y + r.h - progH, r.w * std::clamp(tel.barPhase, 0.0f, 1.0f), progH);
    }

    void drawCopedentStage(visage::Canvas& canvas, const Rect& r, const TelemetrySnapshot& tel, bool mobius) {
        const int rootPc = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::rootKey))), 0, 11);
        const int scaleIdx =
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::scale))), 0, kScaleCount - 1);
        const double gravity = static_cast<double>(context_.model.get(ParamId::gravity)) * 0.01;
        const auto gravMode = static_cast<GravityMode>(
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::gravityMode))), 0, 2));
        const double stagger = static_cast<double>(context_.model.get(ParamId::stagger)) * 0.01;
        const auto order = static_cast<StaggerOrder>(
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::staggerOrder))), 0, 3));
        const bool anchorSub = context_.model.get(ParamId::anchorSub) >= 0.5f;
        const SceneMatrix mat = context_.model.sceneMatrix();
        const int packIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::voicingPack))), 0,
                                       kVoicingPackCount - 1);
        const auto& packInfo = voicingPacks()[static_cast<std::size_t>(packIdx)];
        const unsigned modeCol = mobius ? colour::mobius : colour::copedent;
        const int activeNodeS = dragScene_ >= 0 ? dragScene_ : hoverScene_;
        const int activeNodeV = dragVoice_ >= 0 ? dragVoice_ : hoverVoice_;
        const int highlightOff =
            (activeNodeS >= 0 && activeNodeV >= 0)
                ? static_cast<int>(std::lround(
                      mat.offsets[static_cast<std::size_t>(activeNodeS)][static_cast<std::size_t>(activeNodeV)]))
                : -999;

        // Horizontal Scale Gravity Magnetic Rails!
        const std::uint16_t mask = scaleMask(scaleIdx);
        for (int off = static_cast<int>(kMinOffset); off <= static_cast<int>(kMaxOffset); ++off) {
            const int deg = ((off % 12) + 12) % 12;
            const bool isHovered = (off == highlightOff);
            if (!isHovered && ((mask >> deg) & 1u) == 0u)
                continue;
            const float y = offsetToY(r, static_cast<float>(off));
            const bool isRoot = (deg == 0);
            const float alpha =
                isHovered ? 0.55f : ((isRoot ? 0.14f : 0.05f) + static_cast<float>(gravity) * (isRoot ? 0.22f : 0.13f));
            canvas.setColor(colour::withAlpha(isHovered ? colour::text : modeCol, alpha));
            canvas.rectangle(r.x, y, r.w, context_.px(isHovered ? 1.6f : (isRoot ? 1.4f : 0.8f)));
        }

        // Scene vertical dividers A / B / C / D (+ A+12 in Mobius)
        const int segTotal = mobius ? kSceneCount : (kSceneCount - 1);
        for (int s = 0; s <= segTotal; ++s) {
            const float sx = r.x + (static_cast<float>(s) / static_cast<float>(segTotal)) * r.w;
            canvas.setColor(colour::withAlpha(colour::text, 0.14f));
            canvas.rectangle(sx, r.y, context_.px(1.0f), r.h);

            std::string label;
            if (s < kSceneCount) {
                const char letter = static_cast<char>('A' + s);
                label = std::string(1, letter) +
                        (mat.isCustom ? " · Custom"
                                      : (" · " + std::string(packInfo.sceneLabels[static_cast<std::size_t>(s)])));
            } else {
                label = "A (+12 st wrap)";
            }
            const float lw = context_.px(124.0f);
            const float lx = std::clamp(sx - lw * 0.5f, r.x, r.x + r.w - lw);
            const bool isCur = (s % kSceneCount == tel.activeScene);
            drawLabel(canvas, context_, label, lx, r.y + r.h + context_.px(4.0f), lw, context_.px(16.0f),
                      isCur ? modeCol : colour::textDim, visage::Font::kCenter, 9.2f,
                      isCur ? Weight::semibold : Weight::medium);
        }

        // Sample and stroke all 5 continuous voice-leading ribbons across morph [0..1]
        constexpr int kSteps = 96;
        const double refRoot = 36.0 + static_cast<double>(rootPc);
        for (int v = 0; v < kVoiceCount; ++v) {
            const auto vi = static_cast<std::size_t>(v);
            visage::Path ribbon;
            for (int i = 0; i <= kSteps; ++i) {
                const double mRaw = static_cast<double>(i) / static_cast<double>(kSteps);
                const double m = mobius ? std::min(mRaw, 0.9999) : mRaw;
                const auto eval = evaluateMorphPitches(mat, refRoot, rootPc, scaleIdx, m, gravity, gravMode, stagger,
                                                       order, anchorSub, mobius);
                const float x = (static_cast<float>(i) / static_cast<float>(kSteps)) * r.w;
                const float y = offsetToY(r, static_cast<float>(eval.pitches[vi] - refRoot)) - r.y;
                if (i == 0)
                    ribbon.moveTo(x, y);
                else
                    ribbon.lineTo(x, y);
            }
            canvas.setColor(colour::withAlpha(colour::voices[vi], v == 0 ? 0.95f : 0.82f));
            canvas.stroke(ribbon, r.x, r.y, r.w, r.h, context_.px(v == 0 ? 2.4f : 1.75f));
        }

        // Draggable scene voice target nodes (4 scenes x 5 voices)
        for (int s = 0; s < kSceneCount; ++s) {
            const float sx = r.x + (static_cast<float>(s) / static_cast<float>(segTotal)) * r.w;
            for (int v = 0; v < kVoiceCount; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const float sy = offsetToY(r, mat.offsets[static_cast<std::size_t>(s)][vi]);
                const bool focused = (activeNodeS == s && activeNodeV == v);
                const float d = context_.px(focused ? 11.5f : 7.5f);
                canvas.setColor(0xff080b10u);
                canvas.circle(sx - d * 0.5f - context_.px(1.5f), sy - d * 0.5f - context_.px(1.5f),
                              d + context_.px(3.0f));
                canvas.setColor(focused ? colour::text : colour::voices[vi]);
                canvas.circle(sx - d * 0.5f, sy - d * 0.5f, d);
            }
        }

        // Live playhead + glowing pitch beads
        const float liveMorph = std::clamp(tel.morphPos, 0.0f, 1.0f);
        const float px = r.x + liveMorph * r.w;
        canvas.setColor(colour::withAlpha(colour::text, 0.62f));
        canvas.rectangle(px - context_.px(0.75f), r.y, context_.px(1.5f), r.h);

        const auto liveEval = evaluateMorphPitches(mat, refRoot, rootPc, scaleIdx, static_cast<double>(liveMorph),
                                                   gravity, gravMode, stagger, order, anchorSub, mobius);
        std::string chordReadout;
        for (int v = 0; v < kVoiceCount; ++v) {
            const auto vi = static_cast<std::size_t>(v);
            const float off = static_cast<float>(liveEval.pitches[vi] - refRoot);
            const float by = offsetToY(r, off);
            const float bd = context_.px(v == 0 ? 11.5f : 9.5f);
            canvas.setColor(colour::withAlpha(colour::voices[vi], 0.32f));
            canvas.circle(px - bd * 0.85f, by - bd * 0.85f, bd * 1.7f);
            canvas.setColor(colour::voices[vi]);
            canvas.circle(px - bd * 0.5f, by - bd * 0.5f, bd);
            if (v > 0)
                chordReadout += " · ";
            chordReadout += midiNoteName(static_cast<float>(liveEval.pitches[vi]));
        }

        // Note chord readout pill top-left inside scope
        const float pillW = context_.px(214.0f);
        const float pillH = context_.px(20.0f);
        canvas.setColor(colour::withAlpha(colour::window, 0.86f));
        canvas.roundedRectangle(r.x + context_.px(8.0f), r.y + context_.px(6.0f), pillW, pillH, context_.px(4.0f));
        drawLabel(canvas, context_, chordReadout, r.x + context_.px(14.0f), r.y + context_.px(6.0f),
                  pillW - context_.px(12.0f), pillH, colour::text, visage::Font::kLeft, 9.6f, Weight::semibold);

        // Floating tooltip pill when hovering or dragging a voice node!
        if (activeNodeS >= 0 && activeNodeV >= 0) {
            const float sx = r.x + (static_cast<float>(activeNodeS) / static_cast<float>(segTotal)) * r.w;
            const float off = mat.offsets[static_cast<std::size_t>(activeNodeS)][static_cast<std::size_t>(activeNodeV)];
            const float sy = offsetToY(r, off);
            const std::string nName = midiNoteName(static_cast<float>(refRoot) + off);
            char tip[48]{};
            std::snprintf(tip, sizeof(tip), "V%d · %+.1f st · %s", activeNodeV + 1, static_cast<double>(off),
                          nName.c_str());
            const float tw = context_.px(128.0f);
            const float th = context_.px(20.0f);
            const float tx =
                std::clamp(sx + context_.px(10.0f), r.x + context_.px(4.0f), r.x + r.w - tw - context_.px(4.0f));
            const float ty =
                std::clamp(sy - th - context_.px(6.0f), r.y + context_.px(4.0f), r.y + r.h - th - context_.px(4.0f));
            canvas.setColor(colour::withAlpha(colour::window, 0.94f));
            canvas.roundedRectangle(tx, ty, tw, th, context_.px(4.0f));
            canvas.setColor(colour::voices[static_cast<std::size_t>(activeNodeV)]);
            canvas.roundedRectangleBorder(tx, ty, tw, th, context_.px(4.0f), context_.px(1.0f));
            drawLabel(canvas, context_, tip, tx, ty, tw, th, colour::text, visage::Font::kCenter, 9.2f,
                      Weight::semibold);
        }
    }

    ActionButton capA_, capB_, capC_, capD_;
    ActionButton optBtn_;
    ActionButton resetBtn_;
    bool draggingBell_ = false;
    bool scrubbingMorph_ = false;
    visage::Point bellDragPos_{};
    int dragScene_ = -1;
    int dragVoice_ = -1;
    int hoverScene_ = -1;
    int hoverVoice_ = -1;
    SceneMatrix dragMatrix_{};
};
} // namespace

std::unique_ptr<Panel> makeStagePanel(Context& ctx) { return std::make_unique<StagePanel>(ctx); }

} // namespace tg::ui
