// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include "sb/modal_models.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sb::ui {

namespace {

unsigned shellPalettePrimary(int modelIdx) {
    switch (std::clamp(modelIdx, 0, 5)) {
    case 0:
        return colour::shell; // Snare Shell: Solar Tangerine
    case 1:
        return colour::smack; // Kirchhoff Plate: Electric Magenta
    case 2:
        return colour::body; // Drumhead: Electric Cyan
    case 3:
        return colour::punch; // Metallic Bar: Luminous Gold
    case 4:
        return colour::locked; // Hollow Pipe: Neon Mint
    case 5:
        return colour::violet; // Glass Bell: Luminous Violet
    }
    return colour::shell;
}

class StagePanel final : public Panel, public Live {
  public:
    explicit StagePanel(Context& ctx) : Panel(ctx) { context_.live.push_back(this); }
    ~StagePanel() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto& tel = context_.model.visuals();
        if (tel.triggerCount != lastTrigCount_) {
            lastTrigCount_ = tel.triggerCount;
            ripplePhase_ = 0.04f;
            rippleVel_ = std::clamp(tel.lastVelocity, 0.45f, 1.0f);
        } else if (ripplePhase_ > 0.0f && ripplePhase_ < 1.0f) {
            ripplePhase_ = std::min(1.0f, ripplePhase_ + 0.068f);
            if (ripplePhase_ >= 1.0f)
                ripplePhase_ = 0.0f;
        }
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const auto scope = scopeRect();
        const auto chladni = chladniRect();

        if (e.position.x >= scope.x && e.position.x <= scope.x + scope.w && e.position.y >= scope.y &&
            e.position.y <= scope.y + scope.h) {
            draggingScope_ = true;
            dragStart_ = e.position;
            dragStartOff_ = context_.model.get(ParamId::shellOffsetMs);
            dragStartPhase_ = context_.model.get(ParamId::bodyPhase);
            context_.model.begin(ParamId::shellOffsetMs);
            context_.model.begin(ParamId::bodyPhase);
            redraw();
            return;
        }

