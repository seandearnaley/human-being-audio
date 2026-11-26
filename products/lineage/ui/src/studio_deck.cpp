// SPDX-License-Identifier: AGPL-3.0-or-later
// STUDIO 3-Card Precision Deck: GEN 0 Source & Sub | GEN 1 Sculpt & Density | GEN 2 Strata & Output.
#include "panels.h"

#include <array>
#include <cmath>
#include <string>

namespace ln::ui {

namespace {
class StudioDeck final : public Panel {
  public:
    explicit StudioDeck(Context& ctx)
        : Panel(ctx),
          // Card 1: GEN 0 · Source & Sub
          coreMode_(add<Row>(ctx, ParamId::coreMode, colour::reese, "Core")),
          octave_(add<Row>(ctx, ParamId::octave, colour::reese, "Octave")),
          quality_(add<Row>(ctx, ParamId::quality, colour::reese, "OS")),
          unison_(add<Knob>(ctx, ParamId::unison, colour::reese)),
          detune_(add<Knob>(ctx, ParamId::detune, colour::reese)),
          tone_(add<Knob>(ctx, ParamId::tone, colour::reese)),
          subLevel_(add<Knob>(ctx, ParamId::subLevel, colour::sub)),
          glide_(add<Row>(ctx, ParamId::glide, colour::reese, "Glide", true)),
          attack_(add<Row>(ctx, ParamId::attack, colour::reese, "Attack", true)),
          release_(add<Row>(ctx, ParamId::release, colour::reese, "Release", true)),
          subWarmth_(add<Row>(ctx, ParamId::subWarmth, colour::sub, "Sub Warmth", true)),
          subSplit_(add<Row>(ctx, ParamId::subSplit, colour::sub, "Sub Split", true)),
          subPhase_(add<Row>(ctx, ParamId::subPhase, colour::sub, "Sub Phase", true)),

          // Card 2: GEN 1 · Sculpt & Density
          filt1Mode_(add<Row>(ctx, ParamId::filt1Mode, colour::sculpt, "Filt 1")),
          filt2Mode_(add<Row>(ctx, ParamId::filt2Mode, colour::reese, "Filt 2")),
          filt1Hz_(add<Knob>(ctx, ParamId::filt1Hz, colour::sculpt)),
          drive_(add<Knob>(ctx, ParamId::drive, colour::sculpt)),
          filt2Hz_(add<Knob>(ctx, ParamId::filt2Hz, colour::reese)),
          density_(add<Knob>(ctx, ParamId::density, colour::abyss)),
          filt1Res_(add<Row>(ctx, ParamId::filt1Res, colour::sculpt, "F1 Res", true)),
          driveType_(add<Row>(ctx, ParamId::driveType, colour::sculpt, "Shaper")),
          asym_(add<Row>(ctx, ParamId::asym, colour::sculpt, "Asym", true)),
          filt2Res_(add<Row>(ctx, ParamId::filt2Res, colour::reese, "F2 Res", true)),
          disperse_(add<Row>(ctx, ParamId::disperse, colour::sub, "Disperse", true)),
          width_(add<Row>(ctx, ParamId::width, colour::reese, "Width", true)),

