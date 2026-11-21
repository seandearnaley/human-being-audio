// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <optional>

namespace livecut::ui
{

// Paces an animation from the display's refresh. Give it each refresh's time
// in seconds; it answers with how long to move the animation on by, or with
// nothing when no frame is due yet.
class FrameClock
{
public:
    // `interval` is the time between frames: at most one frame is due in each.
    std::optional<double> tick (double now, double interval)
    {
        // The first refresh only starts the clock.
        if (last < 0.0)
        {
            last = now;
            return {};
        }

        // Refreshes don't arrive exactly on time, so a frame is due once nearly all of its interval has passed.
        const auto elapsed = now - last;

        if (elapsed < interval - earliness)
            return {};

        last = now;
        return std::min (elapsed, longestStep); // after a pause, carry on rather than leap
    }

private:
    static constexpr double earliness = 0.002, longestStep = 0.1;
    double last = -1.0;
};

} // namespace livecut::ui
