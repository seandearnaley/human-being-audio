// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/generate.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string_view>

namespace lg {

namespace {

// mulberry32: small, fast and identical on every platform.
struct Rng {
    std::uint32_t a;
    double operator()() noexcept {
        a += 0x6D2B79F5u;
        std::uint32_t t = (a ^ (a >> 15u)) * (1u | a);
        t = (t + ((t ^ (t >> 7u)) * (61u | t))) ^ t;
        return static_cast<double>(t ^ (t >> 14u)) / 4294967296.0;
    }
};

std::uint32_t fnv1a(std::string_view s) noexcept {
    std::uint32_t h = 2166136261u;
    for (const char ch : s) {
        h ^= static_cast<std::uint8_t>(ch);
        h *= 16777619u;
    }
    return h;
}

constexpr std::array<std::string_view, kRoleCount> kRoleKeys{"chords", "bass", "melody", "arp"};

struct Item {
    int bars = 4;
    bool bridge = false;
    Key key;
    Key toKey;
    RouteId route = RouteId::backdoor;
    std::vector<Chord> chords;
    int leadSection = 0; // the section whose closing voicing the first chord leads from
};

Item resolve(const Song& song, ItemRef ref, int barsOverride) {
    Item it;
    if (!ref.isBridge()) {
        const auto& s = song.sections[static_cast<std::size_t>(ref.index)];
        it.bars = s.bars;
        it.key = s.key;
        it.toKey = s.key;
        it.chords = s.chords;
        it.leadSection = ref.index;
        return it;
    }
    const auto& b = song.bridges[static_cast<std::size_t>(ref.index)];
    const auto& from = song.sections[static_cast<std::size_t>(b.from)];
    const auto& to = song.sections[static_cast<std::size_t>(b.to)];
    const Route r = routeOrFirst(from.key, to.key, b.route);
    it.bridge = true;
    it.bars = barsOverride > 0 ? barsOverride : b.bars;
    it.key = from.key;
    it.toKey = to.key;
    it.route = r.id;
    it.leadSection = b.from;
    const float scale = static_cast<float>(it.bars * 4) / 8.0f;
    for (auto c : r.chords) {
        c.beats *= scale;
        it.chords.push_back(c);
    }
    return it;
}

std::vector<Span> spansOf(const std::vector<Chord>& chords, int total) {
    std::vector<Span> out;
    float s = 0.0f;
    for (const auto& c : chords) {
        if (s >= static_cast<float>(total) - 1e-4f)
            break;
        Span sp;
        sp.chord = c;
        sp.start = s;
        sp.dur = std::min(c.beats * 4.0f, static_cast<float>(total) - s);
        out.push_back(sp);
        s += c.beats * 4.0f;
    }
    return out;
}

int spanIndexAt(const std::vector<Span>& spans, float step) noexcept {
    for (int i = static_cast<int>(spans.size()) - 1; i >= 0; --i)
        if (spans[static_cast<std::size_t>(i)].start <= step)
            return i;
    return 0;
}

bool hasInterval(const Chord& c, int interval) {
    const auto& q = qualityInfo(c.quality);
    for (std::size_t i = 0; i < q.intervalCount; ++i)
        if (q.intervals[i] == interval)
            return true;
    return false;
}

struct Ctx {
    const Item& it;
    const std::vector<Span>& spans;
    const PartSettings& ps;
    std::vector<Note>& notes;
    Rng& rr; // rhythm
    Rng& rp; // pitch
    int total() const noexcept { return it.bars * kStepsPerBar; }
    const Span& at(float step) const noexcept { return spans[static_cast<std::size_t>(spanIndexAt(spans, step))]; }
};

void genChords(Ctx& x) {
    struct Hit {
        float s, d, v;
        int span;
    };
    std::vector<Hit> hits;
    const auto& ps = x.ps;
    const int total = x.total();
    const int n = static_cast<int>(x.spans.size());
    switch (ps.style) {
    case 0: // Hold
        for (int i = 0; i < n; ++i)
            hits.push_back(
                {x.spans[static_cast<std::size_t>(i)].start, x.spans[static_cast<std::size_t>(i)].dur - 0.3f, 0.8f, i});
        break;
    case 3: // Push: each chord lands an 8th early
        for (int i = 0; i < n; ++i) {
            const auto& sp = x.spans[static_cast<std::size_t>(i)];
            const float s0 = i > 0 ? sp.start - 2.0f : sp.start;
            const bool rehit = sp.dur >= 16.0f && ps.density > 0.5f;
            // With a re-hit the first hit stops before it, so no pitch is struck while it still sounds.
            const float e = rehit ? sp.start + 10.0f : sp.start + sp.dur - (i < n - 1 ? 2.0f : 0.0f);
            hits.push_back({s0, e - s0 - 0.3f, 0.85f, i});
            if (rehit)
                hits.push_back({sp.start + 10.0f, 3.5f, 0.62f, i});
        }
        break;
    default: {
        struct P {
            float s, d, v;
        };
        static const std::vector<P> kStabs{{2, 1.2f, .85f}, {6, 1.2f, .7f}, {10, 1.2f, .85f}, {14, 1.2f, .7f}};
        static const std::vector<P> kTwoStep{
            {0, 2, .9f}, {3, 1, .62f}, {6, 2, .8f}, {10, 2.5f, .86f}, {13, 1.5f, .62f}};
        static const std::vector<P> kPulse{{0, 1.4f, .9f}, {2, 1.4f, .6f},  {4, 1.4f, .8f},  {6, 1.4f, .6f},
                                           {8, 1.4f, .9f}, {10, 1.4f, .6f}, {12, 1.4f, .8f}, {14, 1.4f, .6f}};
        const auto& pat = ps.style == 1 ? kStabs : ps.style == 2 ? kTwoStep : kPulse;
        const float glen = 16.0f / ps.rate;
        for (float g = 0.0f; g < static_cast<float>(total); g += glen)
            for (const auto& p : pat) {
                const float t = g + p.s / ps.rate;
                if (t >= static_cast<float>(total))
                    continue;
                if (p.v < 0.75f && x.rr() > static_cast<double>(ps.density) + 0.25)
                    continue;
                const int si = spanIndexAt(x.spans, t + 0.01f);
                const auto& sp = x.spans[static_cast<std::size_t>(si)];
                hits.push_back({t, std::min(p.d / ps.rate, sp.start + sp.dur - t - 0.05f), p.v, si});
            }
    }
    }
    for (const auto& h : hits) {
        const auto& sp = x.spans[static_cast<std::size_t>(h.span)];
        // Nudge varies the voicing per hit: sometimes the top voice doubles an octave up, sometimes the lowest rests.
        const bool lift = x.rp() < 0.2;
        const bool thin = x.rp() < 0.18 && sp.voicing.size() > 3;
        for (std::size_t k = 0; k < sp.voicing.size(); ++k) {
            if (thin && k == 0)
                continue;
            x.notes.push_back({h.s, std::max(0.3f, h.d), sp.voicing[k] + 12 * ps.reg,
                               h.v * static_cast<float>(0.92 + x.rr() * 0.08), Role::chords, 0, h.span});
        }
        if (lift)
            x.notes.push_back(
                {h.s, std::max(0.3f, h.d), sp.voicing.back() + 12 + 12 * ps.reg, h.v * 0.55f, Role::chords, 0, h.span});
    }
}

void genBass(Ctx& x) {
    const auto& ps = x.ps;
    const int total = x.total();
    const int n = static_cast<int>(x.spans.size());
    const auto hit = [&](float s, float d, char kind, float v) {
        const int i = spanIndexAt(x.spans, s + 0.01f);
        const auto& sp = x.spans[static_cast<std::size_t>(i)];
        const auto& nx = x.spans[static_cast<std::size_t>((i + 1) % n)];
        const int root = sp.bass + 12 * ps.reg;
        int p = root;
        if (kind == 'o') {
            p = root + 12;
        } else if (kind == 'f') {
            p = root + (hasInterval(sp.chord, 6) && !hasInterval(sp.chord, 7) ? 6 : 7);
        } else if (kind == 'a') {
            if (sp.start + sp.dur - s <= 2.5f && &nx != &sp) {
                const int nb = nx.bass + 12 * ps.reg;
                p = mod12(nb - root) == 0 ? root + 7
                                          : (std::abs(nb + 1 - root) < std::abs(nb - 1 - root) ? nb + 1 : nb - 1);
            } else {
                p = root + 7;
            }
        }
        if (kind != 'r' && x.rp() < 0.2)
            p = root + (kind == 'o' ? 7 : 12);
        x.notes.push_back({s, std::max(0.4f, std::min(d, sp.start + sp.dur - s - 0.05f)), p, v, Role::bass, 0, i});
    };
    if (ps.style == 0) { // Anchor: Twist moves the re-hit, Nudge picks its note
        for (const auto& sp : x.spans) {
            hit(sp.start, sp.dur - 0.5f, 'r', 0.95f);
            static constexpr std::array<float, 4> kAt{10, 12, 14, 6};
            const float at = kAt[static_cast<std::size_t>(std::floor(x.rr() * 4.0))];
            if (sp.dur >= 16.0f && ps.density > 0.55f)
                hit(sp.start + at, 2.0f, x.rp() < 0.5 ? 'o' : 'f', 0.7f);
        }
        return;
    }
    struct P {
        float s, d;
        char k;
        float v;
    };
    static const std::vector<P> kGarage{
        {0, 2.5f, 'r', .95f}, {3, 1, 'o', .62f}, {6, 2, 'r', .82f}, {10, 2.5f, 'r', .86f}, {14, 1.5f, 'a', .7f}};
    static const std::vector<P> kOffbeat{
        {2, 1.6f, 'r', .9f}, {6, 1.6f, 'o', .75f}, {10, 1.6f, 'r', .9f}, {14, 1.6f, 'o', .75f}};
    static const std::vector<P> kRolling{{0, 1.5f, 'r', .95f}, {2, 1, 'o', .62f},    {3, 1, 'r', .7f},
                                         {6, 1.5f, 'f', .82f}, {8, 1, 'r', .86f},    {10, 1, 'o', .62f},
                                         {11, 1, 'r', .7f},    {14, 1.5f, 'a', .75f}};
    const auto& pat = ps.style == 1 ? kGarage : ps.style == 2 ? kOffbeat : kRolling;
    const float glen = 16.0f / ps.rate;
    for (float g = 0.0f; g < static_cast<float>(total); g += glen)
        for (const auto& p : pat) {
            const float t = g + p.s / ps.rate;
            if (t >= static_cast<float>(total))
                continue;
            if (p.v < 0.8f && x.rr() > static_cast<double>(ps.density) + 0.3)
                continue;
            hit(t, p.d / ps.rate, p.k, p.v);
        }
}

using Cell = std::vector<std::array<float, 2>>;
const std::array<Cell, 6>& melodyCells() {
    static const std::array<Cell, 6> kCells{{
        {{0, 3}, {3, 3}, {6, 2}, {8, 4}, {12, 2}},
        {{0, 2}, {2, 2}, {4, 4}, {10, 2}, {12, 4}},
        {{2, 2}, {4, 2}, {6, 4}, {11, 1}, {12, 4}},
        {{0, 6}, {6, 2}, {8, 2}, {10, 6}},
        {{0, 2}, {3, 1}, {4, 2}, {7, 3}, {10, 2}, {12, 3}},
        {{1, 2}, {3, 3}, {6, 2}, {8, 1}, {9, 3}, {12, 4}},
    }};
    return kCells;
}

std::vector<int> scalePcs(Key k) {
    std::vector<int> out;
    for (int iv : scaleIntervals(k.mode))
        out.push_back(mod12(k.tonic + iv));
    return out;
}

bool contains(const std::vector<int>& v, int pc) { return std::find(v.begin(), v.end(), mod12(pc)) != v.end(); }

// The key's scale bent to the chord: its chord tones, plus the scale notes that sit at least a whole tone from every
// chord tone (the safe tensions: 9ths, 11ths over minor chords, 13ths). Avoid notes such as the b6 over a minor chord
// or the 4th over a major one are left out, and a chord tone outside the key (a borrowed chord, the harmonic-minor V)
// is taken in.
std::vector<int> chordScale(const std::vector<int>& scale, const std::vector<int>& chordTones) {
    std::vector<int> out;
    for (int c : chordTones)
        if (!contains(out, c))
            out.push_back(mod12(c));
    for (int pc : scale) {
        if (contains(out, pc))
            continue;
        bool rubs = false;
        for (int c : chordTones)
            rubs = rubs || mod12(pc - c) == 1 || mod12(c - pc) == 1;
        if (!rubs)
            out.push_back(mod12(pc));
    }
    return out;
}

int stepScale(int p, int n, const std::vector<int>& scale) {
    int q = p;
    const int dir = n > 0 ? 1 : -1;
    for (int k = 0; k < std::abs(n); ++k) {
        do {
            q += dir;
        } while (!contains(scale, q));
    }
    return q;
}

int nearestPc(int p, const std::vector<int>& pcs) {
    for (int d = 0; d < 7; ++d) {
        if (contains(pcs, p + d))
            return p + d;
        if (contains(pcs, p - d))
            return p - d;
    }
    return p;
}

void genMelody(Ctx& x) {
    const auto& ps = x.ps;
    const int total = x.total(), bars = x.it.bars;
    const auto& cells = melodyCells();
    const int ci = static_cast<int>(std::floor(x.rr() * 6.0));
    const int ci2 = (ci + 1 + static_cast<int>(std::floor(x.rr() * 5.0))) % 6;
    const int lo = 69 + 12 * ps.reg, hi = 88 + 12 * ps.reg;
    std::vector<int> contour;
    for (std::size_t i = 0; i < cells[static_cast<std::size_t>(ci)].size(); ++i)
        contour.push_back(static_cast<int>(std::round((x.rp() - 0.45) * 4.0)));
    int cur = lo + 7;
    const float dens = x.it.bridge ? ps.density * 0.75f : ps.density;
    const auto fromScale = scalePcs(x.it.key), toScale = scalePcs(x.it.toKey);
    for (int b = 0; b < bars; ++b) {
        char form = 'A';
        if (bars == 1)
            form = 'C';
        else if (bars == 2)
            form = b == 0 ? 'A' : 'C';
        else
            form = "AXBC"[b % 4]; // X: the first bar again, nudged (A')
        Cell cell = form == 'B' ? cells[static_cast<std::size_t>(ci2)] : cells[static_cast<std::size_t>(ci)];
        if (ps.style == 1) // Hook: a shorter idea
            cell.resize(
                std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(static_cast<double>(cell.size()) * 0.6))));
        if (ps.style == 2) { // Answer: call in the first half, response from beat 2
            Cell c;
            const auto& src = b % 2 == 0 ? cells[static_cast<std::size_t>(ci)] : cells[static_cast<std::size_t>(ci2)];
            for (const auto& e : src)
                if (b % 2 == 0 ? e[0] < 8 : e[0] >= 4)
                    c.push_back(e);
            cell = c;
        }
        if (form == 'C') { // cadence: the opening, then a long note on beat 3
            Cell c;
            for (const auto& e : cell)
                if (e[0] < 8 && e[0] + e[1] <= 8)
                    c.push_back(e);
            c.push_back({8, 7});
            cell = c;
        }
        for (std::size_t j = 0; j < cell.size(); ++j) {
            const float s = cell[j][0], d = cell[j][1];
            if (form != 'C' && dens < 0.5f && j % 2 == 1)
                continue;
            if (dens < 0.3f && j > 1)
                continue;
            const float t = static_cast<float>(b * 16) + s;
            if (t >= static_cast<float>(total))
                continue;
            const int si = spanIndexAt(x.spans, t + 0.01f);
            const auto& sp = x.spans[static_cast<std::size_t>(si)];
            const auto ct = chordPcs(sp.chord);
            const auto scale =
                chordScale(x.it.bridge && t >= static_cast<float>(total) / 2.0f ? toScale : fromScale, ct);
            int dir = contour[j % contour.size()] * (form == 'B' ? -1 : 1);
            if (form == 'X' && j == 0)
                dir += x.rp() < 0.5 ? 1 : -1;
            if (dir == 0)
                dir = x.rp() < 0.5 ? 1 : -1;
            int p = stepScale(cur, dir, scale);
            const bool strong = static_cast<int>(s) % 8 == 0 || d >= 4.0f || (form == 'C' && s == 8.0f);
            if (strong || !contains(scale, p))
                p = nearestPc(p, ct);
            if (!contains(scale, p) && !contains(ct, p))
                p = nearestPc(p, ct);
            while (p < lo)
                p += 12;
            while (p > hi)
                p -= 12;
            cur = p;
            x.notes.push_back({t, std::max(0.5f, std::min(d - 0.2f, static_cast<float>(total) - t)), p,
                               strong ? 0.9f : 0.72f, Role::melody, 0, si});
        }
    }
}

