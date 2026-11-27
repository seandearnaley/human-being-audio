// SPDX-License-Identifier: AGPL-3.0-or-later
// LATTICE precision surface: 4-column spectral/carrier/tame/output bench above
// the shared 4-lane tempo-synced StudioMovementPanel.
#include "panels.h"

#include <array>
#include <string_view>
#include <vector>

namespace pv::ui {

namespace {

class BenchColumn final : public Panel {
  public:
    BenchColumn(Context& ctx, std::string_view title, unsigned accent, std::initializer_list<ParamId> params)
        : Panel(ctx), title_(title), accent_(accent) {
        for (ParamId id : params)
            rows_.push_back(&add<Row>(ctx, id, accent));
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float sc = context_.scale;
        const float pad = 10.0f * sc;

        // Top accent indicator bar + crisp uppercase header
        canvas.setColor(accent_);
        canvas.roundedRectangle(pad, 9.0f * sc, 3.0f * sc, 12.0f * sc, 1.5f * sc);
        drawLabel(canvas, context_, title_, pad + 8.0f * sc, 6.0f * sc, width() - 2.0f * pad - 8.0f * sc, 18.0f * sc,
                  colour::text, visage::Font::kLeft, 10.2f, Weight::semibold);

        canvas.setColor(colour::edge);
        canvas.fill(pad, 28.0f * sc, width() - 2.0f * pad, 1.0f * sc);
    }

    void resized() override {
        const float sc = context_.scale;
        const float pad = 10.0f * sc;
        const float top = 34.0f * sc;
        const float availH = std::max(40.0f * sc, height() - top - 8.0f * sc);
        const float gap = 4.0f * sc;
        const int count = static_cast<int>(rows_.size());
        if (count <= 0)
            return;
        const float rowH =
            std::min(30.0f * sc, (availH - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
        for (int i = 0; i < count; ++i) {
            rows_[static_cast<std::size_t>(i)]->setBounds(pad, top + static_cast<float>(i) * (rowH + gap),
                                                          width() - 2.0f * pad, rowH);
        }
    }

  private:
    std::string title_;
    unsigned accent_;
    std::vector<Row*> rows_;
};

class LatticePanel final : public visage::Frame {
  public:
    explicit LatticePanel(Context& ctx) : context_(ctx) {
        cols_[0] = std::make_unique<BenchColumn>(
            ctx, "01 SPECTRUM", colour::prism,
            std::initializer_list<ParamId>{ParamId::harmonics, ParamId::tilt, ParamId::qSharpness, ParamId::detune,
                                           ParamId::octaveShift, ParamId::glideMs});
        cols_[1] = std::make_unique<BenchColumn>(
            ctx, "02 CARRIER \xc2\xb7 SHIELD", colour::harmonic,
            std::initializer_list<ParamId>{ParamId::morphBlend, ParamId::carrierTilt, ParamId::shieldSens,
                                           ParamId::inHarmMix, ParamId::shimmer});
        cols_[2] = std::make_unique<BenchColumn>(ctx, "03 TAME \xc2\xb7 GRIT", colour::tame,
                                                 std::initializer_list<ParamId>{ParamId::tameFocusHz,
                                                                                ParamId::tameSelect, ParamId::gritType,
                                                                                ParamId::airDb, ParamId::subLevelDb});
        cols_[3] = std::make_unique<BenchColumn>(ctx, "04 OUTPUT", colour::sub,
                                                 std::initializer_list<ParamId>{ParamId::inputDb, ParamId::outputDb,
                                                                                ParamId::ceilingDb, ParamId::limitOn,
                                                                                ParamId::velToColor});

        for (auto& c : cols_)
            addChild(c.get());

        movement_ = makeMovementPanel(ctx);
        addChild(movement_.get());
    }

    void resized() override {
        const float gap = context_.px(metric::gap);
        const float moveH = context_.px(176.0f);
        const float topH = std::max(context_.px(120.0f), height() - moveH - gap);
        const float colW = (width() - 3.0f * gap) * 0.25f;

        for (int i = 0; i < 4; ++i) {
            cols_[static_cast<std::size_t>(i)]->setBounds(static_cast<float>(i) * (colW + gap), 0.0f, colW, topH);
        }
        movement_->setBounds(0.0f, topH + gap, width(), moveH);
    }

  private:
    Context& context_;
    std::array<std::unique_ptr<BenchColumn>, 4> cols_{};
    std::unique_ptr<Panel> movement_;
};

} // namespace

std::unique_ptr<visage::Frame> makeLatticePanel(Context& context) { return std::make_unique<LatticePanel>(context); }

} // namespace pv::ui
