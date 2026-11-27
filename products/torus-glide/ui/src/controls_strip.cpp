// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <utility>

namespace tg::ui {

namespace {
class MeterStrip final : public visage::Frame, public Live {
  public:
    explicit MeterStrip(Context& ctx) : context_(ctx) { context_.live.push_back(this); }
    ~MeterStrip() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto& v = context_.model.visuals();
        l_ = std::max(v.peakL, l_ * 0.88f);
        r_ = std::max(v.peakR, r_ * 0.88f);
        sub_ = std::max(v.subPeak, sub_ * 0.88f);
        gr_ = v.limiterGain;
        redraw();
    }

    void draw(visage::Canvas& canvas) override {
        const float w = width();
        const float pad = context_.px(4.0f);
        const float barH = context_.px(5.0f);

        const auto drawBar = [&](float y, float val, unsigned col, const char* lbl) {
            drawLabel(canvas, context_, lbl, pad, y - context_.px(2.0f), context_.px(34.0f), context_.px(10.0f),
                      colour::textDim, visage::Font::kLeft, 8.6f, Weight::semibold);
            const float bx = pad + context_.px(36.0f);
            const float bw = std::max(8.0f, w - bx - pad);
            canvas.setColor(colour::track);
            canvas.roundedRectangle(bx, y, bw, barH, barH * 0.5f);
            const float fill = std::clamp(val, 0.0f, 1.0f) * bw;
            if (fill > 1.0f) {
                canvas.setColor(col);
                canvas.roundedRectangle(bx, y, fill, barH, barH * 0.5f);
            }
        };

        const float spacing = std::max(context_.px(10.0f), (height() - context_.px(10.0f)) * 0.25f);
        drawBar(context_.px(5.0f), l_, colour::spiral, "OUT L");
        drawBar(context_.px(5.0f) + spacing, r_, colour::spiral, "OUT R");
        drawBar(context_.px(5.0f) + spacing * 2.0f, sub_, colour::mobius, "SUB");
        drawBar(context_.px(5.0f) + spacing * 3.0f, 1.0f - std::clamp(gr_, 0.0f, 1.0f), colour::drive, "LIM");
    }

  private:
    Context& context_;
    float l_ = 0.0f, r_ = 0.0f, sub_ = 0.0f, gr_ = 1.0f;
};

class ControlsStrip final : public Panel, public Live {
  public:
    explicit ControlsStrip(Context& ctx)
        : Panel(ctx),
          // Spiral geometry controls
          bellCenter_(ctx, ParamId::bellCenter, colour::spiral, "Bell Freq"),
          bellWidth_(ctx, ParamId::bellWidth, colour::spiral, "Bell Width"),
          spiralDir_(ctx, ParamId::spiralDir, colour::spiral, "Direction"),
          loopBars_(ctx, ParamId::loopBars, colour::spiral, "Loop Bars"),
          spiralRatio_(ctx, ParamId::spiralRatio, colour::spiral, "Ratio"),
          spiralStack_(ctx, ParamId::spiralStack, colour::spiral, "Chord Stack"),
          phaseOffset_(ctx, ParamId::phaseOffset, colour::spiral, "Bar Offset"),

          // Copedent / Mobius geometry controls
          morph_(ctx, ParamId::morph, colour::copedent, "Morph"),
          gravity_(ctx, ParamId::gravity, colour::copedent, "Gravity"),
          stagger_(ctx, ParamId::stagger, colour::copedent, "Stagger"),
          voicingPack_(ctx, ParamId::voicingPack, colour::copedent, "Voicing"),
          gravityMode_(ctx, ParamId::gravityMode, colour::copedent, "Wells"),
          staggerOrder_(ctx, ParamId::staggerOrder, colour::copedent, "Order"),
          anchorSub_(ctx, ParamId::anchorSub, colour::mobius, "Anchor V1 Sub"),

