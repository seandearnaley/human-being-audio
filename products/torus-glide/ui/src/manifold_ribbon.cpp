// SPDX-License-Identifier: AGPL-3.0-or-later
#include "manifold_ribbon.h"

#include <hba/dsp/control.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace tg::ui {

namespace {
constexpr std::array<const char*, 12> kNoteNames{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

std::string midiNoteName(float midi) {
    const int n = static_cast<int>(std::lround(midi));
    const int pc = ((n % 12) + 12) % 12;
    const int oct = (n / 12) - 1;
    return std::string(kNoteNames[static_cast<std::size_t>(pc)]) + std::to_string(oct);
}
} // namespace

ManifoldRibbon::ManifoldRibbon(Context& ctx) : Panel(ctx) { context_.live.push_back(this); }
ManifoldRibbon::~ManifoldRibbon() { std::erase(context_.live, static_cast<Live*>(this)); }

void ManifoldRibbon::tick() {
    if (!isVisible())
        return;
    const auto& tel = context_.model.visuals();
    pulse_ = pulse_ * 0.86f + std::clamp(tel.rmsLevel * 1.4f, 0.0f, 1.0f) * 0.14f;
    redraw();
}

void ManifoldRibbon::mouseDown(const visage::MouseEvent& e) {
    const float px = context_.scale;
    const float hudW = 248.0f * px;
    const float plotX = 10.0f * px;
    const float plotW = std::max(40.0f, width() - hudW - 24.0f * px);
    if (e.position.x >= plotX && e.position.x <= plotX + plotW) {
        const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
        const float norm = std::clamp((e.position.x - plotX) / plotW, 0.0f, 1.0f);
        if (modeIdx == 0) {
            context_.model.begin(ParamId::bellCenter);
            context_.model.set(ParamId::bellCenter, fromNormalised(specOf(ParamId::bellCenter), norm));
            context_.model.end(ParamId::bellCenter);
        } else {
            if (context_.model.get(ParamId::morphAuto) >= 0.5f) {
                context_.model.begin(ParamId::morphAuto);
                context_.model.set(ParamId::morphAuto, 0.0f);
                context_.model.end(ParamId::morphAuto);
            }
            context_.model.begin(ParamId::morph);
            context_.model.set(ParamId::morph, norm * 100.0f);
            context_.model.end(ParamId::morph);
        }
        redraw();
    }
}

void ManifoldRibbon::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const float w = width();
    const float h = height();
    const auto& tel = context_.model.visuals();
    const int modeIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::mode))), 0, 2);
    const int padProg =
        std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::padProgram))), 0, kPadProgramCount - 1);
    const unsigned accent = colour::padAccent(padProg);

    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::panel, metric::radius);

    // Right status telemetry pill
    const float hudW = 248.0f * px;
    const float hudX = w - hudW - 8.0f * px;
    const float hudY = 7.0f * px;
    const float hudH = h - 14.0f * px;
    drawPanel(canvas, context_, hudX, hudY, hudW, hudH, colour::raised, metric::radiusSmall);

    // Live sounding LED dot
    const float dotD = 7.0f * px;
    canvas.setColor(tel.sounding ? accent : colour::textFaint);
    canvas.circle(hudX + 10.0f * px, hudY + 11.0f * px, dotD);

    const char* modeName = modeIdx == 0 ? "SPIRAL"
                                        : (modeIdx == 1 ? "COPEDENT"
                                                        : "M\xc3\x96"
                                                          "BIUS");
    char line1[64]{};
    std::snprintf(line1, sizeof(line1), "%s  \xc2\xb7  BAR %d.%d  \xc2\xb7  %.0f BPM", modeName, tel.barNumber,
                  tel.beatNumber, static_cast<double>(tel.bpm));
    drawLabel(canvas, context_, line1, hudX + 22.0f * px, hudY + 4.0f * px, hudW - 30.0f * px, 17.0f * px, colour::text,
              visage::Font::kLeft, 9.8f, Weight::semibold);

    char line2[72]{};
    if (modeIdx == 0) {
        const float bellHz = tel.sounding ? tel.effectiveBellHz : context_.model.get(ParamId::bellCenter);
        std::snprintf(line2, sizeof(line2), "Bell %.0f Hz  \xc2\xb7  Speed %+.1f\xc3\x97  \xc2\xb7  Sub %.0f%%",
                      static_cast<double>(bellHz), static_cast<double>(tel.spiralVel),
                      static_cast<double>(tel.effectiveSubAnchor * 100.0f));
    } else {
        const int rootPc = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::rootKey))), 0, 11);
        const float p0 = tel.voicePitches[0] > 1.0f ? tel.voicePitches[0] : static_cast<float>(36 + rootPc);
        const float p1 = tel.voicePitches[1] > 1.0f ? tel.voicePitches[1] : p0 + 7.0f;
        const float p2 = tel.voicePitches[2] > 1.0f ? tel.voicePitches[2] : p0 + 12.0f;
        const char scChar = static_cast<char>('A' + std::clamp(tel.activeScene, 0, kSceneCount - 1));
        std::snprintf(line2, sizeof(line2), "Scene %c  \xc2\xb7  %s \xc2\xb7 %s \xc2\xb7 %s  \xc2\xb7  Halo %.0f%%",
                      scChar, midiNoteName(p0).c_str(), midiNoteName(p1).c_str(), midiNoteName(p2).c_str(),
                      static_cast<double>(tel.effectiveSpace * 100.0f));
    }
    drawLabel(canvas, context_, line2, hudX + 10.0f * px, hudY + 21.0f * px, hudW - 18.0f * px, 16.0f * px,
              colour::textDim, visage::Font::kLeft, 9.2f, Weight::medium);

    // Left live spectrum / trajectory scope well
    const float plotX = 8.0f * px;
    const float plotY = 7.0f * px;
    const float plotW = std::max(40.0f, hudX - plotX - 8.0f * px);
    const float plotH = hudH;
    canvas.setColor(0xff070a0fu);
    canvas.roundedRectangle(plotX, plotY, plotW, plotH, metric::radiusSmall * px);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(plotX, plotY, plotW, plotH, metric::radiusSmall * px, 1.0f * px);

    canvas.saveState();
    canvas.trimClampBounds(plotX, plotY, plotW, plotH);

    if (modeIdx == 0) {
        // Live 7-Octave Shepard-Risset Spectral Bell + Sub-Anchor Floor + Gliding Partials
        const int rootPc = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::rootKey))), 0, 11);
        const float subA = context_.model.get(ParamId::subAnchor) * 0.01f;
        const float bellHz = tel.sounding ? tel.effectiveBellHz : context_.model.get(ParamId::bellCenter);
        const float bellW = context_.model.get(ParamId::bellWidth) * 0.01f;
        const int ratioIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::spiralRatio))), 0, 4);
        const double ratioSemi = kSpiralRatioSemitones[static_cast<std::size_t>(ratioIdx)];
        const double baseHz = 440.0 * std::pow(2.0, (12.0 + static_cast<double>(rootPc) - 69.0) / 12.0);
        const double centerU = std::clamp(
            (12.0 * std::log2(std::max(16.0, static_cast<double>(bellHz)) / baseHz)) / ratioSemi, 0.75, 6.25);
        const double sigma = 0.65 + static_cast<double>(bellW) * 2.25;

        // Sub-Anchor floor tint
        if (subA > 0.04f) {
            const float sx1 = plotX + (0.65f / 7.0f) * plotW;
            const float sx2 = plotX + (2.15f / 7.0f) * plotW;
            canvas.setColor(colour::withAlpha(colour::mobius, 0.11f + 0.10f * subA));
            canvas.rectangle(sx1, plotY, sx2 - sx1, plotH);
        }

        // 7 octave dividers
        for (int o = 1; o < kOctaveCount; ++o) {
            const float gx = plotX + (static_cast<float>(o) / static_cast<float>(kOctaveCount)) * plotW;
            canvas.setColor(colour::withAlpha(colour::text, 0.06f));
            canvas.rectangle(gx, plotY, 1.0f * px, plotH);
        }

        std::array<float, 96> bellCurve{};
        for (std::size_t i = 0; i < bellCurve.size(); ++i) {
            const double u = (static_cast<double>(i) / static_cast<double>(bellCurve.size() - 1)) * 7.0;
            const double edge = std::sin(hba::dsp::kPi * (u / 7.0));
            const double tilt = std::exp(-0.5 * ((u - centerU) / sigma) * ((u - centerU) / sigma));
            bellCurve[i] = static_cast<float>(std::clamp(edge * edge * tilt * 0.92, 0.0, 1.0));
        }
        hba::ui::drawCurve(canvas, px, bellCurve, plotX, plotY + 4.0f * px, plotW, plotH - 6.0f * px, accent, 0.0f,
                           1.0f, 0.16f + 0.14f * pulse_, 0.0f, 1.7f);

        // Live gliding partial pillars
        const float gateVis = 0.45f + 0.55f * std::clamp(tel.gateLevel, 0.0f, 1.0f);
        for (int k = 0; k < kOctaveCount; ++k) {
            const double u = hba::dsp::frac((static_cast<double>(k) + static_cast<double>(tel.spiralPos)) / 7.0) * 7.0;
            const double edge = std::sin(hba::dsp::kPi * (u / 7.0));
            const double tilt = std::exp(-0.5 * ((u - centerU) / sigma) * ((u - centerU) / sigma));
            const float amp = static_cast<float>(std::clamp(edge * edge * tilt, 0.0, 1.0));
            const bool inSub = (subA > 0.05f && u >= 0.65 && u <= 2.15);
            const unsigned barCol = inSub ? colour::mobius : accent;
            const float bx = plotX + static_cast<float>(u / 7.0) * plotW;
            const float bh = amp * (plotH - 8.0f * px) * (inSub ? 1.0f : gateVis);
            const float bw = 3.2f * px;
            canvas.setColor(colour::withAlpha(barCol, 0.35f + 0.65f * amp));
            canvas.roundedRectangle(bx - bw * 0.5f, plotY + plotH - 3.0f * px - bh, bw, bh, bw * 0.5f);
        }
    } else {
        // Live 5-Voice Copedent / Mobius Mini Pitch Trajectory Ribbon
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
        const bool mobius = (modeIdx == 2);
        const SceneMatrix mat = context_.model.sceneMatrix();
        const double refRoot = 36.0 + static_cast<double>(rootPc);

        const int segTotal = mobius ? kSceneCount : (kSceneCount - 1);
        for (int s = 1; s < segTotal; ++s) {
            const float sx = plotX + (static_cast<float>(s) / static_cast<float>(segTotal)) * plotW;
            canvas.setColor(colour::withAlpha(colour::text, 0.10f));
            canvas.rectangle(sx, plotY, 1.0f * px, plotH);
        }

        constexpr int kSteps = 64;
        for (int v = 0; v < kVoiceCount; ++v) {
            const auto vi = static_cast<std::size_t>(v);
            visage::Path ribbon;
            for (int i = 0; i <= kSteps; ++i) {
                const double mRaw = static_cast<double>(i) / static_cast<double>(kSteps);
                const double m = mobius ? std::min(mRaw, 0.9999) : mRaw;
                const auto eval = evaluateMorphPitches(mat, refRoot, rootPc, scaleIdx, m, gravity, gravMode, stagger,
                                                       order, anchorSub, mobius);
                const float off = static_cast<float>(eval.pitches[vi] - refRoot);
                const float normY = std::clamp((off + 12.0f) / 42.0f, 0.0f, 1.0f);
                const float rx = static_cast<float>(mRaw) * plotW;
                const float ry = plotH - 5.0f * px - normY * (plotH - 10.0f * px);
                if (i == 0)
                    ribbon.moveTo(rx, ry);
                else
                    ribbon.lineTo(rx, ry);
            }
            canvas.setColor(colour::withAlpha(colour::voices[vi], 0.85f));
            canvas.stroke(ribbon, plotX, plotY, plotW, plotH, 1.5f * px);
        }

        const float playX = plotX + std::clamp(tel.morphPos, 0.0f, 1.0f) * plotW;
        canvas.setColor(colour::withAlpha(colour::text, 0.72f));
        canvas.rectangle(playX - 0.75f * px, plotY, 1.5f * px, plotH);
    }

    // Bottom bar phase progress strip
    const float progH = 2.5f * px;
    canvas.setColor(colour::withAlpha(accent, 0.22f));
    canvas.rectangle(plotX, plotY + plotH - progH, plotW, progH);
    canvas.setColor(accent);
    canvas.rectangle(plotX, plotY + plotH - progH, plotW * std::clamp(tel.barPhase, 0.0f, 1.0f), progH);

    canvas.restoreState();
}

std::unique_ptr<Panel> makeManifoldRibbon(Context& ctx) { return std::make_unique<ManifoldRibbon>(ctx); }

} // namespace tg::ui
