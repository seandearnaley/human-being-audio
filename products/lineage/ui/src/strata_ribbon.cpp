// SPDX-License-Identifier: AGPL-3.0-or-later
// Top 3-Generation Lineage Strip in PLAY view: GEN 0 Source -> GEN 1 Sculpt -> GEN 2 Live Resampled Strata.
#include "panels.h"

#include <algorithm>
#include <string>

namespace ln::ui {

namespace {
class StrataRibbon final : public Panel {
  public:
    explicit StrataRibbon(Context& ctx)
        : Panel(ctx), printBtn_(ctx, "PRINT GEN", [this] { context_.model.triggerPrint(false); }),
          stackBtn_(ctx, "STACK +1", [this] { context_.model.triggerPrint(true); }),
          stepBackBtn_(ctx, "\xe2\x86\xa9", [this] { context_.model.stepBackPrint(); }),
          loadBtn_(ctx, "LOAD WAV", [this] { context_.model.browseLoadWav(); }) {
        printBtn_.setAccent(colour::strata);
        stackBtn_.setAccent(colour::sculpt);
        stepBackBtn_.setAccent(colour::textFaint);
        loadBtn_.setAccent(colour::reese);
        addChild(&printBtn_);
        addChild(&stackBtn_);
        addChild(&stepBackBtn_);
        addChild(&loadBtn_);
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
        const float h = height();
        const float bh = px(28.0f);
        const float y = (h - bh) * 0.5f;
        const float gap = px(5.0f);
        float rx = width() - px(10.0f);
        const float loadW = px(76.0f);
        const float backW = px(28.0f);
        const float stackW = px(74.0f);
        const float printW = px(84.0f);
        loadBtn_.setBounds(rx - loadW, y, loadW, bh);
        rx -= loadW + gap;
        stepBackBtn_.setBounds(rx - backW, y, backW, bh);
        rx -= backW + gap;
        stackBtn_.setBounds(rx - stackW, y, stackW, bh);
        rx -= stackW + gap;
        printBtn_.setBounds(rx - printW, y, printW, bh);
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float sc = context_.scale;
        const auto& tel = context_.model.visuals();
        const auto& peaks = context_.model.printPeaks();

        // Left: GEN 0 Source Pill + Arrow + GEN 1 Sculpt Pill + Arrow
        const float pad = px(10.0f);
        const float pillH = px(34.0f);
        const float pillY = (height() - pillH) * 0.5f;

        const int coreIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::coreMode))), 0, 5);
        const int f1Idx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::filt1Mode))), 0, 4);
        const int drvIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::driveType))), 0, 4);

        const float g0W = px(152.0f);
        drawPanel(canvas, context_, pad, pillY, g0W, pillH, colour::raised, metric::radiusSmall);
        canvas.setColor(colour::withAlpha(colour::reese, 0.55f));
        canvas.rectangle(pad, pillY + px(4.0f), px(3.0f), pillH - px(8.0f));
        drawLabel(canvas, context_, "GEN 0 \xc2\xb7 SOURCE", pad + px(10.0f), pillY + px(3.0f), g0W - px(14.0f),
                  px(13.0f), colour::reese, visage::Font::kLeft, 8.8f, Weight::semibold);
        drawLabel(canvas, context_, std::string(choices::coreMode[static_cast<std::size_t>(coreIdx)]), pad + px(10.0f),
                  pillY + px(16.0f), g0W - px(14.0f), px(15.0f), colour::text, visage::Font::kLeft, 10.8f,
                  Weight::medium);

        // Connector 0 -> 1
        float cx = pad + g0W;
        drawLabel(canvas, context_, "\xe2\x86\x92", cx, pillY, px(18.0f), pillH, colour::textDim, visage::Font::kCenter,
                  12.0f);
        cx += px(18.0f);

        const float g1W = px(168.0f);
        drawPanel(canvas, context_, cx, pillY, g1W, pillH, colour::raised, metric::radiusSmall);
        canvas.setColor(colour::withAlpha(colour::sculpt, 0.65f));
        canvas.rectangle(cx, pillY + px(4.0f), px(3.0f), pillH - px(8.0f));
        drawLabel(canvas, context_, "GEN 1 \xc2\xb7 SCULPT", cx + px(10.0f), pillY + px(3.0f), g1W - px(14.0f),
                  px(13.0f), colour::sculpt, visage::Font::kLeft, 8.8f, Weight::semibold);
        const std::string g1Text = std::string(choices::filt1Mode[static_cast<std::size_t>(f1Idx)]) + " \xc2\xb7 " +
                                   std::string(choices::driveType[static_cast<std::size_t>(drvIdx)]);
        drawLabel(canvas, context_, g1Text, cx + px(10.0f), pillY + px(16.0f), g1W - px(14.0f), px(15.0f), colour::text,
                  visage::Font::kLeft, 10.8f, Weight::medium);

        cx += g1W;
        drawLabel(canvas, context_, "\xe2\x86\x92", cx, pillY, px(18.0f), pillH, colour::strata, visage::Font::kCenter,
                  12.0f);
        cx += px(18.0f);

        // Center-Right: Living Interactive Gen-2 Strata Mini-Waveform Well!
        const float rightEdge = width() - px(296.0f);
        const float waveW = std::max(px(120.0f), rightEdge - cx);
        waveBounds_ = {cx, pillY, waveW, pillH};
        drawPanel(canvas, context_, cx, pillY, waveW, pillH, 0xff090c13u, metric::radiusSmall);
        canvas.setColor(colour::withAlpha(colour::strata, tel.hasPrint ? 0.45f : 0.18f));
        canvas.roundedRectangleBorder(cx, pillY, waveW, pillH, metric::radiusSmall * sc, 1.0f * sc);

        // Label badge on left inside waveform well
        const std::string genLbl = context_.model.printLabel();
        const float badgeW = px(114.0f);
        drawLabel(canvas, context_, genLbl, cx + px(8.0f), pillY, badgeW, pillH,
                  tel.hasPrint ? colour::strata : colour::textDim, visage::Font::kLeft, 9.6f, Weight::semibold);

        // Mini waveform bars + active loop highlight + playhead
        const float wx = cx + badgeW + px(4.0f);
        const float ww = std::max(20.0f, waveW - badgeW - px(12.0f));
        const float wy = pillY + px(4.0f);
        const float wh = pillH - px(8.0f);
        const float wMid = wy + wh * 0.5f;

        if (tel.hasPrint) {
            const float lx0 = wx + tel.loopStartNorm * ww;
            const float lx1 = wx + tel.loopEndNorm * ww;
            canvas.setColor(colour::withAlpha(colour::strata, 0.14f));
            canvas.rectangle(lx0, wy, std::max(2.0f, lx1 - lx0), wh);

            constexpr int kN = PrintBuffer::kWaveformPoints;
            for (int i = 0; i < kN; i += 2) {
                const float u = static_cast<float>(i) / static_cast<float>(kN - 1);
                const float pk = peaks[static_cast<std::size_t>(i)];
                const bool inLoop = (u >= tel.loopStartNorm && u <= tel.loopEndNorm);
                const float amp = std::max(1.0f * sc, pk * wh * 0.46f);
                canvas.setColor(colour::withAlpha(inLoop ? colour::strata : colour::textFaint, inLoop ? 0.85f : 0.35f));
                canvas.rectangle(wx + u * ww, wMid - amp, std::max(1.2f * sc, ww / 96.0f), 2.0f * amp);
            }
            const float phX = wx + tel.playheadNorm * ww;
            canvas.setColor(0xffffffffu);
            canvas.rectangle(phX - 0.75f * sc, wy, 1.5f * sc, wh);
        } else {
            drawLabel(canvas, context_, "Click PRINT GEN to capture non-destructive Gen 2 strata", wx, pillY, ww, pillH,
                      colour::textDim, visage::Font::kCenter, 9.8f, Weight::regular);
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.position.x >= waveBounds_.x && e.position.x <= waveBounds_.x + waveBounds_.w &&
            e.position.y >= waveBounds_.y && e.position.y <= waveBounds_.y + waveBounds_.h) {
            scrubbing_ = true;
            context_.model.begin(ParamId::strataPos);
            updateScrub(e.position.x);
        }
    }
    void mouseDrag(const visage::MouseEvent& e) override {
        if (scrubbing_)
            updateScrub(e.position.x);
    }
    void mouseUp(const visage::MouseEvent&) override {
        if (scrubbing_) {
            scrubbing_ = false;
            context_.model.end(ParamId::strataPos);
        }
    }

  private:
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    void updateScrub(float x) {
        const float badgeW = px(108.0f);
        const float wx = waveBounds_.x + badgeW;
        const float ww = std::max(20.0f, waveBounds_.w - badgeW - px(8.0f));
        const float u = std::clamp((x - wx) / ww, 0.0f, 1.0f);
        context_.model.set(ParamId::strataPos, u);
        redraw();
    }

    PillButton printBtn_, stackBtn_, stepBackBtn_, loadBtn_;
    Rect waveBounds_{};
    bool scrubbing_ = false;
};
} // namespace

std::unique_ptr<Panel> makeStrataRibbon(Context& context) { return std::make_unique<StrataRibbon>(context); }

} // namespace ln::ui
