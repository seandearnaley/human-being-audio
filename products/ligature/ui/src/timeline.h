// SPDX-License-Identifier: AGPL-3.0-or-later
// The timeline of the focused item: ruler, harmony lane (chords, numerals, tension), the four part rolls in pitch
// order, the stutter lane, the lane headers and the playhead.
#pragma once

#include "control.h"

#include <array>
#include <memory>

namespace lg::ui {

// Pixel geometry shared by the timeline and the inspector's thumbnails.
struct RollRange {
    int lo = 60;
    int hi = 72;
    float rowH = 7.0f;
    float top = 0.0f;
    float y(int pitch) const { return top + static_cast<float>(hi - pitch) * rowH; }
};
RollRange rollRange(const Pattern& pattern, Role role, float height);

class Timeline final : public Panel {
  public:
    explicit Timeline(Context& context);
    ~Timeline() override;

    void draw(visage::Canvas& canvas) override;

    class HarmonyView;
    class LaneView;
    class StutterView;
    class Playhead;

  private:
    void sync() override;
    void layoutChildren() override;
    void live() override;

    std::unique_ptr<HarmonyView> harmony_;
    std::array<std::unique_ptr<LaneView>, 4> lanes_;
    std::unique_ptr<StutterView> stutter_;
    std::unique_ptr<Playhead> playhead_;

    struct Header {
        std::unique_ptr<Control> select;
        std::unique_ptr<Control> mutate;
        std::unique_ptr<Control> mute;
        std::unique_ptr<Control> solo;
    };
    std::array<Header, 4> headers_;
    int playheadShown_ = -1;
};

} // namespace lg::ui