void genArp(Ctx& x) {
    const auto& ps = x.ps;
    const int total = x.total();
    if (ps.style == 2) { // Counter: guide tones, two per chord
        int prev = -1;
        for (int i = 0; i < static_cast<int>(x.spans.size()); ++i) {
            const auto& sp = x.spans[static_cast<std::size_t>(i)];
            const auto& c = sp.chord;
            const int third = mod12(c.root + (hasInterval(c, 3) ? 3 : hasInterval(c, 4) ? 4 : 5));
            int sevI = -1;
            for (int iv : {10, 11, 9})
                if (sevI < 0 && hasInterval(c, iv))
                    sevI = iv;
            const std::array<int, 2> g{third, sevI >= 0 ? mod12(c.root + sevI) : mod12(c.root + 7)};
            const auto pick = [&](int pc, int ref) {
                int best = -1;
                for (int m = 62 + 12 * ps.reg; m <= 77 + 12 * ps.reg; ++m)
                    if (mod12(m) == pc && (best < 0 || std::abs(m - ref) < std::abs(best - ref)))
                        best = m;
                return best;
            };
            const int ref = prev >= 0 ? prev : 69 + 12 * ps.reg;
            const int a = pick(x.rp() < 0.5 ? g[0] : g[1], ref);
            const int b2 = pick(mod12(a) == g[0] ? g[1] : g[0], a);
            // Twist moves where the second guide tone enters.
            static constexpr std::array<float, 4> kSplit{0.5f, 0.75f, 0.375f, 0.625f};
            const float at =
                std::max(1.0f, std::round(sp.dur * kSplit[static_cast<std::size_t>(std::floor(x.rr() * 4.0))]));
            x.notes.push_back(
                {sp.start, std::max(0.5f, (ps.density > 0.35f ? at : sp.dur) - 0.3f), a, 0.72f, Role::arp, 0, i});
            if (ps.density > 0.35f && at < sp.dur) {
                x.notes.push_back({sp.start + at, std::max(0.5f, sp.dur - at - 0.3f), b2, 0.6f, Role::arp, 0, i});
                prev = b2;
            } else {
                prev = a;
            }
        }
        return;
    }
    const bool up = ps.style == 0;
    const int shape = static_cast<int>(std::floor(x.rp() * 3.0)); // Nudge picks the shape: up, up-down or down
    const auto order = [shape](int n) {
        std::vector<int> o(static_cast<std::size_t>(n));
        std::iota(o.begin(), o.end(), 0);
        if (shape == 2)
            std::reverse(o.begin(), o.end());
        else if (shape == 1)
            for (int k = n - 2; k >= 1; --k)
                o.push_back(k);
        return o;
    };
    int idx = 0, last = -1;
    for (float t = 0.0f; t < static_cast<float>(total) - 1e-6f;) {
        const bool lastBar = x.it.bridge && t >= static_cast<float>(total - 16);
        const float r = (up ? ps.rate : ps.rate * 0.5f) * (up && lastBar ? 2.0f : 1.0f);
        const float len = 1.0f / r;
        const int si = spanIndexAt(x.spans, t + 0.01f);
        if (si != last) {
            idx = 0;
            last = si;
        }
        std::vector<int> pool;
        for (int p : x.spans[static_cast<std::size_t>(si)].voicing)
            pool.push_back(p + 12 + 12 * ps.reg);
        std::sort(pool.begin(), pool.end());
        const int n = static_cast<int>(pool.size());
        int p;
        float v;
        if (up) {
            const auto ord = order(n);
            p = pool[static_cast<std::size_t>(ord[static_cast<std::size_t>(idx) % ord.size()])];
            v = idx % static_cast<int>(ord.size()) == 0 ? 0.8f : 0.58f;
        } else {
            const std::array<int, 4> k{n - 1, n - 2, 1, n - 2};
            p = pool[static_cast<std::size_t>(std::max(0, k[static_cast<std::size_t>(idx % 4)]))];
            v = (idx % 2) != 0 ? 0.78f : 0.6f;
        }
        ++idx;
        if (!(x.rr() < (1.0 - static_cast<double>(ps.density)) * 0.5))
            x.notes.push_back({t, len * 0.7f, p, v, Role::arp, 0, si});
        t += len;
    }
}