          // Sound & Sub-Anchor Core
          subAnchor_(ctx, ParamId::subAnchor, colour::mobius, "Sub-Anchor"),
          drive_(ctx, ParamId::drive, colour::drive, "ADAA Drive"),
          filterHz_(ctx, ParamId::filterHz, colour::spiral, "Filter"),
          timbre_(ctx, ParamId::timbre, colour::spiral, "Timbre"),
          filterMode_(ctx, ParamId::filterMode, colour::spiral, "Filter"),
          detune_(ctx, ParamId::detune, colour::spiral, "Detune"),
          filterRes_(ctx, ParamId::filterRes, colour::drive, "Resonance"),
          width_(ctx, ParamId::width, colour::spiral, "Width"), space_(ctx, ParamId::space, colour::copedent, "Space"),
          subWarmth_(ctx, ParamId::subWarmth, colour::mobius, "Sub Warm"),
          subCutoff_(ctx, ParamId::subCutoff, colour::mobius, "Sub X-Over"),
          driveModel_(ctx, ParamId::driveModel, colour::drive, "Shaper"),
          rissetGate_(ctx, ParamId::rissetGate, colour::mobius, "Risset Gate"),

          // Envelope & Output (Retrig, Glide, Bend, Key Switch & OS live in the studio Voice footer bar)
          outputDb_(ctx, ParamId::outputDb, colour::text, "Output"),
          attackMs_(ctx, ParamId::attackMs, colour::textDim, "Attack"),
          releaseMs_(ctx, ParamId::releaseMs, colour::textDim, "Release"),
          morphAuto_(ctx, ParamId::morphAuto, colour::copedent, "Auto-Morph"),
          mpeOut_(ctx, ParamId::mpeOut, colour::copedent, "MPE Out"),
          limiter_(ctx, ParamId::limiter, colour::drive, "True-Peak"), meter_(ctx) {
        morph_.setModProvider([&m = context_.model] { return m.visuals().morphPos; });

        for (visage::Frame* c : std::initializer_list<visage::Frame*>{
                 &bellCenter_, &bellWidth_, &spiralDir_, &loopBars_,    &spiralRatio_, &spiralStack_,  &phaseOffset_,
                 &morph_,      &gravity_,   &stagger_,   &voicingPack_, &gravityMode_, &staggerOrder_, &anchorSub_,
                 &subAnchor_,  &drive_,     &filterHz_,  &timbre_,      &filterMode_,  &detune_,       &filterRes_,
                 &width_,      &space_,     &subWarmth_, &subCutoff_,   &driveModel_,  &rissetGate_,   &outputDb_,
                 &attackMs_,   &releaseMs_, &morphAuto_, &mpeOut_,      &limiter_,     &meter_}) {
            addChild(c);
        }

        context_.live.push_back(this);
        updateModeVisibility();
    }

    ~ControlsStrip() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        if (modeIdx != shownMode_) {
            shownMode_ = modeIdx;
            updateModeVisibility();
            resized();
            redraw();
        }
    }

    void updateModeVisibility() {
        const bool isSpiral = (shownMode_ == 0);
        const bool isMobius = (shownMode_ == 2);

        bellCenter_.setVisible(isSpiral);
        bellWidth_.setVisible(isSpiral);
        spiralDir_.setVisible(isSpiral || isMobius);
        loopBars_.setVisible(isSpiral || isMobius);
        spiralRatio_.setVisible(isSpiral);
        spiralStack_.setVisible(isSpiral);
        phaseOffset_.setVisible(isSpiral);

        morph_.setVisible(!isSpiral);
        gravity_.setVisible(!isSpiral);
        stagger_.setVisible(!isSpiral);
        voicingPack_.setVisible(!isSpiral);
        gravityMode_.setVisible(!isSpiral);
        staggerOrder_.setVisible(!isSpiral);
        anchorSub_.setVisible(shownMode_ == 1);

        const unsigned col = modeAccent(shownMode_);
        morph_.setAccent(col);
        gravity_.setAccent(col);
        stagger_.setAccent(col);
        voicingPack_.setAccent(col);
    }

