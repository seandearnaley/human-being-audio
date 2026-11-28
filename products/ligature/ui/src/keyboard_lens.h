// SPDX-License-Identifier: AGPL-3.0-or-later
// The bottom row: a five-octave keyboard lighting what sounds, by part, and the theory lens card.
#pragma once

#include "control.h"

#include <array>
#include <cstdint>

namespace lg::ui {

class KeyboardPanel final : public Panel {
  public:
    explicit KeyboardPanel(Context& context) : Panel(context) {}
    void draw(visage::Canvas& canvas) override;

  private:
    void live() override;
    std::array<std::array<std::uint64_t, 2>, kRoleCount> shown_{};
};

class LensCard final : public Panel {
  public:
    explicit LensCard(Context& context) : Panel(context) {}
    void draw(visage::Canvas& canvas) override;

  private:
    void sync() override;
    void live() override;

    struct Shown {
        bool playing = false;
        ItemRef item;
        int span = -1;
        bool operator==(const Shown&) const = default;
    };
    Shown current() const;
    Shown shown_;
};

} // namespace lg::ui