void applyStutter(std::vector<Note>& notes, const StutterCells& cells) {
    if (cells.empty())
        return;
    std::vector<Note> out;
    out.reserve(notes.size() * 2);
    for (const auto& n : notes) {
        std::vector<Note> segs{n};
        for (const auto& [cellStep, type] : cells) {
            if (type == Stutter::none || (n.role == Role::bass && type != Stutter::gate))
                continue;
            const float c = static_cast<float>(cellStep);
            std::vector<Note> next;
            for (const auto& m : segs) {
                const float ms = m.start, me = m.start + m.dur;
                if (me <= c || ms >= c + 1.0f) {
                    next.push_back(m);
                    continue;
                }
                if (ms < c) {
                    auto head = m;
                    head.dur = c - ms;
                    next.push_back(head);
                }
                // Repeats only where the note itself sounds: a note that starts or ends inside the cell repeats from
                // its start or until its end, so two notes that share the cell never strike the same repeat twice.
                const int k = type == Stutter::r32 ? 2 : type == Stutter::r64 ? 4 : 0;
                bool any = false;
                for (int j = 0; j < k; ++j) {
                    const float rs = c + static_cast<float>(j) / static_cast<float>(k);
                    if (rs < ms - 1e-4f || rs >= me - 1e-4f)
                        continue;
                    auto r = m;
                    r.start = rs;
                    r.dur = std::min(0.8f / static_cast<float>(k), me - rs);
                    r.velocity = m.velocity * (0.62f + 0.38f * static_cast<float>(j) / static_cast<float>(k));
                    r.flags = static_cast<std::uint8_t>(r.flags | kNoteStutter);
                    next.push_back(r);
                    any = true;
                }
                if (k > 0 && !any) { // too short to catch a repeat: it keeps its own slice of the cell
                    auto r = m;
                    r.start = std::max(ms, c);
                    r.dur = std::min(me, c + 1.0f) - r.start;
                    r.flags = static_cast<std::uint8_t>(r.flags | kNoteStutter);
                    next.push_back(r);
                }
                if (me > c + 1.0f) {
                    auto tail = m;
                    tail.start = c + 1.0f;
                    tail.dur = me - c - 1.0f;
                    tail.velocity = m.velocity * 0.8f;
                    next.push_back(tail);
                }
            }
            segs = std::move(next);
        }
        for (auto& s : segs)
            if (s.dur > 0.05f)
                out.push_back(s);
    }
    notes = std::move(out);
}