          // Card 3: GEN 2 · Strata & Output
          strataDir_(add<Row>(ctx, ParamId::strataDir, colour::strata, "Dir")),
          strataBlend_(add<Row>(ctx, ParamId::strataBlend, colour::strata, "Xover")),
          printBars_(add<Row>(ctx, ParamId::printBars, colour::strata, "Len")),
          genMix_(add<Knob>(ctx, ParamId::genMix, colour::strata)),
          strataPos_(add<Knob>(ctx, ParamId::strataPos, colour::strata)),
          strataSpan_(add<Knob>(ctx, ParamId::strataSpan, colour::strata)),
          motion_(add<Knob>(ctx, ParamId::motion, colour::abyss)),
          strataPitch_(add<Row>(ctx, ParamId::strataPitch, colour::strata, "Pitch", true)),
          strataFade_(add<Row>(ctx, ParamId::strataFade, colour::strata, "Loop Xfade", true)),
          postDrive_(add<Row>(ctx, ParamId::postDrive, colour::sculpt, "Post Drive", true)),
          postTone_(add<Row>(ctx, ParamId::postTone, colour::sculpt, "Post Tone", true)),
          outputGain_(add<Row>(ctx, ParamId::outputGain, colour::text, "Output", true)),
          ceiling_(add<Row>(ctx, ParamId::ceiling, colour::alert, "Ceiling", true)),
          strataKeytrack_(add<Row>(ctx, ParamId::strataKeytrack, colour::strata, "Track")),
          limit_(add<Row>(ctx, ParamId::limit, colour::alert, "Limit")) {
        // Live modulation arc providers on modulated controls
        tone_.setModProvider([this] { return toNormalised(spec(ParamId::tone), context_.model.visuals().effTone); });
        filt1Hz_.setModProvider(
            [this] { return toNormalised(spec(ParamId::filt1Hz), context_.model.visuals().effFilt1Hz); });
        drive_.setModProvider(
            [this] { return toNormalised(spec(ParamId::drive), context_.model.visuals().effDrive); });
        filt2Hz_.setModProvider(
            [this] { return toNormalised(spec(ParamId::filt2Hz), context_.model.visuals().effFilt2Hz); });
        density_.setModProvider(
            [this] { return toNormalised(spec(ParamId::density), context_.model.visuals().effDensity); });
        genMix_.setModProvider(
            [this] { return toNormalised(spec(ParamId::genMix), context_.model.visuals().effGenMix); });
        strataPos_.setModProvider(
            [this] { return toNormalised(spec(ParamId::strataPos), context_.model.visuals().effStrataPos); });
        strataSpan_.setModProvider(
            [this] { return toNormalised(spec(ParamId::strataSpan), context_.model.visuals().effStrataSpan); });
        subWarmth_.setModProvider(
            [this] { return toNormalised(spec(ParamId::subWarmth), context_.model.visuals().effSubWarmth); });
        disperse_.setModProvider(
            [this] { return toNormalised(spec(ParamId::disperse), context_.model.visuals().effDisperse); });
        width_.setModProvider([this] { return toNormalised(spec(ParamId::width), context_.model.visuals().effWidth); });
    }

    void resized() override {
        const float gap = px(10.0f);
        const float cardW = std::floor((width() - 2.0f * gap) / 3.0f);

        const auto placeCard = [&](int idx, std::array<Knob*, 4> knobs, std::array<Row*, 3> colL,
                                   std::array<Row*, 3> colR) {
            const float cx = static_cast<float>(idx) * (cardW + gap);
            cardX_[static_cast<std::size_t>(idx)] = cx;
            cardW_[static_cast<std::size_t>(idx)] = (idx == 2) ? (width() - cx) : cardW;
            const float cw = cardW_[static_cast<std::size_t>(idx)];
            const float pad = px(12.0f);
            const float innerW = cw - 2.0f * pad;
            const float colGap = px(14.0f);
            const float halfW = (innerW - colGap) * 0.5f;
            const float rowH = px(23.0f);

            // Top choice selectors
            const float selY = px(36.0f);
            if (idx == 0) {
                coreMode_.setBounds(cx + pad, selY, halfW, rowH);
                octave_.setBounds(cx + pad + halfW + colGap, selY, halfW, rowH);
                quality_.setBounds(cx + cw - pad - px(64.0f), px(8.0f), px(64.0f), px(18.0f));
            } else if (idx == 1) {
                filt1Mode_.setBounds(cx + pad, selY, halfW, rowH);
                filt2Mode_.setBounds(cx + pad + halfW + colGap, selY, halfW, rowH);
            } else {
                const float triGap = px(10.0f);
                const float triW = (innerW - 2.0f * triGap) / 3.0f;
                strataDir_.setBounds(cx + pad, selY, triW, rowH);
                strataBlend_.setBounds(cx + pad + triW + triGap, selY, triW, rowH);
                printBars_.setBounds(cx + pad + 2.0f * (triW + triGap), selY, triW, rowH);
                limit_.setBounds(cx + cw - pad - px(62.0f), px(8.0f), px(62.0f), px(18.0f));
                strataKeytrack_.setBounds(cx + cw - pad - px(62.0f + 10.0f + 68.0f), px(8.0f), px(68.0f), px(18.0f));
            }

            // 4 Hero knobs
            const float knobY = px(68.0f);
            const float knobH = px(78.0f);
            const float knobGap = px(6.0f);
            const float knobW = (innerW - 3.0f * knobGap) / 4.0f;
            for (int k = 0; k < 4; ++k) {
                knobs[static_cast<std::size_t>(k)]->setBounds(
                    cx + pad + static_cast<float>(k) * (knobW + knobGap), knobY, knobW, knobH);
            }

            // 2 columns x 3 precision rows
            const float rowsTop = px(158.0f);
            const float rowPitch = px(34.0f);
            for (int r = 0; r < 3; ++r) {
                const float ry = rowsTop + static_cast<float>(r) * rowPitch;
                colL[static_cast<std::size_t>(r)]->setBounds(cx + pad, ry, halfW, rowH);
                colR[static_cast<std::size_t>(r)]->setBounds(cx + pad + halfW + colGap, ry, halfW, rowH);
            }
        };

        placeCard(0, {&unison_, &detune_, &tone_, &subLevel_}, {&glide_, &attack_, &release_},
                  {&subWarmth_, &subSplit_, &subPhase_});
        placeCard(1, {&filt1Hz_, &drive_, &filt2Hz_, &density_}, {&filt1Res_, &driveType_, &asym_},
                  {&filt2Res_, &disperse_, &width_});
        placeCard(2, {&genMix_, &strataPos_, &strataSpan_, &motion_}, {&strataPitch_, &strataFade_, &postDrive_},
                  {&postTone_, &outputGain_, &ceiling_});
    }

