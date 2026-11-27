// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <array>
#include <cstdio>
#include <memory>

namespace dt::ui {

namespace {

class SculptPanel final : public visage::Frame {
  public:
    explicit SculptPanel(Context& ctx) : context_(ctx) {
        constexpr std::array<std::pair<ParamId, unsigned>, 6> kMacros{{
            {ParamId::tuneHz, colour::sub},
            {ParamId::punchAmt, colour::body},
            {ParamId::lengthMs, colour::sub},
            {ParamId::clickAmt, colour::impact},
            {ParamId::drive, colour::body},
            {ParamId::tight, colour::sidechain},
        }};

        for (std::size_t i = 0; i < kMacros.size(); ++i) {
            knobs_[i] = std::make_unique<HeroKnob>(ctx, kMacros[i].first, kMacros[i].second);
            addChild(knobs_[i].get());
        }
        knobs_[0]->setMainTextProvider([](float hz) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%.1f Hz", static_cast<double>(hz));
            return std::string(buf);
        });
        knobs_[0]->setSubTextProvider([](float hz) { return noteNameForHz(hz); });

        constexpr std::array<std::pair<const char*, unsigned>, 7> kArchetypes{{
            {"Neuro", colour::sub},
            {"808", colour::body},
            {"909", colour::body},
            {"Tech", colour::sidechain},
            {"Half", colour::impact},
            {"Sub", colour::sub},
            {"Click", colour::impact},
        }};

        for (int i = 0; i < 7; ++i) {
            archetypes_[static_cast<std::size_t>(i)] =
                std::make_unique<PillButton>(ctx, kArchetypes[static_cast<std::size_t>(i)].first, [this, i] {
                    context_.model.applyArchetype(i);
                    context_.model.triggerAudition(-1, 0.96f);
                });
            archetypes_[static_cast<std::size_t>(i)]->setAccent(kArchetypes[static_cast<std::size_t>(i)].second);
            archetypes_[static_cast<std::size_t>(i)]->setFontSize(10.0f);
            addChild(archetypes_[static_cast<std::size_t>(i)].get());
        }

        mutateBtn_ = std::make_unique<PillButton>(ctx, "Mutate", [this] {
            context_.model.mutateKick();
            context_.model.triggerAudition(-1, 0.96f);
        });
        mutateBtn_->setAccent(colour::body);
        mutateBtn_->setFontSize(10.0f);
        addChild(mutateBtn_.get());
    }

    void draw(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

        // Crisp vertical divider between 6 Hero Macros and Quick-Start Archetype Matrix
        const float splitX = width() - 344.0f * sc;
        canvas.setColor(colour::edge);
        canvas.rectangle(splitX, 14.0f * sc, 1.0f * sc, height() - 28.0f * sc);

        // Recessed matrix backdrop on right
        drawPanel(canvas, context_, splitX + 12.0f * sc, 14.0f * sc, width() - splitX - 24.0f * sc,
                  height() - 28.0f * sc, colour::window, metric::radiusSmall);
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float splitX = width() - px(344.0f);
        const float padX = px(18.0f);
        const float knobAreaW = splitX - 2.0f * padX;
        const float colW = knobAreaW / 6.0f;
        const float knobW = std::min(colW - px(8.0f), px(134.0f));
        const float knobH = height() - px(24.0f);
        const float knobY = px(12.0f);

        for (std::size_t i = 0; i < knobs_.size(); ++i) {
            const float cx = padX + (static_cast<float>(i) + 0.5f) * colW;
            knobs_[i]->setBounds(cx - knobW * 0.5f, knobY, knobW, knobH);
        }

        // 2x4 Archetype Matrix on right
        const float mx = splitX + px(22.0f);
        const float mw = width() - mx - px(22.0f);
        const float my = px(26.0f);
        const float mh = height() - px(52.0f);
        const float gapX = px(8.0f);
        const float gapY = px(10.0f);
        const float bw = (mw - 3.0f * gapX) / 4.0f;
        const float bh = (mh - gapY) * 0.5f;

        for (int i = 0; i < 7; ++i) {
            const int row = i / 4;
            const int col = i % 4;
            archetypes_[static_cast<std::size_t>(i)]->setBounds(mx + static_cast<float>(col) * (bw + gapX),
                                                                my + static_cast<float>(row) * (bh + gapY), bw, bh);
        }
        mutateBtn_->setBounds(mx + 3.0f * (bw + gapX), my + (bh + gapY), bw, bh);
    }

  private:
    Context& context_;
    std::array<std::unique_ptr<HeroKnob>, 6> knobs_;
    std::array<std::unique_ptr<PillButton>, 7> archetypes_;
    std::unique_ptr<PillButton> mutateBtn_;
};

} // namespace

std::unique_ptr<visage::Frame> makeSculptPanel(Context& context) { return std::make_unique<SculptPanel>(context); }

} // namespace dt::ui