StutterCells routeCells(const Item& it) {
    StutterCells cells;
    if (!it.bridge || it.route != RouteId::slam)
        return cells;
    const int L = it.bars * kStepsPerBar;
    const std::array<std::pair<Stutter, int>, 8> kTail{{{Stutter::r32, 8},
                                                        {Stutter::r32, 7},
                                                        {Stutter::r64, 6},
                                                        {Stutter::r64, 5},
                                                        {Stutter::gate, 4},
                                                        {Stutter::gate, 3},
                                                        {Stutter::gate, 2},
                                                        {Stutter::gate, 1}}};
    for (const auto& [type, back] : kTail)
        cells[L - back] = type;
    return cells;
}

} // namespace

int Pattern::spanAt(float step) const noexcept { return spanIndexAt(spans, step); }

std::vector<Chord> itemChords(const Song& song, ItemRef item, int barsOverride) {
    return resolve(song, item, barsOverride).chords;
}

std::vector<int> closingLead(const Song& song, int section) {
    const auto& s = song.sections[static_cast<std::size_t>(section)];
    std::vector<int> lead;
    for (const auto& sp : spansOf(s.chords, s.bars * kStepsPerBar))
        lead = voiceChord(sp.chord, lead.empty() ? nullptr : &lead, song.voiceLead).lead;
    return lead;
}

