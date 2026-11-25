// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <hba/dsp/control.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace sb::ui {

namespace {

class RackPanel final : public Panel, public Live {
  public:
    explicit RackPanel(Context& ctx)
        : Panel(ctx),
          // Layer M/S pills
          mBody_(ctx, "M", [this] { toggleMute(ParamId::bodyMute); }), sBody_(ctx, "S", [this] { toggleSolo(1); }),
          mShell_(ctx, "M", [this] { toggleMute(ParamId::shellMute); }), sShell_(ctx, "S", [this] { toggleSolo(2); }),
          mWire_(ctx, "M", [this] { toggleMute(ParamId::wireMute); }), sWire_(ctx, "S", [this] { toggleSolo(3); }),

          // Column 1: Fundamental Body (Electric Cyan) — 3 Hero Knobs + Chip + 5 Sleek Faders
          kBodyHz_(ctx, ParamId::bodyHz, colour::body, "Body Hz"),
          kPitchDrop_(ctx, ParamId::pitchDrop, colour::body, "Pitch Drop"),
          kBodyLevel_(ctx, ParamId::bodyLevel, colour::body, "Level"),
          rBodyWave_(ctx, ParamId::bodyWave, colour::body, "Wave", Row::Style::chip),
          rPitchCurve_(ctx, ParamId::pitchCurve, colour::body, "Drop Curve"),
          rPitchTime_(ctx, ParamId::pitchTime, colour::body, "Drop Time"),
          rBodyDecay_(ctx, ParamId::bodyDecay, colour::body, "Body Decay"),
          rBodyPhase_(ctx, ParamId::bodyPhase, colour::body, "Start Phase"),
          rBodyFm_(ctx, ParamId::bodyFm, colour::body, "Body FM"),
          rBodyFmRatio_(ctx, ParamId::bodyFmRatio, colour::body, "FM Ratio"),

          // Column 2: 24-Mode Modal Shell (Solar Tangerine) — 3 Hero Knobs + Dual Chips + 6 Faders w/ Inline Pol
          kTension_(ctx, ParamId::shellTension, colour::shell, "Tension"),
          kStrikePos_(ctx, ParamId::strikePos, colour::shell, "Strike Pos"),
          kShellLevel_(ctx, ParamId::shellLevel, colour::shell, "Level"),
          rShellModel_(ctx, ParamId::shellModel, colour::shell, "", Row::Style::chip),
          rMallet_(ctx, ParamId::malletType, colour::shell, "", Row::Style::chip),
          rShellTune_(ctx, ParamId::shellTune, colour::shell, "Shell Tune"),
          rShellStretch_(ctx, ParamId::shellStretch, colour::shell, "Stretch"),
          rShellDecay_(ctx, ParamId::shellDecay, colour::shell, "Shell Decay"),
          rShellDamp_(ctx, ParamId::shellDamp, colour::shell, "Hi Damp"),
          rShellSpread_(ctx, ParamId::shellSpread, colour::shell, "Spread"),
          rShellOffset_(ctx, ParamId::shellOffsetMs, colour::shell, "Align ms"),
          rShellInvert_(ctx, ParamId::shellInvert, colour::shell, "\xc3\x98", Row::Style::badge),

          // Column 3: Wire & Transient (Plasma Ice) — 3 Hero Knobs + Chip+Hum + 6 Faders w/ Inline Pol
          kClickAmt_(ctx, ParamId::clickAmt, colour::wire, "Crack Click"),
          kWireDuck_(ctx, ParamId::wireDuck, colour::wire, "Wire Duck"),
          kWireLevel_(ctx, ParamId::wireLevel, colour::wire, "Level"),
          rWireColor_(ctx, ParamId::wireColor, colour::wireAlt, "-", Row::Style::chip),
          rHumanize_(ctx, ParamId::humanize, colour::wireAlt, "Hum"),
          rClickTone_(ctx, ParamId::clickTone, colour::wire, "Click Hz"),
          rWireBuzz_(ctx, ParamId::wireBuzz, colour::wire, "Buzz"),
          rWireDuckMs_(ctx, ParamId::wireDuckMs, colour::wire, "Duck"),
          rWireTone_(ctx, ParamId::wireTone, colour::wire, "Wire Tone"),
          rWireDecay_(ctx, ParamId::wireDecay, colour::wire, "Wire Decay"),
          rWireWidth_(ctx, ParamId::wireWidth, colour::wire, "Width"),
          rWireOffset_(ctx, ParamId::wireOffsetMs, colour::wire, "Align ms"),
          rWireInvert_(ctx, ParamId::wireInvert, colour::wireAlt, "\xc3\x98", Row::Style::badge),

