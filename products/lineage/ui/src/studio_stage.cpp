// SPDX-License-Identifier: AGPL-3.0-or-later
// STUDIO Stage: dual interactive Filter/Sculpt spectrum canvas + Gen-2 Strata waveform scrubber.
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace ln::ui {

namespace {
constexpr float kMinFreq = 24.0f;
constexpr float kMaxFreq = 18000.0f;
constexpr int kCurvePts = 128;

float freqToNorm(float hz) {
    const float clamped = std::clamp(hz, kMinFreq, kMaxFreq);
    return std::log(clamped / kMinFreq) / std::log(kMaxFreq / kMinFreq);
}

float normToFreq(float norm) { return kMinFreq * std::pow(kMaxFreq / kMinFreq, std::clamp(norm, 0.0f, 1.0f)); }

// Visual approximation of Filter 1 & Filter 2 magnitude responses in [0, 1] display height
float evalFilter1Visual(Filt1Mode mode, float f, float fc, float res) {
    const float r = std::max(f / std::max(fc, 12.0f), 1e-4f);
    const float lr = std::log2(r);
    const float qBoost = res * 0.44f;
    switch (mode) {
    case Filt1Mode::lp24: {
        const float mag = 1.0f / std::sqrt(1.0f + std::pow(r, 8.0f));
        const float peak = qBoost * std::exp(-lr * lr * 16.0f);
        return std::clamp(0.14f + 0.64f * mag + peak, 0.05f, 0.96f);
    }
    case Filt1Mode::bp12: {
        const float width = 1.6f + res * 5.0f;
        const float bell = std::exp(-lr * lr * width);
        return std::clamp(0.12f + (0.68f + qBoost * 0.45f) * bell, 0.05f, 0.96f);
    }
    case Filt1Mode::notch: {
        const float width = 4.5f + res * 10.0f;
        const float dip = std::exp(-lr * lr * width);
        const float rim = qBoost * 0.35f * std::exp(-(std::abs(lr) - 0.55f) * (std::abs(lr) - 0.55f) * 14.0f);
        return std::clamp(0.70f - 0.58f * dip + rim, 0.06f, 0.94f);
    }
    case Filt1Mode::comb: {
        const float ripples = 0.5f + 0.5f * std::cos(f / std::max(fc * 0.35f, 12.0f) * 6.2831853f);
        const float depth = 0.28f + res * 0.48f;
        return std::clamp(0.42f + (ripples - 0.5f) * depth * 1.4f, 0.08f, 0.94f);
    }
    case Filt1Mode::formant: {
        const float fA = fc * 0.55f;
        const float fB = fc * 1.65f;
        const float fC = fc * 3.10f;
        const float lA = std::log2(std::max(f / std::max(fA, 20.0f), 1e-4f));
        const float lB = std::log2(std::max(f / std::max(fB, 20.0f), 1e-4f));
        const float lC = std::log2(std::max(f / std::max(fC, 20.0f), 1e-4f));
        const float w = 4.0f + res * 7.0f;
        const float sum =
            0.55f * std::exp(-lA * lA * w) + 0.72f * std::exp(-lB * lB * w) + 0.42f * std::exp(-lC * lC * (w * 1.2f));
        return std::clamp(0.18f + sum * (0.72f + qBoost * 0.45f), 0.06f, 0.96f);
    }
    }
    return 0.5f;
}

float evalFilter2Visual(Filt2Mode mode, float f, float fc, float res) {
    const float r = std::max(f / std::max(fc, 20.0f), 1e-4f);
    const float lr = std::log2(r);
    const float qBoost = res * 0.38f;
    switch (mode) {
    case Filt2Mode::notch: {
        const float dip = std::exp(-lr * lr * (5.5f + res * 12.0f));
        return std::clamp(0.68f - 0.56f * dip, 0.06f, 0.94f);
    }
    case Filt2Mode::dualNotch: {
        const float l1 = std::log2(std::max(f / std::max(fc * 0.68f, 20.0f), 1e-4f));
        const float l2 = std::log2(std::max(f / std::max(fc * 1.52f, 20.0f), 1e-4f));
        const float w = 7.0f + res * 12.0f;
        const float dip = std::max(std::exp(-l1 * l1 * w), std::exp(-l2 * l2 * w));
        return std::clamp(0.70f - 0.58f * dip, 0.06f, 0.94f);
    }
    case Filt2Mode::lp12: {
        const float mag = 1.0f / std::sqrt(1.0f + std::pow(r, 4.0f));
        const float peak = qBoost * std::exp(-lr * lr * 12.0f);
        return std::clamp(0.14f + 0.62f * mag + peak, 0.06f, 0.95f);
    }
    case Filt2Mode::bp12: {
        const float bell = std::exp(-lr * lr * (2.2f + res * 6.0f));
        return std::clamp(0.14f + (0.64f + qBoost * 0.4f) * bell, 0.06f, 0.95f);
    }
    case Filt2Mode::phaser: {
        const float phase = lr * 9.5f;
        const float comb = 0.5f + 0.5f * std::cos(phase);
        return std::clamp(0.46f + (comb - 0.5f) * (0.28f + res * 0.48f), 0.08f, 0.92f);
    }
    }
    return 0.5f;
}

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

class StudioStage final : public Panel {
  public:
    explicit StudioStage(Context& ctx)
        : Panel(ctx), printBtn_(ctx, "PRINT GEN", [this] { context_.model.triggerPrint(false); }),
          stackBtn_(ctx, "STACK +1", [this] { context_.model.triggerPrint(true); }),
          stepBackBtn_(ctx, "\xe2\x86\xa9", [this] { context_.model.stepBackPrint(); }),
          loadBtn_(ctx, "LOAD WAV", [this] { context_.model.browseLoadWav(); }),
          clearBtn_(ctx, "CLEAR", [this] { context_.model.clearPrint(); }) {
        printBtn_.setAccent(colour::strata);
        stackBtn_.setAccent(colour::sculpt);
        stepBackBtn_.setAccent(colour::textFaint);
        loadBtn_.setAccent(colour::reese);
        clearBtn_.setAccent(colour::textDim);
        addChild(&printBtn_);
        addChild(&stackBtn_);
        addChild(&stepBackBtn_);
        addChild(&loadBtn_);
        addChild(&clearBtn_);
    }

    void tick() override {
        const auto pstate = context_.model.printState();
        const int gen = context_.model.printGeneration();
        if (pstate == PrintState::printing) {
            printBtn_.setLabel("Printing...");
            printBtn_.setActive(false);
        } else {
            printBtn_.setLabel(gen >= 2 ? "RE-PRINT" : "PRINT GEN");
            printBtn_.setActive(gen >= 2);
        }
        stackBtn_.setActive(gen >= 3);
        stepBackBtn_.setAccent(context_.model.canStepBackPrint() ? colour::sculpt : colour::textFaint);
        redraw();
    }

    void resized() override {
        const float pad = px(10.0f);
        const float gap = px(10.0f);
        const float innerW = width() - 2.0f * pad - gap;
        const float leftW = std::floor(innerW * 0.52f);
        const float rightW = innerW - leftW;
        const float innerH = height() - 2.0f * pad;

        specRect_ = {pad, pad, leftW, innerH};
        strataRect_ = {pad + leftW + gap, pad, rightW, innerH};

        // Top-right button strip inside Strata canvas
        const float bh = px(24.0f);
        const float by = strataRect_.y + px(8.0f);
        const float bgap = px(4.0f);
        float rx = strataRect_.x + strataRect_.w - px(8.0f);

        const float clearW = px(50.0f);
        const float loadW = px(74.0f);
        const float backW = px(26.0f);
        const float stackW = px(68.0f);
        const float printW = px(76.0f);

        clearBtn_.setBounds(rx - clearW, by, clearW, bh);
        rx -= clearW + bgap;
        loadBtn_.setBounds(rx - loadW, by, loadW, bh);
        rx -= loadW + bgap;
        stepBackBtn_.setBounds(rx - backW, by, backW, bh);
        rx -= backW + bgap;
        stackBtn_.setBounds(rx - stackW, by, stackW, bh);
        rx -= stackW + bgap;
        printBtn_.setBounds(rx - printW, by, printW, bh);
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawSpectrumWell(canvas);
        drawStrataWell(canvas);
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const float mx = e.position.x;
        const float my = e.position.y;

        if (specRect_.contains(mx, my)) {
            const Rect plot = spectrumPlotRect();
            const float subX = plot.x + freqToNorm(context_.model.get(ParamId::subSplit)) * plot.w;
            const float f1x = plot.x + freqToNorm(context_.model.get(ParamId::filt1Hz)) * plot.w;
            const float f1y = plot.y + (1.0f - context_.model.get(ParamId::filt1Res)) * plot.h;
            const float f2x = plot.x + freqToNorm(context_.model.get(ParamId::filt2Hz)) * plot.w;
            const float f2y = plot.y + (1.0f - context_.model.get(ParamId::filt2Res)) * plot.h;
            const float d1 = std::hypot(mx - f1x, my - f1y);
            const float d2 = std::hypot(mx - f2x, my - f2y);
            const float dSub = std::abs(mx - subX);

            if (dSub <= px(12.0f) && dSub < std::min(d1, d2)) {
                dragMode_ = DragMode::subSplit;
                context_.model.begin(ParamId::subSplit);
                applySpectrumDrag(mx, my);
                return;
            }
            dragMode_ = (d2 < d1) ? DragMode::filt2 : DragMode::filt1;
            if (dragMode_ == DragMode::filt1) {
                context_.model.begin(ParamId::filt1Hz);
                context_.model.begin(ParamId::filt1Res);
            } else {
                context_.model.begin(ParamId::filt2Hz);
                context_.model.begin(ParamId::filt2Res);
            }
            applySpectrumDrag(mx, my);
            return;
        }

        const Rect wave = strataWaveRect();
        if (wave.contains(mx, my)) {
            if (e.isShiftDown()) {
                dragMode_ = DragMode::strataSpan;
                context_.model.begin(ParamId::strataSpan);
                applyStrataDrag(mx);
            } else {
                dragMode_ = DragMode::strataPos;
                context_.model.begin(ParamId::strataPos);
                applyStrataDrag(mx);
            }
        }
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragMode_ == DragMode::filt1 || dragMode_ == DragMode::filt2 || dragMode_ == DragMode::subSplit) {
            applySpectrumDrag(e.position.x, e.position.y);
        } else if (dragMode_ == DragMode::strataPos || dragMode_ == DragMode::strataSpan) {
            applyStrataDrag(e.position.x);
        }
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (dragMode_ == DragMode::filt1) {
            context_.model.end(ParamId::filt1Hz);
            context_.model.end(ParamId::filt1Res);
        } else if (dragMode_ == DragMode::filt2) {
            context_.model.end(ParamId::filt2Hz);
            context_.model.end(ParamId::filt2Res);
        } else if (dragMode_ == DragMode::subSplit) {
            context_.model.end(ParamId::subSplit);
        } else if (dragMode_ == DragMode::strataPos) {
            context_.model.end(ParamId::strataPos);
        } else if (dragMode_ == DragMode::strataSpan) {
            context_.model.end(ParamId::strataSpan);
        }
        dragMode_ = DragMode::none;
    }

  private:
    enum class DragMode { none, filt1, filt2, subSplit, strataPos, strataSpan };

    Rect spectrumPlotRect() const {
        const float padX = px(12.0f);
        const float top = px(34.0f);
        const float bot = px(20.0f);
        return {specRect_.x + padX, specRect_.y + top, std::max(40.0f, specRect_.w - 2.0f * padX),
                std::max(40.0f, specRect_.h - top - bot)};
    }

    Rect strataWaveRect() const {
        const float padX = px(12.0f);
        const float top = px(40.0f);
        const float bot = px(22.0f);
        return {strataRect_.x + padX, strataRect_.y + top, std::max(40.0f, strataRect_.w - 2.0f * padX),
                std::max(40.0f, strataRect_.h - top - bot)};
    }

    void applySpectrumDrag(float mx, float my) {
        const Rect plot = spectrumPlotRect();
        const float nx = std::clamp((mx - plot.x) / std::max(1.0f, plot.w), 0.0f, 1.0f);
        const float ny = std::clamp(1.0f - (my - plot.y) / std::max(1.0f, plot.h), 0.0f, 1.0f);
        const float hz = normToFreq(nx);
        if (dragMode_ == DragMode::filt1) {
            context_.model.set(ParamId::filt1Hz, clampToRange(spec(ParamId::filt1Hz), hz));
            context_.model.set(ParamId::filt1Res, ny);
        } else if (dragMode_ == DragMode::filt2) {
            context_.model.set(ParamId::filt2Hz, clampToRange(spec(ParamId::filt2Hz), hz));
            context_.model.set(ParamId::filt2Res, ny);
        } else if (dragMode_ == DragMode::subSplit) {
            context_.model.set(ParamId::subSplit, clampToRange(spec(ParamId::subSplit), hz));
        }
        redraw();
    }

    void applyStrataDrag(float mx) {
        const Rect wave = strataWaveRect();
        const float u = std::clamp((mx - wave.x) / std::max(1.0f, wave.w), 0.0f, 1.0f);
        if (dragMode_ == DragMode::strataPos) {
            context_.model.set(ParamId::strataPos, u);
        } else if (dragMode_ == DragMode::strataSpan) {
            const float start = context_.model.get(ParamId::strataPos);
            const float span = std::clamp(std::abs(u - start), 0.05f, 1.0f);
            context_.model.set(ParamId::strataSpan, span);
        }
        redraw();
    }

    void drawSpectrumWell(visage::Canvas& canvas) {
        const float sc = context_.scale;
        const auto& tel = context_.model.visuals();
        const Rect& r = specRect_;
        const Rect plot = spectrumPlotRect();

        drawPanel(canvas, context_, r.x, r.y, r.w, r.h, 0xff080b12u, metric::radiusSmall);
        canvas.setColor(colour::withAlpha(colour::sculpt, 0.28f));
        canvas.roundedRectangleBorder(r.x, r.y, r.w, r.h, metric::radiusSmall * sc, 1.0f * sc);

        // Header & live badges
        drawLabel(canvas, context_, "GEN 1 \xc2\xb7 SCULPT SPECTRUM", r.x + px(12.0f), r.y + px(8.0f), px(220.0f),
                  px(18.0f), colour::sculpt, visage::Font::kLeft, 9.6f, Weight::semibold);

        char stats[96];
        std::snprintf(stats, sizeof stats, "F1 %.0f Hz   \xc2\xb7   F2 %.0f Hz   \xc2\xb7   SUB %.0f Hz",
                      static_cast<double>(tel.effFilt1Hz), static_cast<double>(tel.effFilt2Hz),
                      static_cast<double>(context_.model.get(ParamId::subSplit)));
        drawLabel(canvas, context_, stats, r.x + r.w - px(290.0f), r.y + px(8.0f), px(278.0f), px(18.0f),
                  colour::textDim, visage::Font::kRight, 9.4f, Weight::medium);

        // Subtle log frequency gridlines + labels
        constexpr std::array<float, 6> kGridHz{50.0f, 100.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};
        constexpr std::array<const char*, 6> kGridLbl{"50", "100", "250", "1k", "4k", "12k"};
        for (std::size_t i = 0; i < kGridHz.size(); ++i) {
            const float gx = plot.x + freqToNorm(kGridHz[i]) * plot.w;
            canvas.setColor(colour::withAlpha(colour::edge, 0.55f));
            canvas.rectangle(gx, plot.y, 1.0f * sc, plot.h);
            drawLabel(canvas, context_, kGridLbl[i], gx - px(16.0f), plot.y + plot.h + px(2.0f), px(32.0f), px(14.0f),
                      colour::textFaint, visage::Font::kCenter, 8.6f);
        }

        // Sub-Split clean mono curtain (draggable emerald rail!)
        const float subSplitHz = context_.model.get(ParamId::subSplit);
        const float subX = plot.x + freqToNorm(subSplitHz) * plot.w;
        const bool subDrag = (dragMode_ == DragMode::subSplit);
        canvas.setColor(colour::withAlpha(colour::sub, subDrag ? 0.18f : 0.11f));
        canvas.rectangle(plot.x, plot.y, std::max(0.0f, subX - plot.x), plot.h);
        canvas.setColor(colour::withAlpha(colour::sub, subDrag ? 0.95f : 0.70f));
        canvas.rectangle(subX - (subDrag ? 1.25f : 0.75f) * sc, plot.y, (subDrag ? 2.5f : 1.5f) * sc, plot.h);
        // Subtle tactile thumb notch on the emerald sub rail
        canvas.roundedRectangle(subX - 3.0f * sc, plot.y + plot.h * 0.5f - 8.0f * sc, 6.0f * sc, 16.0f * sc, 3.0f * sc);
        drawLabel(canvas, context_, "MONO SUB", plot.x + px(4.0f), plot.y + px(4.0f), px(72.0f), px(13.0f),
                  colour::withAlpha(colour::sub, 0.88f), visage::Font::kLeft, 8.4f, Weight::semibold);

        // Optional Strata Band-Split indicator (460 Hz crossover in Hi-Strata / Lo-Strata modes)
        const int blendIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::strataBlend))), 0, 2);
        if (blendIdx != 0 && tel.hasPrint && tel.effGenMix > 0.02f) {
            const float splitX = plot.x + freqToNorm(460.0f) * plot.w;
            canvas.setColor(colour::withAlpha(colour::strata, 0.08f * tel.effGenMix));
            if (blendIdx == 1) {
                canvas.rectangle(splitX, plot.y, std::max(0.0f, plot.x + plot.w - splitX), plot.h);
                drawLabel(canvas, context_, "HI-STRATA", splitX + px(5.0f), plot.y + px(4.0f), px(72.0f), px(13.0f),
                          colour::withAlpha(colour::strata, 0.80f), visage::Font::kLeft, 8.2f, Weight::semibold);
            } else {
                canvas.rectangle(subX, plot.y, std::max(0.0f, splitX - subX), plot.h);
                drawLabel(canvas, context_, "LO-STRATA", subX + px(5.0f), plot.y + px(4.0f), px(72.0f), px(13.0f),
                          colour::withAlpha(colour::strata, 0.80f), visage::Font::kLeft, 8.2f, Weight::semibold);
            }
            canvas.setColor(colour::withAlpha(colour::strata, 0.50f));
            canvas.rectangle(splitX - 0.5f * sc, plot.y, 1.0f * sc, plot.h);
        }

        // Background live audio oscilloscope
        drawCurve(canvas, context_, tel.scope, plot.x, plot.y + plot.h * 0.18f, plot.w, plot.h * 0.64f,
                  colour::withAlpha(colour::text, 0.20f), -1.0f, 1.0f, 0.0f, 0.0f, 1.2f);

        // Compute & draw Filter 2 (cyan) and Filter 1 (amber) curves
        const auto f1Mode = static_cast<Filt1Mode>(
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::filt1Mode))), 0, 4));
        const auto f2Mode = static_cast<Filt2Mode>(
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::filt2Mode))), 0, 4));
        const float f1Res = context_.model.get(ParamId::filt1Res);
        const float f2Res = context_.model.get(ParamId::filt2Res);

        std::array<float, kCurvePts> c1{}, c2{};
        for (int i = 0; i < kCurvePts; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kCurvePts - 1);
            const float hz = normToFreq(u);
            c1[static_cast<std::size_t>(i)] = evalFilter1Visual(f1Mode, hz, tel.effFilt1Hz, f1Res);
            c2[static_cast<std::size_t>(i)] = evalFilter2Visual(f2Mode, hz, tel.effFilt2Hz, f2Res);
        }

        drawCurve(canvas, context_, c2, plot.x, plot.y, plot.w, plot.h, colour::reese, 0.0f, 1.0f, 0.12f, 0.0f, 1.8f);
        drawCurve(canvas, context_, c1, plot.x, plot.y, plot.w, plot.h, colour::sculpt, 0.0f, 1.0f, 0.16f, 0.0f, 2.2f);

        // Draggable interactive orbs for F1 & F2 (showing both base knob & live modulated ghost)
        const auto drawOrb = [&](float baseHz, float effHz, float res, unsigned col, const char* tag, bool active) {
            const float ex = plot.x + freqToNorm(effHz) * plot.w;
            const float bx = plot.x + freqToNorm(baseHz) * plot.w;
            const float y = plot.y + (1.0f - std::clamp(res, 0.0f, 1.0f)) * plot.h;

            // Modulated halo
            if (std::abs(ex - bx) > 2.0f * sc) {
                canvas.setColor(colour::withAlpha(col, 0.45f));
                canvas.segment(bx, y, ex, y, 1.5f * sc, true);
                const float md = 10.0f * sc;
                canvas.setColor(colour::withAlpha(col, 0.30f));
                canvas.circle(ex - md * 0.5f, y - md * 0.5f, md);
            }

            const float halo = (active ? 22.0f : 16.0f) * sc;
            canvas.setColor(colour::withAlpha(col, active ? 0.34f : 0.20f));
            canvas.circle(bx - halo * 0.5f, y - halo * 0.5f, halo);

            const float dot = 9.0f * sc;
            canvas.setColor(col);
            canvas.circle(bx - dot * 0.5f, y - dot * 0.5f, dot);
            canvas.setColor(0xffffffffu);
            canvas.circle(bx - 2.0f * sc, y - 2.0f * sc, 4.0f * sc);

            drawLabel(canvas, context_, tag, bx - px(14.0f), y - px(20.0f), px(28.0f), px(14.0f), col,
                      visage::Font::kCenter, 8.8f, Weight::semibold);
        };

        drawOrb(context_.model.get(ParamId::filt2Hz), tel.effFilt2Hz, f2Res, colour::reese, "F2",
                dragMode_ == DragMode::filt2);
        drawOrb(context_.model.get(ParamId::filt1Hz), tel.effFilt1Hz, f1Res, colour::sculpt, "F1",
                dragMode_ == DragMode::filt1);
    }

    void drawStrataWell(visage::Canvas& canvas) {
        const float sc = context_.scale;
        const auto& tel = context_.model.visuals();
        const auto& peaks = context_.model.printPeaks();
        const Rect& r = strataRect_;
        const Rect wave = strataWaveRect();

        drawPanel(canvas, context_, r.x, r.y, r.w, r.h, 0xff080b12u, metric::radiusSmall);
        canvas.setColor(colour::withAlpha(colour::strata, tel.hasPrint ? 0.42f : 0.20f));
        canvas.roundedRectangleBorder(r.x, r.y, r.w, r.h, metric::radiusSmall * sc, 1.0f * sc);

        // Header + Generation badge
        drawLabel(canvas, context_, "GEN 2 \xc2\xb7 STRATA BUFFER", r.x + px(12.0f), r.y + px(8.0f), px(170.0f),
                  px(16.0f), colour::strata, visage::Font::kLeft, 9.6f, Weight::semibold);
        drawLabel(canvas, context_, context_.model.printLabel(), r.x + px(12.0f), r.y + px(22.0f), px(220.0f),
                  px(14.0f), tel.hasPrint ? colour::text : colour::textDim, visage::Font::kLeft, 9.2f, Weight::medium);

        // Subtle waveform well interior
        drawPanel(canvas, context_, wave.x, wave.y, wave.w, wave.h, 0xff05070cu, metric::radiusSmall);
        const float wMid = wave.y + wave.h * 0.5f;
        canvas.setColor(colour::withAlpha(colour::edge, 0.5f));
        canvas.rectangle(wave.x, wMid, wave.w, 1.0f * sc);

        if (tel.hasPrint) {
            // Active loop aperture window
            const float lx0 = wave.x + tel.loopStartNorm * wave.w;
            const float lx1 = wave.x + tel.loopEndNorm * wave.w;
            const float lw = std::max(3.0f * sc, lx1 - lx0);
            canvas.setColor(colour::withAlpha(colour::strata, 0.14f));
            canvas.rectangle(lx0, wave.y, lw, wave.h);

            // 192-bar mirrored waveform
            constexpr int kN = PrintBuffer::kWaveformPoints;
            const float barW = std::max(1.5f * sc, wave.w / static_cast<float>(kN) - 0.6f * sc);
            for (int i = 0; i < kN; ++i) {
                const float u = static_cast<float>(i) / static_cast<float>(kN - 1);
                const float pk = peaks[static_cast<std::size_t>(i)];
                const bool inLoop = (u >= tel.loopStartNorm && u <= tel.loopEndNorm);
                const float amp = std::max(1.5f * sc, pk * wave.h * 0.46f);
                const unsigned col = inLoop ? colour::lerpArgb(colour::strata, colour::sculpt, pk * 0.45f)
                                            : colour::withAlpha(colour::textFaint, 0.35f);
                canvas.setColor(inLoop ? colour::withAlpha(col, 0.88f) : col);
                canvas.rectangle(wave.x + u * wave.w, wMid - amp, barW, 2.0f * amp);
            }

            // Glowing aperture rails
            canvas.setColor(colour::withAlpha(colour::strata, 0.85f));
            canvas.rectangle(lx0, wave.y, 1.5f * sc, wave.h);
            canvas.rectangle(lx1 - 1.5f * sc, wave.y, 1.5f * sc, wave.h);

            // Sweeping white-magenta laser playhead
            const float phX = wave.x + tel.playheadNorm * wave.w;
            canvas.setColor(colour::withAlpha(colour::strata, 0.38f));
            canvas.rectangle(phX - 3.0f * sc, wave.y, 6.0f * sc, wave.h);
            canvas.setColor(0xffffffffu);
            canvas.rectangle(phX - 0.75f * sc, wave.y, 1.5f * sc, wave.h);
        } else {
            drawLabel(canvas, context_, "Click PRINT GEN to bake Gen 0 + Gen 1 into a non-destructive Strata loop",
                      wave.x + px(12.0f), wave.y, wave.w - px(24.0f), wave.h, colour::textDim, visage::Font::kCenter,
                      10.2f, Weight::regular);
        }

        // Bottom hint & Gen Mix readout
        char footer[96];
        std::snprintf(footer, sizeof footer, "Drag: scrub Pos  \xc2\xb7  Shift+Drag: Span   |   Gen Mix %.0f%%",
                      static_cast<double>(tel.effGenMix * 100.0f));
        drawLabel(canvas, context_, footer, wave.x, wave.y + wave.h + px(4.0f), wave.w, px(15.0f), colour::textFaint,
                  visage::Font::kCenter, 8.8f, Weight::medium);
    }

    PillButton printBtn_, stackBtn_, stepBackBtn_, loadBtn_, clearBtn_;
    Rect specRect_{};
    Rect strataRect_{};
    DragMode dragMode_ = DragMode::none;
};
} // namespace

std::unique_ptr<Panel> makeStudioStage(Context& context) { return std::make_unique<StudioStage>(context); }

} // namespace ln::ui