        const float cx = chladni.x + chladni.w * 0.5f;
        const float cy = chladni.y + chladni.h * 0.5f;
        const float maxR = std::min(chladni.w, chladni.h) * 0.45f;
        const float dx = e.position.x - cx;
        const float dy = e.position.y - cy;
        if (dx * dx + dy * dy <= maxR * maxR * 1.2f) {
            draggingChladni_ = true;
            context_.model.begin(ParamId::strikePos);
            applyChladniStrike(e.position, true);
        }
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (draggingScope_) {
            const float dx = (e.position.x - dragStart_.x) / std::max(1.0f, scopeRect().w);
            const float dy = (dragStart_.y - e.position.y) / std::max(1.0f, scopeRect().h);
            const float fine = e.isShiftDown() ? 0.25f : 1.0f;
            const float nextOff = std::clamp(dragStartOff_ + dx * 6.0f * fine, -5.0f, 5.0f);
            float nextPhase = std::fmod(dragStartPhase_ + dy * 360.0f * fine, 360.0f);
            if (nextPhase < 0.0f)
                nextPhase += 360.0f;
            context_.model.set(ParamId::shellOffsetMs, nextOff);
            context_.model.set(ParamId::bodyPhase, nextPhase);
            redraw();
            return;
        }
        if (draggingChladni_) {
            applyChladniStrike(e.position, false);
            redraw();
        }
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (draggingScope_) {
            draggingScope_ = false;
            context_.model.end(ParamId::shellOffsetMs);
            context_.model.end(ParamId::bodyPhase);
            context_.model.triggerAudition(Articulation::main, 0.96f);
            redraw();
        }
        if (draggingChladni_) {
            draggingChladni_ = false;
            context_.model.end(ParamId::strikePos);
            context_.model.triggerAudition(Articulation::main, 0.96f);
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        const float gap = context_.px(metric::gap);
        const float leftW = std::floor((width() - gap) * 0.62f);
        const float rightX = leftW + gap;
        const float rightW = width() - rightX;

        const int modelIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::shellModel))), 0, 5);
        const unsigned shellHue = shellPalettePrimary(modelIdx);

        drawCard(canvas, context_, 0.0f, 0.0f, leftW, height(), colour::body);
        drawCard(canvas, context_, rightX, 0.0f, rightW, height(), shellHue);

        drawMicroscope(canvas, leftW);
        drawChladni(canvas, rightX, rightW, modelIdx, shellHue);
    }

  private:
    struct Rect {
        float x, y, w, h;
    };

    Rect scopeRect() const {
        const float gap = context_.px(metric::gap);
        const float leftW = std::floor((width() - gap) * 0.62f);
        const float pad = context_.px(12.0f);
        const float top = context_.px(34.0f);
        const float bot = context_.px(22.0f);
        return {pad, top, std::max(40.0f, leftW - 2.0f * pad), std::max(40.0f, height() - top - bot)};
    }

    Rect chladniRect() const {
        const float gap = context_.px(metric::gap);
        const float leftW = std::floor((width() - gap) * 0.62f);
        const float rightX = leftW + gap;
        const float rightW = width() - rightX;
        const float pad = context_.px(12.0f);
        const float top = context_.px(34.0f);
        const float bot = context_.px(22.0f);
        return {rightX + pad, top, std::max(40.0f, rightW - 2.0f * pad), std::max(40.0f, height() - top - bot)};
    }

    void applyChladniStrike(visage::Point pt, bool fireNow) {
        const auto r = chladniRect();
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.5f;
        const float maxR = std::max(10.0f, std::min(r.w, r.h) * 0.45f);
        const float dist = std::hypot(pt.x - cx, pt.y - cy);
        const float normR = std::clamp(dist / maxR, 0.0f, 1.0f);
        strikeAngle_ = std::atan2(pt.y - cy, pt.x - cx);
        context_.model.set(ParamId::strikePos, normR * 100.0f);
        if (fireNow) {
            ripplePhase_ = 0.04f;
            rippleVel_ = 0.96f;
            context_.model.triggerAudition(normR > 0.78f ? Articulation::rim : Articulation::main, 0.96f);
        }
    }

    void drawMicroscope(visage::Canvas& canvas, float leftW) {
        const auto& tel = context_.model.visuals();
        drawLabel(canvas, context_, "40 MS TRANSIENT MICROSCOPE  \xc2\xb7  DRAG X: ALIGN  \xc2\xb7  DRAG Y: PHASE",
                  context_.px(14.0f), context_.px(9.0f), leftW * 0.58f, context_.px(18.0f), colour::body,
                  visage::Font::kLeft, 9.6f, Weight::semibold);

        // Glowing Phase-Lock & Fundamental HUD pill
        const float lockPct = std::clamp(tel.phaseLockPct, 0.0f, 100.0f);
        const unsigned lockCol = lockPct >= 62.0f ? colour::locked : (lockPct < 42.0f ? colour::warn : colour::punch);
        const float hudW = context_.px(252.0f);
        const float hudH = context_.px(20.0f);
        const float hudX = leftW - hudW - context_.px(12.0f);
        const float hudY = context_.px(8.0f);
        canvas.setBrush(
            visage::Brush::horizontal(colour::withAlpha(lockCol, 0.16f), colour::withAlpha(lockCol, 0.08f)));
        canvas.roundedRectangle(hudX, hudY, hudW, hudH, context_.px(5.0f));
        canvas.setColor(colour::withAlpha(lockCol, 0.48f));
        canvas.roundedRectangleBorder(hudX, hudY, hudW, hudH, context_.px(5.0f), context_.px(1.0f));

        char hud[96]{};
        const float fHz = context_.model.get(ParamId::bodyHz);
        std::snprintf(hud, sizeof(hud), "%.0f Hz %s  \xc2\xb7  CREST %.1f dB  \xc2\xb7  LOCK %.0f%%",
                      static_cast<double>(fHz), hzToNoteTag(fHz).c_str(), static_cast<double>(tel.crestDb),
                      static_cast<double>(lockPct));
        drawLabel(canvas, context_, hud, hudX, hudY, hudW, hudH, lockCol, visage::Font::kCenter, 9.2f,
                  Weight::semibold);

        // Deep OLED sapphire-obsidian glass well with subtle center radial glow
        const auto r = scopeRect();
        const float rad = context_.px(metric::radiusSmall);
        canvas.setBrush(visage::Brush::radial(0xff10192d, 0xff050810, {r.x + r.w * 0.35f, r.y + r.h * 0.5f},
                                              r.w * 0.72f, r.h * 0.9f));
        canvas.roundedRectangle(r.x, r.y, r.w, r.h, rad);
        canvas.setColor(colour::wellEdge);
        canvas.roundedRectangleBorder(r.x, r.y, r.w, r.h, rad, context_.px(1.0f));

        // Vertical millisecond gridlines (0, 5, 10, 15, 20, 30, 40 ms)
        constexpr float kTicksMs[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 40.0f};
        for (float ms : kTicksMs) {
            const float gx = r.x + (ms / 40.0f) * r.w;
            canvas.setColor(colour::withAlpha(ms == 0.0f ? colour::body : colour::wire,
                                              ms == 0.0f ? 0.32f : (ms == 10.0f || ms == 20.0f ? 0.13f : 0.06f)));
            canvas.rectangle(gx, r.y, context_.px(ms == 0.0f ? 1.5f : 1.0f), r.h);

            char lbl[16]{};
            std::snprintf(lbl, sizeof(lbl), "%.0f ms", static_cast<double>(ms));
            const float lw = context_.px(34.0f);
            const float lx = std::clamp(gx - lw * 0.5f, r.x, r.x + r.w - lw);
            drawLabel(canvas, context_, lbl, lx, r.y + r.h + context_.px(3.5f), lw, context_.px(14.0f), colour::textDim,
                      visage::Font::kCenter, 8.8f, Weight::regular);
        }

        // Zero centerline
        const float midY = r.y + r.h * 0.5f;
        canvas.setColor(colour::withAlpha(colour::body, 0.18f));
        canvas.rectangle(r.x, midY, r.w, context_.px(1.0f));

        // Dual-pass Neon OLED Oscilloscope Curves (wide bloom under razor-sharp core!)
        const float invPeak = 1.0f / std::max(0.25f, tel.scopePeak * 1.08f);
        auto drawGlowingWave = [&](const std::array<float, kScopeSamples>& w, unsigned col, float fillAlpha,
                                   float coreThick, float glowAlpha) {
            std::array<float, kScopeSamples> norm{};
            for (std::size_t i = 0; i < kScopeSamples; ++i)
                norm[i] = std::clamp(w[i] * invPeak, -1.0f, 1.0f);
            const float rx = r.x + context_.px(2.0f);
            const float ry = r.y + context_.px(6.0f);
            const float rw = r.w - context_.px(4.0f);
            const float rh = r.h - context_.px(12.0f);
            if (glowAlpha > 0.0f) {
                hba::ui::drawCurve(canvas, context_.scale, norm, rx, ry, rw, rh, colour::withAlpha(col, glowAlpha),
                                   -1.0f, 1.0f, 0.0f, 0.0f, coreThick * 2.7f);
            }
            hba::ui::drawCurve(canvas, context_.scale, norm, rx, ry, rw, rh, col, -1.0f, 1.0f, fillAlpha, 0.0f,
                               coreThick);
        };

        drawGlowingWave(tel.sumWave, colour::withAlpha(colour::locked, 0.68f), 0.14f, 1.35f, 0.20f);
        drawGlowingWave(tel.wireWave, colour::withAlpha(colour::wire, 0.60f), 0.0f, 1.15f, 0.0f);
        drawGlowingWave(tel.shellWave, colour::shell, 0.0f, 1.85f, 0.24f);
        drawGlowingWave(tel.bodyWave, colour::body, 0.09f, 2.25f, 0.28f);

        // Glassy translucent layer legend pill top-left inside scope
        const float pillX = r.x + context_.px(8.0f);
        const float pillY = r.y + context_.px(6.0f);
        const float pillW = context_.px(230.0f);
        const float pillH = context_.px(20.0f);
        canvas.setColor(0xe6070b14u);
        canvas.roundedRectangle(pillX, pillY, pillW, pillH, context_.px(5.0f));
        canvas.setColor(colour::cardEdge);
        canvas.roundedRectangleBorder(pillX, pillY, pillW, pillH, context_.px(5.0f), context_.px(1.0f));

        const float ly = pillY + context_.px(3.0f);
        float lx = pillX + context_.px(9.0f);
        auto drawLegend = [&](const char* txt, unsigned col, float widthPx) {
            const float dot = context_.px(6.0f);
            canvas.setColor(colour::withAlpha(col, 0.45f));
            canvas.fadeCircle(lx - dot * 0.5f, ly + context_.px(1.0f), dot * 2.0f, dot);
            canvas.setColor(col);
            canvas.circle(lx, ly + context_.px(4.0f), dot);
            lx += dot + context_.px(5.0f);
            drawLabel(canvas, context_, txt, lx, ly, context_.px(widthPx), context_.px(14.0f), col, visage::Font::kLeft,
                      9.0f, Weight::semibold);
            lx += context_.px(widthPx + 6.0f);
        };
        drawLegend("BODY", colour::body, 36.0f);
        drawLegend("SHELL", colour::shell, 40.0f);
        drawLegend("WIRE", colour::wire, 34.0f);
        drawLegend("SUM", colour::locked, 30.0f);

        if (draggingScope_) {
            const float hw = context_.px(218.0f);
            const float hh = context_.px(24.0f);
            const float hx = r.x + r.w - hw - context_.px(8.0f);
            const float hy = r.y + context_.px(6.0f);
            canvas.setColor(0xf0101828u);
            canvas.roundedRectangle(hx, hy, hw, hh, context_.px(5.0f));
            canvas.setColor(colour::body);
            canvas.roundedRectangleBorder(hx, hy, hw, hh, context_.px(5.0f), context_.px(1.2f));
            char dragTxt[64]{};
            std::snprintf(dragTxt, sizeof(dragTxt), "ALIGN %+.2f ms  \xc2\xb7  PHASE %.0f\xc2\xb0",
                          static_cast<double>(context_.model.get(ParamId::shellOffsetMs)),
                          static_cast<double>(context_.model.get(ParamId::bodyPhase)));
            drawLabel(canvas, context_, dragTxt, hx, hy, hw, hh, 0xffffffff, visage::Font::kCenter, 9.6f,
                      Weight::semibold);
        }
    }

    void drawChladni(visage::Canvas& canvas, float rightX, float rightW, int modelIdx, unsigned shellHue) {
        const auto& tel = context_.model.visuals();
        const int malletIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::malletType))), 0, 5);
        const auto& geom = modalGeometry(static_cast<ShellModel>(modelIdx));
        const float strikeNorm = std::clamp(context_.model.get(ParamId::strikePos) * 0.01f, 0.0f, 1.0f);

        drawLabel(canvas, context_, "CHLADNI MODAL SHELL  \xc2\xb7  TAP TO STRIKE", rightX + context_.px(14.0f),
                  context_.px(9.0f), rightW * 0.56f, context_.px(18.0f), shellHue, visage::Font::kLeft, 9.6f,
                  Weight::semibold);

        const std::string meta = formatValue(specOf(ParamId::shellModel), static_cast<float>(modelIdx)) + " \xc2\xb7 " +
                                 formatValue(specOf(ParamId::malletType), static_cast<float>(malletIdx));
        drawLabel(canvas, context_, meta, rightX + rightW - context_.px(206.0f), context_.px(9.0f), context_.px(194.0f),
                  context_.px(18.0f), 0xffb8c7e0, visage::Font::kRight, 9.1f, Weight::medium);

        const auto r = chladniRect();
        const float rad = context_.px(metric::radiusSmall);
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.5f;
        const float maxR = std::min(r.w, r.h) * 0.45f;

        // Compute aggregate live shell energy for breathing plasma aura
        float sumE = 0.0f;
        for (float e : tel.modeEnergy)
            sumE += e;
        const float energyGlow = std::clamp(sumE * 0.09f, 0.0f, 1.0f);

        // Deep glowing radial acoustic drumhead chamber!
        canvas.setBrush(visage::Brush::radial(colour::withAlpha(shellHue, 0.14f + 0.18f * energyGlow), 0xff050810,
                                              {cx, cy}, maxR * 1.35f));
        canvas.roundedRectangle(r.x, r.y, r.w, r.h, rad);
        canvas.setColor(colour::wellEdge);
        canvas.roundedRectangleBorder(r.x, r.y, r.w, r.h, rad, context_.px(1.0f));

        // Glowing acoustic standing-wave rim halo
        canvas.setColor(colour::withAlpha(shellHue, 0.20f + 0.25f * energyGlow));
        canvas.fadeCircle(cx - maxR * 1.12f, cy - maxR * 1.12f, maxR * 2.24f, maxR * 0.35f);

        // Concentric Chladni nodal rings
        for (int ring = 1; ring <= 4; ++ring) {
            const float rr = maxR * (static_cast<float>(ring) / 4.0f);
            const bool outer = (ring == 4);
            canvas.setColor(
                colour::withAlpha(outer ? shellHue : colour::wire, outer ? (0.58f + 0.30f * energyGlow) : 0.10f));
            canvas.ring(cx - rr, cy - rr, rr * 2.0f, context_.px(outer ? 2.0f : 1.0f));
        }

        // Crosshair axes
        canvas.setColor(colour::withAlpha(colour::wire, 0.08f));
        canvas.rectangle(cx - maxR, cy, maxR * 2.0f, context_.px(1.0f));
        canvas.rectangle(cx, cy - maxR, context_.px(1.0f), maxR * 2.0f);

        const float sx = cx + std::cos(strikeAngle_) * strikeNorm * maxR;
        const float sy = cy + std::sin(strikeAngle_) * strikeNorm * maxR;

        // Multi-ring velocity-reactive shockwave ripple expanding from strike point!
        const float activeRipple = ripplePhase_ > 0.0f ? ripplePhase_ : (energyGlow > 0.25f ? 0.34f : 0.0f);
        if (activeRipple > 0.0f) {
            const float fade = (1.0f - activeRipple) * (1.0f - activeRipple) * rippleVel_;
            const float r1 = maxR * (0.10f + 0.92f * activeRipple);
            canvas.setColor(colour::withAlpha(shellHue, 0.78f * fade));
            canvas.ring(sx - r1, sy - r1, r1 * 2.0f, context_.px(2.6f * (1.0f - 0.45f * activeRipple)));

            const float r2 = maxR * (0.04f + 0.65f * activeRipple);
            canvas.setColor(colour::withAlpha(colour::body, 0.58f * fade));
            canvas.ring(cx - r2, cy - r2, r2 * 2.0f, context_.px(1.6f));
        }

        // Plot all 24 live vibrating Chladni modal orbs with feathered GPU bloom!
        for (int k = 0; k < kModeCount; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            const float radFrac = static_cast<float>(geom.radialOrder[ki]);
            const float angle = static_cast<float>(k) * 2.399963f + static_cast<float>(geom.angularOrder[ki]) * 0.45f;
            const float mx = cx + std::cos(angle) * radFrac * maxR;
            const float my = cy + std::sin(angle) * radFrac * maxR;

            const double weight = strikeModeWeight(geom.gains[ki], geom.radialOrder[ki], geom.angularOrder[ki],
                                                   static_cast<double>(strikeNorm));
            const float liveE = tel.modeEnergy[ki];
            const float visualAmp = std::clamp(static_cast<float>(weight) * 0.46f + liveE * 0.92f, 0.10f, 1.0f);

            const unsigned col = (k < 4) ? colour::body : ((k & 1) ? shellHue : colour::punch);
            const float glowD = context_.px(10.0f + visualAmp * 18.0f);
            canvas.setColor(colour::withAlpha(col, 0.22f + 0.44f * liveE));
            canvas.fadeCircle(mx - glowD * 0.5f, my - glowD * 0.5f, glowD, glowD * 0.48f);

            const float dotD = context_.px(3.8f + visualAmp * 5.2f);
            canvas.setColor(colour::withAlpha(col, 0.65f + 0.35f * visualAmp));
            canvas.circle(mx - dotD * 0.5f, my - dotD * 0.5f, dotD);

            if (visualAmp > 0.55f) {
                const float core = dotD * 0.42f;
                canvas.setColor(0xddffffff);
                canvas.circle(mx - core * 0.5f, my - core * 0.5f, core);
            }
        }

        // Interactive glowing Strike Target Reticle
        const float glowRet = context_.px(34.0f);
        canvas.setColor(colour::withAlpha(shellHue, 0.42f));
        canvas.fadeCircle(sx - glowRet * 0.5f, sy - glowRet * 0.5f, glowRet, glowRet * 0.5f);

        const float ringD = context_.px(20.0f);
        canvas.setColor(0xffffffff);
        canvas.ring(sx - ringD * 0.5f, sy - ringD * 0.5f, ringD, context_.px(1.8f));
        const float coreD = context_.px(6.5f);
        canvas.setColor(shellHue);
        canvas.circle(sx - coreD * 0.5f, sy - coreD * 0.5f, coreD);

        char posLbl[48]{};
        std::snprintf(posLbl, sizeof(posLbl), "STRIKE %.0f%%  (%s)", static_cast<double>(strikeNorm * 100.0f),
                      strikeNorm < 0.30f ? "CENTER" : (strikeNorm > 0.72f ? "RIM" : "MID-HEAD"));
        drawLabel(canvas, context_, posLbl, r.x, r.y + r.h + context_.px(3.5f), r.w, context_.px(14.0f), 0xffb8c7e0,
                  visage::Font::kCenter, 9.0f, Weight::semibold);
    }

    bool draggingScope_ = false;
    bool draggingChladni_ = false;
    visage::Point dragStart_{};
    float dragStartOff_ = 0.0f;
    float dragStartPhase_ = 90.0f;
    float strikeAngle_ = -0.62f;
    std::uint32_t lastTrigCount_ = 0;
    float ripplePhase_ = 0.0f;
    float rippleVel_ = 1.0f;
};

} // namespace

std::unique_ptr<Panel> makeStagePanel(Context& ctx) { return std::make_unique<StagePanel>(ctx); }

} // namespace sb::ui
