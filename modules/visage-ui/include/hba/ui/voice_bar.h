// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical studio bottom Voice / Playability / Quality footer bar shared across all HBA instruments.
#pragma once

#include <hba/ui/controls.h>
#include <hba/ui/theme.h>
#include <visage/ui.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hba::ui {

inline constexpr float kVoiceBarHeight = 34.0f;

template <typename ContextT, typename ParamFrameT, typename ParamIdT, typename ParamKindT>
class StudioVoiceBar final : public visage::Frame, public Live {
  public:
    using RowType = StandardRow<ParamFrameT, ParamKindT>;

    struct Slot {
        ParamIdT id;
        std::string label;
        unsigned accent = colour::textDim;
        float designWidth = 112.0f;
        bool showBar = false;
        bool rightAlign = false;
    };

    StudioVoiceBar(ContextT& ctx, unsigned badgeAccent, std::string title, std::vector<Slot> slots,
                   std::function<std::string()> statusProvider = {})
        : context_(ctx), badgeAccent_(badgeAccent), title_(std::move(title)),
          statusProvider_(std::move(statusProvider)) {
        slots_.reserve(slots.size());
        for (auto& s : slots) {
            auto row = std::make_unique<RowType>(ctx, s.id, s.accent, s.label, s.showBar);
            addChild(row.get());
            slots_.push_back({std::move(s), std::move(row)});
        }
        context_.live.push_back(this);
        tick();
    }

    ~StudioVoiceBar() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        if (statusProvider_) {
            std::string next = statusProvider_();
            if (next != statusText_) {
                statusText_ = std::move(next);
                redraw();
            }
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float h = height();
        const float rh = px(22.0f);
        const float ry = (h - rh) * 0.5f;
        const float sepPad = px(12.0f);

        dividers_.clear();

        // Left identity badge + optional live status pill
        float lx = px(statusProvider_ ? 140.0f : 72.0f);
        dividers_.push_back(lx - px(6.0f));
        lx += px(4.0f);

        for (auto& item : slots_) {
            if (item.spec.rightAlign)
                continue;
            const float w = px(item.spec.designWidth);
            item.row->setBounds(lx, ry, w, rh);
            lx += w + sepPad;
            dividers_.push_back(lx - sepPad * 0.5f);
        }

        // Right-aligned slots (e.g. OS Quality) anchored cleanly to far right
        float rx = width() - px(12.0f);
        for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
            if (!it->spec.rightAlign)
                continue;
            const float w = px(it->spec.designWidth);
            rx -= w;
            it->row->setBounds(rx, ry, w, rh);
            rx -= sepPad;
            dividers_.push_back(rx + sepPad * 0.5f);
        }
    }

    void draw(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        drawPanel(canvas, sc, 0.0f, 0.0f, width(), height(), colour::panel, metric::radiusSmall);

        // Subtle luminous top hairline
        canvas.setColor(colour::withAlpha(badgeAccent_, 0.24f));
        canvas.rectangle(8.0f * sc, 0.0f, width() - 16.0f * sc, 1.0f * sc);

        // Left vertical accent rail + VOICE title + live status readout
        canvas.setColor(colour::withAlpha(badgeAccent_, 0.80f));
        canvas.roundedRectangle(10.0f * sc, 9.0f * sc, 3.0f * sc, height() - 18.0f * sc, 1.5f * sc);

        drawLabel(canvas, sc, title_, 19.0f * sc, 0.0f, 48.0f * sc, height(), badgeAccent_, visage::Font::kLeft, 9.0f,
                  Weight::semibold);

        if (statusProvider_ && !statusText_.empty()) {
            const float pillX = 64.0f * sc;
            const float pillW = 66.0f * sc;
            const float pillH = 20.0f * sc;
            const float pillY = (height() - pillH) * 0.5f;
            canvas.setColor(colour::raised);
            canvas.roundedRectangle(pillX, pillY, pillW, pillH, 4.0f * sc);
            canvas.setColor(colour::withAlpha(badgeAccent_, 0.30f));
            canvas.roundedRectangleBorder(pillX, pillY, pillW, pillH, 4.0f * sc, 1.0f * sc);
            drawLabel(canvas, sc, statusText_, pillX + 3.0f * sc, pillY, pillW - 6.0f * sc, pillH, colour::text,
                      visage::Font::kCenter, 9.2f, Weight::medium);
        }

        // Crisp vertical dividers between slot groups
        canvas.setColor(colour::withAlpha(colour::edge, 0.85f));
        for (const float dx : dividers_)
            canvas.rectangle(dx, 7.0f * sc, 1.0f * sc, height() - 14.0f * sc);
    }

  private:
    struct Item {
        Slot spec;
        std::unique_ptr<RowType> row;
    };

    ContextT& context_;
    unsigned badgeAccent_ = colour::textDim;
    std::string title_;
    std::function<std::string()> statusProvider_;
    std::string statusText_;
    std::vector<Item> slots_;
    std::vector<float> dividers_;
};

} // namespace hba::ui