          // Column 4: Punch & Master (Neon Gold / Mint) — 3 Hero Knobs + Dual Chips + Fader + Badges + Meters
          kDisperser_(ctx, ParamId::disperserAmt, colour::punch, "Disperser"),
          kDrive_(ctx, ParamId::drive, colour::punch, "ADAA Drive"),
          kOutputDb_(ctx, ParamId::outputDb, colour::locked, "Output"),
          rDriveModel_(ctx, ParamId::driveModel, colour::punch, "", Row::Style::chip),
          rChokeMode_(ctx, ParamId::chokeMode, colour::punch, "", Row::Style::chip),
          rDispHz_(ctx, ParamId::disperserHz, colour::punch, "Disperser Hz"),
          rKeyTrack_(ctx, ParamId::keyTrack, colour::body, "", Row::Style::chip),
          rQuality_(ctx, ParamId::quality, colour::wireAlt, "", Row::Style::chip),
          rSubMono_(ctx, ParamId::subMono, colour::locked, "SUB MONO", Row::Style::badge),
          rLimiter_(ctx, ParamId::limiter, colour::locked, "LIMITER", Row::Style::badge) {
        for (auto* mb : {&mBody_, &mShell_, &mWire_})
            mb->setAccent(colour::warn);
        sBody_.setAccent(colour::body);
        sShell_.setAccent(colour::shell);
        sWire_.setAccent(colour::wireAlt);

        const std::initializer_list<visage::Frame*> all{
            &mBody_,       &sBody_,        &mShell_,       &sShell_,       &mWire_,         &sWire_,
            &kBodyHz_,     &kPitchDrop_,   &kBodyLevel_,   &rBodyWave_,    &rPitchCurve_,   &rPitchTime_,
            &rBodyDecay_,  &rBodyPhase_,   &rBodyFm_,      &rBodyFmRatio_, &kTension_,      &kStrikePos_,
            &kShellLevel_, &rShellModel_,  &rMallet_,      &rShellTune_,   &rShellStretch_, &rShellDecay_,
            &rShellDamp_,  &rShellSpread_, &rShellOffset_, &rShellInvert_, &kClickAmt_,     &kWireDuck_,
            &kWireLevel_,  &rWireColor_,   &rHumanize_,    &rClickTone_,   &rWireBuzz_,     &rWireDuckMs_,
            &rWireTone_,   &rWireDecay_,   &rWireWidth_,   &rWireOffset_,  &rWireInvert_,   &kDisperser_,
            &kDrive_,      &kOutputDb_,    &rDriveModel_,  &rChokeMode_,   &rDispHz_,       &rKeyTrack_,
            &rQuality_,    &rSubMono_,     &rLimiter_,
        };
        for (auto* f : all)
            addChild(f);
        context_.live.push_back(this);
        tick();
    }

    ~RackPanel() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto& tel = context_.model.visuals();
        peakSmoothL_ = std::max(tel.peakL, peakSmoothL_ * 0.86f);
        peakSmoothR_ = std::max(tel.peakR, peakSmoothR_ * 0.86f);
        grSmooth_ = std::max(tel.gainReductionDb, grSmooth_ * 0.86f);

        mBody_.setActive(context_.model.get(ParamId::bodyMute) >= 0.5f);
        mShell_.setActive(context_.model.get(ParamId::shellMute) >= 0.5f);
        mWire_.setActive(context_.model.get(ParamId::wireMute) >= 0.5f);
        const int solo = static_cast<int>(std::lround(context_.model.get(ParamId::layerSolo)));
        sBody_.setActive(solo == 1);
        sShell_.setActive(solo == 2);
        sWire_.setActive(solo == 3);
        redraw();
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float colW = (width() - 3.0f * gap) * 0.25f;
        const float pad = px(10.0f);
        const float innerW = colW - 2.0f * pad;
        const float knobY = px(30.0f);
        const float knobH = px(74.0f);
        const float subGap = px(5.0f);

