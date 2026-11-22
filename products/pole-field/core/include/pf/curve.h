// A drawn movement curve: breakpoints over one cycle, joined by shaped segments.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pf {

inline constexpr int kMaxCurvePoints = 32;

enum class Segment : std::uint8_t { line, smooth, hold };

struct CurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    float tension = 0.0f;
    Segment segment = Segment::line;

    friend bool operator==(const CurvePoint&, const CurvePoint&) = default;
};

struct Curve {
    std::array<CurvePoint, kMaxCurvePoints> points {};
    int count = 0;

    friend bool operator==(const Curve& a, const Curve& b) {
        return a.count == b.count && std::equal(a.points.begin(), a.points.begin() + a.count, b.points.begin());
    }
};

namespace detail {
inline float segmentShape(Segment segment, float tension, float t) noexcept {
    switch (segment) {
        case Segment::hold:   return 0.0f;
        case Segment::smooth: return 0.5f - 0.5f * std::cos(3.14159265f * t);
        case Segment::line:   break;
    }
    if (tension == 0.0f) return t;
    const float exponent = std::pow(8.0f, std::clamp(tension, -1.0f, 1.0f));
    return std::pow(t, exponent);
}
}

inline float evaluate(const Curve& curve, double phase) noexcept {
    if (curve.count <= 0) return 0.0f;
    if (curve.count == 1) return curve.points[0].y;

    const auto x = static_cast<float>(phase - std::floor(phase));
    const auto last = static_cast<std::size_t>(curve.count - 1);

    std::size_t from = last;
    for (std::size_t i = 0; i <= last; ++i) {
        if (curve.points[i].x > x) break;
        from = i;
    }
    const bool beforeFirst = x < curve.points[0].x;
    const std::size_t to = from == last ? 0 : from + 1;
    const CurvePoint& a = curve.points[from];
    const CurvePoint& b = curve.points[to];

    float start = a.x;
    float end = b.x;
    if (from == last) {
        if (beforeFirst) start -= 1.0f; else end += 1.0f;
    }
    const float span = end - start;
    const float t = span > 1.0e-6f ? std::clamp((x - start) / span, 0.0f, 1.0f) : 0.0f;
    return a.y + (b.y - a.y) * detail::segmentShape(a.segment, a.tension, t);
}

inline int addPoint(Curve& curve, CurvePoint point) noexcept {
    if (curve.count >= kMaxCurvePoints) return -1;
    point.x = std::clamp(point.x, 0.0f, 0.9999f);
    point.y = std::clamp(point.y, -1.0f, 1.0f);
    int index = 0;
    while (index < curve.count && curve.points[static_cast<std::size_t>(index)].x <= point.x) ++index;
    for (int i = curve.count; i > index; --i)
        curve.points[static_cast<std::size_t>(i)] = curve.points[static_cast<std::size_t>(i - 1)];
    curve.points[static_cast<std::size_t>(index)] = point;
    ++curve.count;
    return index;
}

inline void removePoint(Curve& curve, int index) noexcept {
    if (index < 0 || index >= curve.count || curve.count <= 1) return;
    for (int i = index; i < curve.count - 1; ++i)
        curve.points[static_cast<std::size_t>(i)] = curve.points[static_cast<std::size_t>(i + 1)];
    --curve.count;
}

inline CurvePoint movePoint(Curve& curve, int index, float x, float y) noexcept {
    if (index < 0 || index >= curve.count) return {};
    const auto i = static_cast<std::size_t>(index);
    const float low = index > 0 ? curve.points[i - 1].x + 1.0e-4f : 0.0f;
    const float high = index < curve.count - 1 ? curve.points[i + 1].x - 1.0e-4f : 0.9999f;
    curve.points[i].x = std::clamp(x, low, std::max(low, high));
    curve.points[i].y = std::clamp(y, -1.0f, 1.0f);
    return curve.points[i];
}

namespace curves {
inline Curve make(std::initializer_list<CurvePoint> points) noexcept {
    Curve curve;
    for (const auto& point : points) addPoint(curve, point);
    return curve;
}
inline Curve sine() noexcept {
    return make({ { 0.25f, 1.0f, 0.0f, Segment::smooth }, { 0.75f, -1.0f, 0.0f, Segment::smooth } });
}
inline Curve triangle() noexcept { return make({ { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 0.75f, -1.0f } }); }
inline Curve rampUp() noexcept { return make({ { 0.0f, -1.0f }, { 0.9999f, 1.0f } }); }
inline Curve rampDown() noexcept { return make({ { 0.0f, 1.0f }, { 0.9999f, -1.0f } }); }
inline Curve square() noexcept {
    return make({ { 0.0f, 1.0f, 0.0f, Segment::hold }, { 0.5f, -1.0f, 0.0f, Segment::hold } });
}
}

}
