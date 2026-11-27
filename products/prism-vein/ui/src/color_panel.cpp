// SPDX-License-Identifier: AGPL-3.0-or-later
// COLOR hero surface: 6 sculptural macro HeroKnobs + tactile 4-Engine selector
// + Shield / Fan / Motion strip. Zero duplicated controls, zero helper prose.
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pv::ui {

namespace {

class ColorPanel final : public Panel {
  public:
    explicit ColorPanel(Context& ctx) : Panel(ctx) {
        // 6 Hero Macro Dials
        macros_[0] = &add<HeroKnob>(ctx, ParamId::color, colour::prism);
        macros_[1] = &add<HeroKnob>(ctx, ParamId::decay, colour::harmonic);
        macros_[2] = &add<HeroKnob>(ctx, ParamId::tame, colour::rose);
        macros_[3] = &add<HeroKnob>(ctx, ParamId::shift, colour::prism);
        macros_[4] = &add<HeroKnob>(ctx, ParamId::grit, colour::tame);
        macros_[5] = &add<HeroKnob>(ctx, ParamId::subLockHz, colour::sub);

        // 4 Engine Mode tactile selector pills
        for (int i = 0; i < 4; ++i) {
            auto& btn = add<PillButton>(ctx, std::string(choices::engineMode[static_cast<std::size_t>(i)]), [this, i] {
                context_.model.begin(ParamId::engineMode);
                context_.model.set(ParamId::engineMode, static_cast<float>(i));
                context_.model.end(ParamId::engineMode);
                syncEngineButtons();
            });
            btn.setFontSize(10.0f);
            engineBtns_[static_cast<std::size_t>(i)] = &btn;
        }

        // 3 Performance Strip rows right under Engine selector
        shieldRow_ = &add<Row>(ctx, ParamId::transientShield, colour::tame);
        fanRow_ = &add<Row>(ctx, ParamId::stereoFan, colour::prism);
        motionRow_ = &add<Row>(ctx, ParamId::motion, colour::harmonic);

        syncEngineButtons();
    }

    void tick() override { syncEngineButtons(); }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float sc = context_.scale;
        const float splitX = macroAreaWidth();

        // Crisp vertical divider separating the 6 Hero Dials from the Engine card
        canvas.setColor(colour::edge);
        canvas.fill(splitX, 14.0f * sc, 1.0f * sc, height() - 28.0f * sc);
    }

    void resized() override {
        const float sc = context_.scale;
        const float pad = 12.0f * sc;
        const float splitX = macroAreaWidth();

        // Left: 6 equal-spaced sculptural HeroKnobs
        const float macroW = (splitX - 2.0f * pad) / 6.0f;
        const float knobH = std::min(height() - 24.0f * sc, 152.0f * sc);
        const float knobY = (height() - knobH) * 0.5f;
        for (int i = 0; i < 6; ++i) {
            macros_[static_cast<std::size_t>(i)]->setBounds(pad + static_cast<float>(i) * macroW, knobY, macroW, knobH);
        }

        // Right: 2x2 Engine selector pills + 3 sleek rows (Shield, Fan, Motion)
        const float rx = splitX + 14.0f * sc;
        const float rw = std::max(120.0f * sc, width() - rx - pad);
        const float gap = 6.0f * sc;
        const float btnW = (rw - gap) * 0.5f;
        const float btnH = 28.0f * sc;
        const float topY = 14.0f * sc;

        for (int i = 0; i < 4; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            engineBtns_[static_cast<std::size_t>(i)]->setBounds(
                rx + static_cast<float>(col) * (btnW + gap), topY + static_cast<float>(row) * (btnH + gap), btnW, btnH);
        }

        const float rowsTop = topY + 2.0f * (btnH + gap) + 8.0f * sc;
        const float rowH = std::max(22.0f * sc, (height() - rowsTop - 12.0f * sc - 8.0f * sc) / 3.0f);
        shieldRow_->setBounds(rx, rowsTop, rw, rowH);
        fanRow_->setBounds(rx, rowsTop + rowH + 4.0f * sc, rw, rowH);
        motionRow_->setBounds(rx, rowsTop + 2.0f * (rowH + 4.0f * sc), rw, rowH);
    }

  private:
    float macroAreaWidth() const { return width() * 0.71f; }

    void syncEngineButtons() {
        const int active = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::engineMode))), 0, 3);
        if (active == shownEngine_)
            return;
        shownEngine_ = active;
        for (int i = 0; i < 4; ++i) {
            const bool isSel = (i == active);
            auto* b = engineBtns_[static_cast<std::size_t>(i)];
            b->setActive(isSel);
            b->setAccent(isSel ? colour::engineAccent(i) : colour::textDim);
        }
    }

    std::array<HeroKnob*, 6> macros_{};
    std::array<PillButton*, 4> engineBtns_{};
    Row* shieldRow_ = nullptr;
    Row* fanRow_ = nullptr;
    Row* motionRow_ = nullptr;
    int shownEngine_ = -1;
};

} // namespace

std::unique_ptr<visage::Frame> makeColorPanel(Context& context) { return std::make_unique<ColorPanel>(context); }

} // namespace pv::ui