        auto placeMS = [&](int idx, ActionButton& mBtn, ActionButton& sBtn) {
            const float cx = static_cast<float>(idx) * (colW + gap);
            const float bw = px(21.0f);
            const float bh = px(17.0f);
            const float by = px(8.0f);
            sBtn.setBounds(cx + colW - pad - bw, by, bw, bh);
            mBtn.setBounds(cx + colW - pad - 2.0f * bw - px(3.0f), by, bw, bh);
        };
        placeMS(0, mBody_, sBody_);
        placeMS(1, mShell_, sShell_);
        placeMS(2, mWire_, sWire_);

        auto place3Knobs = [&](int idx, Knob& k0, Knob& k1, Knob& k2) {
            const float cx = static_cast<float>(idx) * (colW + gap) + pad;
            const float kw = innerW / 3.0f;
            k0.setBounds(cx, knobY, kw, knobH);
            k1.setBounds(cx + kw, knobY, kw, knobH);
            k2.setBounds(cx + 2.0f * kw, knobY, kw, knobH);
        };

        const float y0 = knobY + knobH + px(8.0f);
        const float rowH = px(25.5f);
        const float rGap = px(5.5f);

        // --- Column 1: BODY ---
        place3Knobs(0, kBodyHz_, kPitchDrop_, kBodyLevel_);
        {
            const float cx = pad;
            float y = y0;
            rBodyWave_.setBounds(cx, y, innerW, px(26.0f));
            y += px(26.0f) + rGap;
            for (Row* r : {&rPitchCurve_, &rPitchTime_, &rBodyDecay_, &rBodyPhase_}) {
                r->setBounds(cx, y, innerW, rowH);
                y += rowH + rGap;
            }
            const float halfW = (innerW - subGap) * 0.5f;
            rBodyFm_.setBounds(cx, y, halfW, rowH);
            rBodyFmRatio_.setBounds(cx + halfW + subGap, y, halfW, rowH);
        }

        // --- Column 2: SHELL ---
        place3Knobs(1, kTension_, kStrikePos_, kShellLevel_);
        {
            const float cx = (colW + gap) + pad;
            float y = y0;
            const float halfW = (innerW - subGap) * 0.5f;
            rShellModel_.setBounds(cx, y, halfW, px(26.0f));
            rMallet_.setBounds(cx + halfW + subGap, y, halfW, px(26.0f));
            y += px(26.0f) + rGap;
            const float sRowH = px(23.5f);
            const float sGap = px(4.5f);
            for (Row* r : {&rShellTune_, &rShellStretch_, &rShellDecay_, &rShellDamp_, &rShellSpread_}) {
                r->setBounds(cx, y, innerW, sRowH);
                y += sRowH + sGap;
            }
            const float polW = px(32.0f);
            rShellOffset_.setBounds(cx, y, innerW - polW - subGap, sRowH);
            rShellInvert_.setBounds(cx + innerW - polW, y, polW, sRowH);
        }

        // --- Column 3: WIRE & CLICK ---
        place3Knobs(2, kClickAmt_, kWireDuck_, kWireLevel_);
        {
            const float cx = 2.0f * (colW + gap) + pad;
            float y = y0;
            const float chipW = innerW * 0.54f;
            const float humW = innerW - chipW - subGap;
            rWireColor_.setBounds(cx, y, chipW, px(26.0f));
            rHumanize_.setBounds(cx + chipW + subGap, y, humW, px(26.0f));
            y += px(26.0f) + rGap;
            const float wRowH = px(23.5f);
            const float wGap = px(4.5f);
            const float halfW = (innerW - subGap) * 0.5f;
            rClickTone_.setBounds(cx, y, innerW, wRowH);
            y += wRowH + wGap;
            rWireBuzz_.setBounds(cx, y, halfW, wRowH);
            rWireDuckMs_.setBounds(cx + halfW + subGap, y, halfW, wRowH);
            y += wRowH + wGap;
            rWireTone_.setBounds(cx, y, innerW, wRowH);
            y += wRowH + wGap;
            rWireDecay_.setBounds(cx, y, innerW, wRowH);
            y += wRowH + wGap;
            rWireWidth_.setBounds(cx, y, innerW, wRowH);
            y += wRowH + wGap;
            const float polW = px(32.0f);
            rWireOffset_.setBounds(cx, y, innerW - polW - subGap, wRowH);
            rWireInvert_.setBounds(cx + innerW - polW, y, polW, wRowH);
        }

