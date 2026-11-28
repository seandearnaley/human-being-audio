// SPDX-License-Identifier: AGPL-3.0-or-later
// The song strip: a tile per section and a knot per bridge, in ring order, with launch and fire buttons, the queued
// and playing states, "add a section", and the Land and Follow controls.
#pragma once

#include "control.h"

#include <chrono>
#include <string>
#include <vector>

namespace lg::ui {

class SongStrip final : public Panel {
  public:
    explicit SongStrip(Context& context);

    void draw(visage::Canvas& canvas) override;

  private:
    void sync() override;
    void layoutChildren() override;
    void live() override;

    // What the performance shows on the strip this frame.
    struct LiveState {
        bool playing = false;
        ItemRef item;
        int progress = 0; // 0..1000 of the playing item
        bool hold = false;
        int queuedTile = -1;
        std::string tileLabel;
        int queuedKnot = -1;
        std::string knotLabel;
        int pulse = 0; // animation phase for queued and held states
        bool operator==(const LiveState&) const = default;
    };
    LiveState liveState() const;
    float pulse(float period) const;

    void paintTile(const Paint& p, const Control& c, int section) const;
    void paintKnot(const Paint& p, const Control& c, int bridge) const;
    void paintRound(const Paint& p, const Control& c, bool fire) const;

    ControlPool tiles_;
    ControlPool knots_;
    ControlPool launches_;
    ControlPool fires_;
    Control add_;
    Segmented land_;
    Control follow_;

    // Geometry in design pixels (inside the strip).
    struct Slot {
        ItemRef item;
        float x = 0.0f;
        float w = 0.0f;
    };
    std::vector<Slot> slots_;
    float tileW_ = 212.0f;
    float knotW_ = 156.0f;
    float loopX_ = 0.0f;
    float landLabelX_ = 0.0f;
    float landLabelY_ = 45.0f;
    float knotMargin_ = 14.0f;
    bool compact_ = false; // three or four sections: Land and Follow stack, knots sit closer

    LiveState shown_;
    // Fire button: a tap fires, a press held past 230 ms holds the bridge until release.
    int pressedBridge_ = -1;
    bool held_ = false;
    bool wasHeld_ = false;
    std::chrono::steady_clock::time_point pressTime_{};
};

} // namespace lg::ui