Pattern buildPattern(const Song& song, ItemRef item, int barsOverride,
                     const std::array<PartSettings, kRoleCount>* partsOverride) {
    const Item it = resolve(song, item, barsOverride);
    const bool full = barsOverride <= 0 || !item.isBridge() ||
                      barsOverride == song.bridges[static_cast<std::size_t>(item.index)].bars;
    Pattern pat;
    pat.item = item;
    pat.bars = it.bars;
    pat.bridge = it.bridge;
    pat.key = it.key;
    pat.toKey = it.toKey;
    pat.route = it.route;
    const int total = it.bars * kStepsPerBar;
    pat.spans = spansOf(it.chords, total);

    std::vector<int> lead = closingLead(song, it.leadSection);
    for (auto& sp : pat.spans) {
        const auto v = voiceChord(sp.chord, lead.empty() ? nullptr : &lead, song.voiceLead);
        sp.voicing = v.notes;
        lead = v.lead;
        sp.bass = bassPitch(sp.chord.bass >= 0 ? sp.chord.bass : sp.chord.root);
        sp.analysis = analyze(sp.chord, it.key);
        if (it.bridge) {
            sp.toAnalysis = analyze(sp.chord, it.toKey);
            sp.pivot = isPivot(sp.chord, it.key, it.toKey);
        } else {
            sp.toAnalysis = sp.analysis;
        }
        sp.tension = tension(sp.chord, sp.analysis);
    }

    const auto& parts = partsOverride != nullptr ? *partsOverride : song.parts;
    const std::string key = itemKey(song, item);
    for (const Role role : {Role::melody, Role::chords, Role::arp, Role::bass}) {
        const auto& ps = parts[static_cast<std::size_t>(role)];
        if (styleIsOff(role, ps.style))
            continue;
        const std::uint32_t h = fnv1a(key + std::string(kRoleKeys[static_cast<std::size_t>(role)]));
        Rng rr{h ^ (ps.seedR * 7919u)};
        Rng rp{(h * 3u) ^ (ps.seedP * 104729u)};
        Ctx x{it, pat.spans, ps, pat.notes, rr, rp};
        switch (role) {
        case Role::chords:
            genChords(x);
            break;
        case Role::bass:
            genBass(x);
            break;
        case Role::melody:
            genMelody(x);
            break;
        case Role::arp:
            genArp(x);
            break;
        default:
            break;
        }
    }

    pat.cells = routeCells(it);
    if (full)
        for (const auto& [step, cell] : itemStutter(song, item))
            pat.cells[step] = cell;
    applyStutter(pat.notes, pat.cells);

    if (full && partsOverride == nullptr) {
        for (const auto& pin : song.pins) {
            if (pin.item != item)
                continue;
            std::erase_if(pat.notes, [&](const Note& n) {
                return n.role == pin.role && n.start < pin.start + pin.dur && n.start + n.dur > pin.start;
            });
            pat.notes.push_back({pin.start, pin.dur, pin.pitch, 0.9f, pin.role, kNotePinned,
                                 spanIndexAt(pat.spans, pin.start + 0.01f)});
        }
    }
    std::erase_if(pat.notes, [](const Note& n) { return n.pitch < 0 || n.pitch > 127 || !(n.dur > 0.0f); });
    std::stable_sort(pat.notes.begin(), pat.notes.end(),
                     [](const Note& a, const Note& b) { return a.start < b.start; });
    return pat;
}

} // namespace lg