        // --- Column 4: PUNCH & MASTER ---
        place3Knobs(3, kDisperser_, kDrive_, kOutputDb_);
        {
            const float cx = 3.0f * (colW + gap) + pad;
            float y = y0;
            const float halfW = (innerW - subGap) * 0.5f;
            rDriveModel_.setBounds(cx, y, halfW, px(26.0f));
            rChokeMode_.setBounds(cx + halfW + subGap, y, halfW, px(26.0f));
            y += px(26.0f) + px(6.5f);
            rDispHz_.setBounds(cx, y, innerW, px(27.0f));
            y += px(27.0f) + px(6.5f);
            rKeyTrack_.setBounds(cx, y, halfW, px(26.0f));
            rQuality_.setBounds(cx + halfW + subGap, y, halfW, px(26.0f));
            y += px(26.0f) + px(6.5f);
            rSubMono_.setBounds(cx, y, halfW, px(27.0f));
            rLimiter_.setBounds(cx + halfW + subGap, y, halfW, px(27.0f));
        }
    }

    void draw(visage::Canvas& canvas) override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float colW = (width() - 3.0f * gap) * 0.25f;

        struct ColMeta {
            const char* title;
            unsigned accent;
        };
        constexpr std::array<ColMeta, 4> cols{{
            {"01  \xc2\xb7  FUNDAMENTAL BODY", colour::body},
            {"02  \xc2\xb7  MODAL SHELL (24)", colour::shell},
            {"03  \xc2\xb7  WIRE & TRANSIENT", colour::wireAlt},
            {"04  \xc2\xb7  PUNCH & MASTER", colour::punch},
        }};

        for (int i = 0; i < 4; ++i) {
            const float cx = static_cast<float>(i) * (colW + gap);
            const auto& c = cols[static_cast<std::size_t>(i)];
            drawCard(canvas, context_, cx, 0.0f, colW, height(), c.accent);
            drawLabel(canvas, context_, c.title, cx + px(12.0f), px(9.0f), colW - px(68.0f), px(15.0f), c.accent,
                      visage::Font::kLeft, 9.4f, Weight::semibold);
        }

        // Luminous Output L/R Peak & Limiter GR Meter Well at bottom of Column 4
        const float pad = px(10.0f);
        const float c4x = 3.0f * (colW + gap) + pad;
        const float wellW = colW - 2.0f * pad;
        const float wellH = px(70.0f);
        const float wellY = height() - wellH - px(10.0f);

        canvas.setBrush(visage::Brush::vertical(colour::wellTop, colour::wellBot));
        canvas.roundedRectangle(c4x, wellY, wellW, wellH, px(6.0f));
        canvas.setColor(colour::wellEdge);
        canvas.roundedRectangleBorder(c4x, wellY, wellW, wellH, px(6.0f), px(1.0f));

        const float mx = c4x + px(9.0f);
        const float mW = wellW - px(18.0f);
        const float mY = wellY + px(7.0f);

        auto dbNorm = [](float lin) {
            const float db = static_cast<float>(hba::dsp::gainToDb(std::max(1e-4, static_cast<double>(lin))));
            return std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        };

        drawLabel(canvas, context_, "OUTPUT L / R", mx, mY, px(84.0f), px(12.0f), 0xffb8c7e0, visage::Font::kLeft, 8.6f,
                  Weight::semibold);
        char grLbl[32]{};
        std::snprintf(grLbl, sizeof(grLbl), "GR -%.1f dB", static_cast<double>(grSmooth_));
        drawLabel(canvas, context_, grLbl, mx + mW - px(80.0f), mY, px(80.0f), px(12.0f),
                  grSmooth_ > 2.5f ? colour::warn : colour::locked, visage::Font::kRight, 8.8f, Weight::semibold);

