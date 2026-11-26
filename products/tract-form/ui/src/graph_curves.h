// SPDX-License-Identifier: AGPL-3.0-or-later
// Analytical curves and signal-wire geometry shared by Tract Form's GraphPanel.
#pragma once

#include "tf/params.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace tf::ui {

template <std::size_t N> using GraphCurve = std::array<float, N>;

inline constexpr std::size_t kSparkPoints = 96;
inline constexpr std::size_t kShapePoints = 160;
inline constexpr float kSculptTop = 64.0f;
inline constexpr float kSculptRangeDb = 30.0f;
inline constexpr std::size_t kSculptPoints = 768;

float harmonicPosition(float harmonic);
GraphCurve<kShapePoints> subPeriod(float subWarmth);
GraphCurve<kSparkPoints> voicePeriod(float ratio, float phaseDegrees, float pmIndex, float oscShape);
GraphCurve<kShapePoints> formantResponse(int vowelA, int vowelB, float morph, float shiftSt, float qAmt);
GraphCurve<kShapePoints> foldTransfer(FoldType type, float drive, float asym, float extent);
GraphCurve<kSculptPoints> sculptResponse(float sculpt, int count, float spread, float depth, float resonance,
                                         SculptMode mode);

struct WirePoint {
    float x, y;
};

class SignalWire {
  public:
    SignalWire(float x, float y) { points_.push_back({x, y}); }

    SignalWire& to(float x, float y);
    SignalWire& curveTo(float x, float y);
    SignalWire& turn(float cx, float cy, float x, float y);

    const std::vector<WirePoint>& points() const { return points_; }
    float length() const;
    WirePoint at(float s) const;

  private:
    static float distance(WirePoint a, WirePoint b);
    SignalWire& cubic(WirePoint c1, WirePoint c2, WirePoint end, int steps);

    std::vector<WirePoint> points_;
};

} // namespace tf::ui
