// SPDX-License-Identifier: AGPL-3.0-or-later
// Precision dark-obsidian & phase-coded acoustic palette for Datum.
#pragma once

#include "dt/ui/model.h"

#include <hba/ui/theme.h>

#include <algorithm>
#include <array>
#include <vector>

namespace dt::ui {

using hba::ui::Live;
using hba::ui::Weight;

struct Context {
    explicit Context(Model& m) : model(m) {}

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;

    float px(float designPixels) const { return designPixels * scale; }
};

namespace colour {
using hba::ui::colour::edge;
using hba::ui::colour::panel;
using hba::ui::colour::raised;
using hba::ui::colour::text;
using hba::ui::colour::textDim;
using hba::ui::colour::textFaint;
using hba::ui::colour::track;
using hba::ui::colour::window;
using hba::ui::colour::withAlpha;

inline constexpr unsigned sub = 0xff3ee5a0u;       // Phase-locked Sub emerald (CORE / Zero-X)
inline constexpr unsigned impact = 0xff38e8ffu;    // Electric cyan (IMPACT / Click / Amp curve)
inline constexpr unsigned body = 0xfff5a638u;      // Warm amber (SHAPE / Pitch curve / Drive)
inline constexpr unsigned sidechain = 0xffa884ffu; // Iris violet (MASTER / Sub-Link / Slot D)
inline constexpr unsigned alert = 0xffff5d5du;     // Peak / clip / GR
inline constexpr unsigned onColour = 0xff090c12u;  // Dark crisp ink on active pills

inline constexpr std::array<unsigned, kKitSlots> slotPalette{
    sub,       // A · Main
    impact,    // B · Ghost
    body,      // C · Double
    sidechain, // D · Sub
};

inline unsigned slotAccent(int slot) {
    return slotPalette[static_cast<std::size_t>(std::clamp(slot, 0, kKitSlots - 1))];
}
} // namespace colour

namespace metric {
using namespace hba::ui::metric;
}

namespace type {
using namespace hba::ui::type;
}

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    return hba::ui::fontPx(context.px(designSize), weight);
}

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    hba::ui::drawPanel(canvas, context.scale, x, y, width, height, fill, designRadius);
}

} // namespace dt::ui
