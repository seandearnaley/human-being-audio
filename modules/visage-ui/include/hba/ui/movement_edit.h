// SPDX-License-Identifier: GPL-3.0-or-later
// Shared modulation-curve interactive editor geometry & gesture state machine.
#pragma once

#include <hba/dsp/curve.h>

namespace hba::ui::edit {

inline constexpr int kSnapDivisions = 16;

struct Pos {
    float x = 0.0f;
    float y = 0.0f;
};

struct Area {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct Reach {
    float handle = 8.0f;
    float line = 5.0f;
    float slop = 3.0f;
};

float toX(const Area& area, float cycle);
float toY(const Area& area, float value);
float cycleAt(const Area& area, float x);
float valueAt(const Area& area, float y);
Pos pointPos(const Area& area, const hba::dsp::CurvePoint& point);
float snap(float cycle, bool free);

int pickPoint(const hba::dsp::Curve& curve, const Area& area, Pos at, float radius);
int segmentAt(const hba::dsp::Curve& curve, float cycle);
int pickSegment(const hba::dsp::Curve& curve, const Area& area, Pos at, float tolerance);
float bend(float tensionAtPress, float fromY, float toY, float dragValue);

class Gesture {
  public:
    bool press(hba::dsp::Curve& curve, const Area& area, Pos at, int clicks, bool free, const Reach& reach);
    bool drag(hba::dsp::Curve& curve, const Area& area, Pos at, bool free);
    bool release(hba::dsp::Curve& curve, const Area& area, bool free);
    bool active() const { return mode_ != Mode::none; }
    int movingPoint() const { return mode_ == Mode::point ? index_ : -1; }

  private:
    enum class Mode { none, point, segment };
    Mode mode_ = Mode::none;
    int index_ = -1;
    Pos offset_{};
    Pos press_{};
    float tension_ = 0.0f;
    float slop_ = 3.0f;
    bool moved_ = false;
};

} // namespace hba::ui::edit
