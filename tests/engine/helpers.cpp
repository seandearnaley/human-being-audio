#include "helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tf::test {

Rendered render(const Setup& setup, const std::vector<NoteEvent>& events, long long count) {
    Engine engine;
    engine.prepare(setup.sampleRate, setup.block, setup.oversampling);
    return render(engine, setup, events, count);
}

Rendered render(Engine& engine, const Setup& setup, const std::vector<NoteEvent>& events, long long count,
                long long frameOffset) {
    Rendered out;
    out.latency = engine.latencySamples();
    const auto n = static_cast<std::size_t>(count);
    out.left.resize(n);
    out.right.resize(n);
    out.master.resize(n);
    out.growl.resize(n);
    std::vector<float> l(static_cast<std::size_t>(setup.block)), r(l.size()), m(l.size()), g(l.size());
    engine.pitchBend(setup.bend);
    engine.setLaneCurves(setup.curves);

    std::size_t next = 0;
    const double qps = setup.bpm / 60.0 / setup.sampleRate;
    Transport transport;
    transport.bpm = setup.bpm;
    transport.playing = setup.playing;
    for (long long pos = 0; pos < count;) {
        while (next < events.size() && events[next].frame <= pos) {
            const auto& e = events[next++];
            if (e.bendOnly) engine.pitchBend(e.bend);
            else if (e.on) engine.noteOn(e.note);
            else engine.noteOff(e.note);
        }
        long long end = std::min(count, pos + setup.block);
        if (next < events.size()) end = std::min(end, std::max(pos + 1, events[next].frame));
        const int len = static_cast<int>(end - pos);
        transport.ppq = setup.ppqStart + static_cast<double>(pos + frameOffset) * qps;
        engine.process(setup.params, transport, l.data(), r.data(), len, Taps { m.data(), g.data() });
        for (int i = 0; i < len; ++i) {
            const auto o = static_cast<std::size_t>(pos + i);
            const auto s = static_cast<std::size_t>(i);
            out.left[o] = l[s];
            out.right[o] = r[s];
            out.master[o] = m[s];
            out.growl[o] = g[s];
        }
        out.laneValues.push_back(engine.laneValues());
        out.hz.push_back(engine.currentHz());
        out.limiterGr.push_back(engine.gainReductionDb());
        out.growlClip.push_back(engine.growlClipDb());
        pos = end;
    }
    return out;
}

Params staticPatch() {
    Params p = Params::defaults();
    for (int i = 0; i < kLaneCount; ++i) p[laneParam(i, LaneField::on)] = 0.0f;
    return p;
}

Params hotPatch() {
    Params p = Params::defaults();
    p[ParamId::coupling] = 1.0f;
    p[ParamId::drive] = 1.0f;
    p[ParamId::warp] = 1.0f;
    p[ParamId::growlLevel] = 1.0f;
    p[ParamId::fbAmount] = 0.6f;
    p[ParamId::squash] = 0.6f;
    return p;
}

Params randomParams(std::mt19937& rng) {
    Params p;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        const auto& s = kParams[i];
        if (s.kind == ParamKind::continuous) {
            std::uniform_real_distribution<float> d(s.min, s.max);
            p.values[i] = d(rng);
        } else {
            std::uniform_int_distribution<int> d(static_cast<int>(s.min), static_cast<int>(s.max));
            p.values[i] = static_cast<float>(d(rng));
        }
    }
    return p;
}

Curve randomCurve(std::mt19937& rng) {
    Curve c;
    const int n = std::uniform_int_distribution<int>(1, 8)(rng);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f), bipolar(-1.0f, 1.0f);
    for (int i = 0; i < n; ++i) {
        CurvePoint point;
        point.x = unit(rng);
        point.y = bipolar(rng);
        point.tension = bipolar(rng);
        point.segment = static_cast<Segment>(std::uniform_int_distribution<int>(0, 2)(rng));
        addPoint(c, point);
    }
    return c;
}

std::uint64_t hashOutput(const Rendered& r) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto* sig : { &r.left, &r.right })
        for (double v : *sig) {
            const auto x = static_cast<float>(v);
            std::uint32_t bits;
            std::memcpy(&bits, &x, sizeof bits);
            for (int b = 0; b < 4; ++b) {
                h ^= (bits >> (8 * b)) & 0xffU;
                h *= 1099511628211ULL;
            }
        }
    return h;
}

double maxAbs(const Signal& x) {
    double m = 0.0;
    for (double v : x) m = std::max(m, std::abs(v));
    return m;
}

bool allFinite(const Signal& x) {
    return std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); });
}

double maxAbsDiff(const Signal& a, const Signal& b) {
    double m = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return a.size() == b.size() ? m : 1e300;
}

Signal slice(const Signal& x, std::size_t begin, std::size_t end) {
    end = std::min(end, x.size());
    begin = std::min(begin, end);
    return Signal(x.begin() + static_cast<long>(begin), x.begin() + static_cast<long>(end));
}

Signal mid(const Rendered& r) {
    Signal m(r.left.size());
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = 0.5 * (r.left[i] + r.right[i]);
    return m;
}

Signal side(const Rendered& r) {
    Signal s(r.left.size());
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = 0.5 * (r.left[i] - r.right[i]);
    return s;
}

double noteHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

long long frames(double seconds, double sampleRate) { return std::llround(seconds * sampleRate); }

}