    void draw(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        constexpr std::array<const char*, 3> kTitles{
            "GEN 0 \xc2\xb7 SOURCE & SUB",
            "GEN 1 \xc2\xb7 SCULPT & DENSITY",
            "GEN 2 \xc2\xb7 STRATA & OUTPUT",
        };
        constexpr std::array<unsigned, 3> kAccents{colour::reese, colour::sculpt, colour::strata};

        for (std::size_t i = 0; i < 3; ++i) {
            const float cx = cardX_[i];
            const float cw = cardW_[i];
            drawPanel(canvas, context_, cx, 0.0f, cw, height(), colour::panel, metric::radius);

            canvas.setColor(colour::withAlpha(kAccents[i], 0.72f));
            canvas.rectangle(cx + px(12.0f), px(11.0f), px(3.0f), px(13.0f));
            drawLabel(canvas, context_, kTitles[i], cx + px(21.0f), px(8.0f), cw - px(160.0f), px(18.0f), kAccents[i],
                      visage::Font::kLeft, 9.6f, Weight::semibold);

            canvas.setColor(colour::withAlpha(colour::edge, 0.75f));
            canvas.rectangle(cx + px(12.0f), px(30.0f), cw - px(24.0f), 1.0f * sc);

            canvas.setColor(colour::withAlpha(colour::edge, 0.45f));
            canvas.rectangle(cx + px(12.0f), px(151.0f), cw - px(24.0f), 1.0f * sc);
        }
    }

  private:
    Row &coreMode_, &octave_, &quality_;
    Knob &unison_, &detune_, &tone_, &subLevel_;
    Row &glide_, &attack_, &release_, &subWarmth_, &subSplit_, &subPhase_;

    Row &filt1Mode_, &filt2Mode_;
    Knob &filt1Hz_, &drive_, &filt2Hz_, &density_;
    Row &filt1Res_, &driveType_, &asym_, &filt2Res_, &disperse_, &width_;

    Row &strataDir_, &strataBlend_, &printBars_;
    Knob &genMix_, &strataPos_, &strataSpan_, &motion_;
    Row &strataPitch_, &strataFade_, &postDrive_, &postTone_, &outputGain_, &ceiling_, &strataKeytrack_, &limit_;

    std::array<float, 3> cardX_{};
    std::array<float, 3> cardW_{};
};
} // namespace

std::unique_ptr<Panel> makeStudioDeck(Context& context) { return std::make_unique<StudioDeck>(context); }

} // namespace ln::ui