    void draw(visage::Canvas& canvas) override {
        const float gap = context_.px(metric::gap);
        const float colW = (width() - 2.0f * gap) / 3.0f;
        const float h = height();

        const unsigned leftAcc = modeAccent(shownMode_);
        const char* leftTitle = shownMode_ == 0   ? "01 · SPIRAL GEOMETRY"
                                : shownMode_ == 1 ? "01 · COPEDENT CHORD-BEND"
                                                  : "01 · M\xc3\x96"
                                                    "BIUS TORUS GEOMETRY";

        const std::array<std::pair<const char*, unsigned>, 3> headers{{
            {leftTitle, leftAcc},
            {"02 · SOUND, FILTER & SPACE CORE", colour::mobius},
            {"03 · ENVELOPE & OUTPUT", colour::drive},
        }};

        for (int i = 0; i < 3; ++i) {
            const float x = static_cast<float>(i) * (colW + gap);
            drawPanel(canvas, context_, x, 0.0f, colW, h, colour::panel, metric::radius);
            drawLabel(canvas, context_, headers[static_cast<std::size_t>(i)].first, x + context_.px(12.0f),
                      context_.px(7.0f), colW - context_.px(24.0f), context_.px(16.0f),
                      headers[static_cast<std::size_t>(i)].second, visage::Font::kLeft, 10.0f, Weight::semibold);
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float colW = (width() - 2.0f * gap) / 3.0f;
        const float pad = px(10.0f);
        const float topY = px(26.0f);
        const float rowH = px(20.0f);
        const float rowGap = px(3.0f);
        const float step = rowH + rowGap;

        // ===== Column 0: Mode-Adaptive Geometry =====
        const float x0 = 0.0f;
        if (shownMode_ == 0) {
            const float knobW = px(78.0f);
            const float knobH = px(72.0f);
            bellCenter_.setBounds(x0 + pad, topY, knobW, knobH);
            bellWidth_.setBounds(x0 + pad + knobW + px(6.0f), topY, knobW, knobH);

            const float rx = x0 + pad + 2.0f * knobW + px(14.0f);
            const float rw = std::max(px(120.0f), colW - (rx - x0) - pad);
            spiralDir_.setBounds(rx, topY, rw, rowH);
            loopBars_.setBounds(rx, topY + step, rw, rowH);
            spiralRatio_.setBounds(rx, topY + step * 2.0f, rw, rowH);

            const float botY = topY + knobH + px(12.0f);
            const float halfW = (colW - 2.0f * pad - px(8.0f)) * 0.5f;
            spiralStack_.setBounds(x0 + pad, botY, halfW, rowH);
            phaseOffset_.setBounds(x0 + pad + halfW + px(8.0f), botY, halfW, rowH);
        } else {
            const float knobW = (colW - 2.0f * pad - px(12.0f)) / 3.0f;
            const float knobH = px(72.0f);
            morph_.setBounds(x0 + pad, topY, knobW, knobH);
            gravity_.setBounds(x0 + pad + knobW + px(6.0f), topY, knobW, knobH);
            stagger_.setBounds(x0 + pad + 2.0f * (knobW + px(6.0f)), topY, knobW, knobH);

            const float botY = topY + knobH + px(8.0f);
            const float halfW = (colW - 2.0f * pad - px(8.0f)) * 0.5f;
            voicingPack_.setBounds(x0 + pad, botY, halfW, rowH);
            gravityMode_.setBounds(x0 + pad + halfW + px(8.0f), botY, halfW, rowH);
            staggerOrder_.setBounds(x0 + pad, botY + step, halfW, rowH);
            if (shownMode_ == 1) {
                anchorSub_.setBounds(x0 + pad + halfW + px(8.0f), botY + step, halfW, rowH);
            } else {
                loopBars_.setBounds(x0 + pad + halfW + px(8.0f), botY + step, halfW, rowH);
                spiralDir_.setBounds(x0 + pad, botY + step * 2.0f, halfW, rowH);
            }
        }

        // ===== Column 1: Sound, Filter & Space Core =====
        const float x1 = colW + gap;
        const float k1W = (colW - 2.0f * pad - px(12.0f)) / 3.0f;
        const float k1H = px(68.0f);
        subAnchor_.setBounds(x1 + pad, topY, k1W, k1H);
        drive_.setBounds(x1 + pad + k1W + px(6.0f), topY, k1W, k1H);
        filterHz_.setBounds(x1 + pad + 2.0f * (k1W + px(6.0f)), topY, k1W, k1H);

        const float sY = topY + k1H + px(4.0f);
        const float sHalf = (colW - 2.0f * pad - px(8.0f)) * 0.5f;
        timbre_.setBounds(x1 + pad, sY, sHalf, rowH);
        filterMode_.setBounds(x1 + pad + sHalf + px(8.0f), sY, sHalf, rowH);
        detune_.setBounds(x1 + pad, sY + step, sHalf, rowH);
        filterRes_.setBounds(x1 + pad + sHalf + px(8.0f), sY + step, sHalf, rowH);
        width_.setBounds(x1 + pad, sY + step * 2.0f, sHalf, rowH);
        space_.setBounds(x1 + pad + sHalf + px(8.0f), sY + step * 2.0f, sHalf, rowH);
        subWarmth_.setBounds(x1 + pad, sY + step * 3.0f, sHalf, rowH);
        subCutoff_.setBounds(x1 + pad + sHalf + px(8.0f), sY + step * 3.0f, sHalf, rowH);
        driveModel_.setBounds(x1 + pad, sY + step * 4.0f, sHalf, rowH);
        rissetGate_.setBounds(x1 + pad + sHalf + px(8.0f), sY + step * 4.0f, sHalf, rowH);

        // ===== Column 2: Envelope & Output =====
        const float x2 = 2.0f * (colW + gap);
        const float outKnobW = px(78.0f);
        const float outKnobH = px(72.0f);
        outputDb_.setBounds(x2 + pad, topY, outKnobW, outKnobH);
        meter_.setBounds(x2 + pad + outKnobW + px(10.0f), topY + px(8.0f), colW - 2.0f * pad - outKnobW - px(10.0f),
                         px(56.0f));

        const float eRowH = px(25.0f);
        const float eStep = eRowH + px(9.0f);
        const float eTotalH = 2.0f * eStep + eRowH;
        const float eY =
            std::round(topY + outKnobH + std::max(px(10.0f), (height() - pad - (topY + outKnobH) - eTotalH) * 0.5f));
        const float eHalf = (colW - 2.0f * pad - px(10.0f)) * 0.5f;
        attackMs_.setBounds(x2 + pad, eY, eHalf, eRowH);
        releaseMs_.setBounds(x2 + pad + eHalf + px(10.0f), eY, eHalf, eRowH);
        morphAuto_.setBounds(x2 + pad, eY + eStep, eHalf, eRowH);
        mpeOut_.setBounds(x2 + pad + eHalf + px(10.0f), eY + eStep, eHalf, eRowH);
        limiter_.setBounds(x2 + pad, eY + eStep * 2.0f, colW - 2.0f * pad, eRowH);
    }

  private:
    int shownMode_ = 0;

    Knob bellCenter_, bellWidth_;
    Row spiralDir_, loopBars_, spiralRatio_, spiralStack_, phaseOffset_;

    Knob morph_, gravity_, stagger_;
    Row voicingPack_, gravityMode_, staggerOrder_, anchorSub_;

    Knob subAnchor_, drive_, filterHz_;
    Row timbre_, filterMode_, detune_, filterRes_, width_, space_, subWarmth_, subCutoff_, driveModel_, rissetGate_;

    Knob outputDb_;
    Row attackMs_, releaseMs_, morphAuto_, mpeOut_, limiter_;
    MeterStrip meter_;
};
} // namespace

std::unique_ptr<Panel> makeControlsStrip(Context& ctx) { return std::make_unique<ControlsStrip>(ctx); }

} // namespace tg::ui