        const float barY = mY + px(16.0f);
        const float barH = px(7.0f);
        auto drawPeakBar = [&](float yPos, float peakVal) {
            canvas.setColor(0xff141c2e);
            canvas.roundedRectangle(mx, yPos, mW, barH, barH * 0.5f);
            const float bw = mW * dbNorm(peakVal);
            if (bw > 1.5f) {
                const unsigned endCol = peakVal > 0.94f ? colour::warn : colour::body;
                canvas.setColor(colour::withAlpha(colour::locked, 0.36f));
                canvas.roundedRectangleShadow(mx, yPos, bw, barH, barH * 0.5f, px(6.0f));
                canvas.setBrush(visage::Brush::horizontal(colour::locked, endCol));
                canvas.roundedRectangle(mx, yPos, bw, barH, barH * 0.5f);
            }
        };
        drawPeakBar(barY, peakSmoothL_);
        drawPeakBar(barY + px(11.0f), peakSmoothR_);

        // Limiter Gain Reduction bar
        const float grY = barY + px(23.0f);
        canvas.setColor(0xff141c2e);
        canvas.roundedRectangle(mx, grY, mW, px(5.0f), px(2.5f));
        const float grNorm = std::clamp(grSmooth_ / 12.0f, 0.0f, 1.0f);
        if (grNorm > 0.01f) {
            const float gw = mW * grNorm;
            canvas.setColor(colour::withAlpha(colour::smack, 0.45f));
            canvas.roundedRectangleShadow(mx + mW - gw, grY, gw, px(5.0f), px(2.5f), px(6.0f));
            canvas.setBrush(visage::Brush::horizontal(colour::punch, colour::smack));
            canvas.roundedRectangle(mx + mW - gw, grY, gw, px(5.0f), px(2.5f));
        }

        // Subtle dB tick labels
        drawLabel(canvas, context_, "-36", mx, grY + px(7.0f), px(28.0f), px(10.0f), colour::textFaint,
                  visage::Font::kLeft, 7.8f, Weight::regular);
        drawLabel(canvas, context_, "-12", mx + mW * 0.58f, grY + px(7.0f), px(28.0f), px(10.0f), colour::textFaint,
                  visage::Font::kCenter, 7.8f, Weight::regular);
        drawLabel(canvas, context_, "0 dB", mx + mW - px(28.0f), grY + px(7.0f), px(28.0f), px(10.0f),
                  colour::textFaint, visage::Font::kRight, 7.8f, Weight::regular);
    }

  private:
    void toggleMute(ParamId id) {
        const float next = context_.model.get(id) >= 0.5f ? 0.0f : 1.0f;
        context_.model.begin(id);
        context_.model.set(id, next);
        context_.model.end(id);
        context_.model.triggerAudition(Articulation::main, 0.94f);
    }

    void toggleSolo(int layerIdx) {
        const int cur = static_cast<int>(std::lround(context_.model.get(ParamId::layerSolo)));
        const float next = (cur == layerIdx) ? 0.0f : static_cast<float>(layerIdx);
        context_.model.begin(ParamId::layerSolo);
        context_.model.set(ParamId::layerSolo, next);
        context_.model.end(ParamId::layerSolo);
        context_.model.triggerAudition(Articulation::main, 0.94f);
    }

    ActionButton mBody_, sBody_, mShell_, sShell_, mWire_, sWire_;

    // Col 1
    Knob kBodyHz_, kPitchDrop_, kBodyLevel_;
    Row rBodyWave_, rPitchCurve_, rPitchTime_, rBodyDecay_, rBodyPhase_, rBodyFm_, rBodyFmRatio_;

    // Col 2
    Knob kTension_, kStrikePos_, kShellLevel_;
    Row rShellModel_, rMallet_, rShellTune_, rShellStretch_, rShellDecay_, rShellDamp_, rShellSpread_, rShellOffset_,
        rShellInvert_;

    // Col 3
    Knob kClickAmt_, kWireDuck_, kWireLevel_;
    Row rWireColor_, rHumanize_, rClickTone_, rWireBuzz_, rWireDuckMs_, rWireTone_, rWireDecay_, rWireWidth_,
        rWireOffset_, rWireInvert_;

    // Col 4
    Knob kDisperser_, kDrive_, kOutputDb_;
    Row rDriveModel_, rChokeMode_, rDispHz_, rKeyTrack_, rQuality_, rSubMono_, rLimiter_;

    float peakSmoothL_ = 0.0f;
    float peakSmoothR_ = 0.0f;
    float grSmooth_ = 0.0f;
};

} // namespace

std::unique_ptr<Panel> makeRackPanel(Context& ctx) { return std::make_unique<RackPanel>(ctx); }

} // namespace sb::ui
