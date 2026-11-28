// SPDX-License-Identifier: AGPL-3.0-or-later
// The top bar: wordmark, preset, history, A/B, transport, view toggles, MIDI activity, drag-out and the meter.
#pragma once

#include "control.h"

#include <visage/widgets.h>

#include <chrono>
#include <functional>
#include <string>

namespace lg::ui {

class TopBar final : public Panel {
  public:
    TopBar(Context& context, std::function<void()> showAbout);

    void draw(visage::Canvas& canvas) override;

  private:
    void sync() override;
    void layoutChildren() override;
    void live() override;
    void showDragMenu();

    struct Shown {
        bool playing = false;
        bool host = false;
        int bar = 1;
        int beat = 1;
        std::string item;
        std::string bpm;
        unsigned midi = 0;
        int meter = 0;
        int follow = -1;     // the part a follower plays, or -1 for the Lead instance
        bool leader = false; // a follower found its Lead instance
        std::string status;  // an export message, for a few seconds
        bool operator==(const Shown&) const = default;
    };
    Shown current() const;

    visage::Button wordmark_;
    Control previous_;
    Control name_;
    Control next_;
    Control undo_;
    Control redo_;
    Segmented ab_;
    Control play_;
    Control lens_;
    Control studio_;
    Control beat_;
    Control drag_;

    // Drawn parts, in design pixels.
    float badgeX_ = 0.0f;
    float badgeW_ = 0.0f;
    float posX_ = 0.0f;
    float midiX_ = 0.0f;
    float meterX_ = 0.0f;
    float rightStart_ = 0.0f; // where Lens begins: the position line ends before it
    Shown shown_;
    std::string status_;
    std::chrono::steady_clock::time_point statusUntil_{};
    std::string presetShown_;
};

} // namespace lg::ui
