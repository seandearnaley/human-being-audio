// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/theory.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace lg {

namespace {

using Q = Quality;
using F = Family;

constexpr std::array<QualityInfo, static_cast<std::size_t>(Q::count)> kQualities{{
    {"maj", "", "", F::maj, {0, 4, 7}, 3, {0, 4, 7}, 3},
    {"maj7", "maj7", "maj7", F::maj, {0, 4, 7, 11}, 4, {0, 4, 7, 11}, 4},
    {"maj9", "maj9", "maj9", F::maj, {0, 4, 7, 11, 14}, 5, {4, 7, 11, 14}, 4},
    {"maj9s11", "maj9#11", "maj9#11", F::maj, {0, 4, 7, 11, 14, 18}, 6, {4, 11, 14, 18}, 4},
    {"add9", "add9", "add9", F::maj, {0, 4, 7, 14}, 4, {0, 4, 7, 14}, 4},
    {"six9", "6/9", "6/9", F::maj, {0, 4, 7, 9, 14}, 5, {4, 7, 9, 14}, 4},
    {"min", "m", "", F::min, {0, 3, 7}, 3, {0, 3, 7}, 3},
    {"m7", "m7", "7", F::min, {0, 3, 7, 10}, 4, {0, 3, 7, 10}, 4},
    {"m9", "m9", "9", F::min, {0, 3, 7, 10, 14}, 5, {3, 7, 10, 14}, 4},
    {"m11", "m11", "11", F::min, {0, 3, 7, 10, 14, 17}, 6, {3, 10, 14, 17}, 4},
    {"m6", "m6", "6", F::min, {0, 3, 7, 9}, 4, {0, 3, 7, 9}, 4},
    {"d7", "7", "7", F::dom, {0, 4, 7, 10}, 4, {0, 4, 7, 10}, 4},
    {"d9", "9", "9", F::dom, {0, 4, 7, 10, 14}, 5, {4, 7, 10, 14}, 4},
    {"d13", "13", "13", F::dom, {0, 4, 7, 10, 14, 21}, 6, {4, 10, 14, 21}, 4},
    {"d7b9", "7b9", "7b9", F::dom, {0, 4, 7, 10, 13}, 5, {4, 7, 10, 13}, 4},
    {"sus4", "7sus4", "sus", F::sus, {0, 5, 7, 10}, 4, {0, 5, 7, 10}, 4},
    {"hdim", "m7b5", "7", F::hdim, {0, 3, 6, 10}, 4, {0, 3, 6, 10}, 4},
    {"dim7",
     "\xc2\xb0"
     "7",
     "7",
     F::dim,
     {0, 3, 6, 9},
     4,
     {0, 3, 6, 9},
     4},
    {"aug", "+", "", F::aug, {0, 4, 8}, 3, {0, 4, 8}, 3},
    {"q4", " 4ths", "q", F::sus, {0, 5, 10, 15}, 4, {0, 5, 10, 15}, 4},
}};

constexpr std::array<std::string_view, 12> kNames{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
constexpr std::array<std::string_view, 7> kRoman{"I", "II", "III", "IV", "V", "VI", "VII"};
constexpr std::array<int, 7> kMajor{0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> kMinor{0, 2, 3, 5, 7, 8, 10};

constexpr std::array<Q, 4> kMajLadder{Q::maj, Q::maj7, Q::maj9, Q::maj9s11};
constexpr std::array<Q, 4> kMinLadder{Q::min, Q::m7, Q::m9, Q::m11};
constexpr std::array<Q, 4> kDomLadder{Q::maj, Q::d7, Q::d9, Q::d13};

const std::array<Q, 4>* ladderOf(F f) noexcept {
    switch (f) {
    case F::maj:
        return &kMajLadder;
    case F::min:
        return &kMinLadder;
    case F::dom:
        return &kDomLadder;
    default:
        return nullptr;
    }
}

int indexOf(const std::array<int, 7>& scale, int interval) noexcept {
    for (int i = 0; i < 7; ++i)
        if (scale[static_cast<std::size_t>(i)] == interval)
            return i;
    return -1;
}

std::string lowerRoman(std::string s) {
    for (auto& ch : s)
        if (ch == 'I' || ch == 'V')
            ch = static_cast<char>(ch == 'I' ? 'i' : 'v');
    return s;
}

} // namespace

const QualityInfo& qualityInfo(Quality q) noexcept { return kQualities[static_cast<std::size_t>(q)]; }

std::optional<Quality> qualityFromId(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kQualities.size(); ++i)
        if (kQualities[i].id == id)
            return static_cast<Quality>(i);
    return std::nullopt;
}

Chord makeChord(int root, Quality q, float beats, int bass) {
    Chord c;
    c.root = mod12(root);
    c.quality = q;
    c.beats = beats;
    c.bass = bass < 0 ? -1 : mod12(bass);
    c.family = qualityInfo(q).family;
    return c;
}

std::string pcName(int pc) { return std::string(kNames[static_cast<std::size_t>(mod12(pc))]); }

std::string chordName(const Chord& c) {
    std::string out = pcName(c.root);
    out += qualityInfo(c.quality).suffix;
    if (c.bass >= 0 && c.bass != c.root)
        out += "/" + pcName(c.bass);
    return out;
}

std::string keyName(Key k) { return pcName(k.tonic) + (k.mode == Mode::major ? " major" : " minor"); }
std::string keyShort(Key k) { return pcName(k.tonic) + (k.mode == Mode::major ? "" : "m"); }

float keyHue(Key k) {
    const int signature = k.mode == Mode::major ? k.tonic : k.tonic + 3;
    return static_cast<float>((85 + mod12(signature * 7) * 30) % 360);
}

std::uint16_t pcMask(const Chord& c) {
    const auto& q = qualityInfo(c.quality);
    std::uint16_t mask = 0;
    for (std::size_t i = 0; i < q.intervalCount; ++i)
        mask = static_cast<std::uint16_t>(mask | (1u << mod12(c.root + q.intervals[i])));
    return mask;
}

std::vector<int> chordPcs(const Chord& c) {
    const auto& q = qualityInfo(c.quality);
    std::vector<int> out;
    for (std::size_t i = 0; i < q.intervalCount; ++i)
        out.push_back(mod12(c.root + q.intervals[i]));
    return out;
}

std::vector<int> sharedPcs(const Chord& a, const Chord& b) {
    std::vector<int> out;
    const auto mb = pcMask(b);
    for (int pc : chordPcs(a))
        if ((mb & (1u << pc)) != 0u && std::find(out.begin(), out.end(), pc) == out.end())
            out.push_back(pc);
    return out;
}

const std::array<int, 7>& scaleIntervals(Mode m) noexcept { return m == Mode::major ? kMajor : kMinor; }

bool inScale(Key k, int pc) noexcept { return indexOf(scaleIntervals(k.mode), mod12(pc - k.tonic)) >= 0; }

Key parallel(Key k) noexcept { return {k.tonic, k.mode == Mode::major ? Mode::minor : Mode::major}; }

bool hasLadder(Family f) noexcept { return ladderOf(f) != nullptr; }

Quality ladder(Family f, int level) noexcept {
    const auto* l = ladderOf(f);
    if (l == nullptr)
        return Q::maj;
    return (*l)[static_cast<std::size_t>(std::clamp(level, 0, 3))];
}

Chord diatonic(Key k, int degree, int level, float beats) {
    const auto& s = scaleIntervals(k.mode);
    const auto at = [&s](int i) { return s[static_cast<std::size_t>(i % 7)] + 12 * (i / 7); };
    const int r = at(degree);
    const int third = at(degree + 2) - r, fifth = at(degree + 4) - r, seventh = at(degree + 6) - r,
              ninth = at(degree + 8) - r;
    int lvl = std::clamp(level, 0, 3);
    if (lvl >= 2 && ninth == 13)
        lvl = 1; // the scale's ninth is flat here: stop at the seventh
    Quality q;
    Family fam;
    if (third == 4 && fifth == 7) {
        fam = seventh == 11 ? F::maj : F::dom;
        q = ladder(fam, lvl);
    } else if (third == 3 && fifth == 7) {
        fam = F::min;
        q = ladder(fam, lvl);
    } else if (third == 3 && fifth == 6) {
        fam = F::hdim;
        q = Q::hdim;
    } else {
        fam = F::aug;
        q = Q::aug;
    }
    if (q == Q::maj9s11 && !(k.mode == Mode::major && degree == 3))
        q = Q::maj9;
    if (q == Q::m11 && degree != 0 && degree != 1 && degree != 3)
        q = Q::m9;
    Chord c = makeChord(k.tonic + s[static_cast<std::size_t>(degree)], q, beats);
    c.family = fam;
    return c;
}

int levelOf(const Chord& c) {
    if (const auto* l = ladderOf(c.family)) {
        for (int i = 0; i < 4; ++i)
            if ((*l)[static_cast<std::size_t>(i)] == c.quality)
                return i;
    }
    switch (c.quality) {
    case Q::add9:
    case Q::six9:
    case Q::d7b9:
        return 2;
    case Q::aug:
        return 0;
    default:
        return 1;
    }
}

std::string_view functionWord(Function f) noexcept {
    switch (f) {
    case Function::home:
        return "HOME";
    case Function::away:
        return "AWAY";
    case Function::pull:
        return "PULL";
    case Function::colour:
        return "COLOUR";
    }
    return "";
}

Analysis analyze(const Chord& c, Key k) {
    const auto& s = scaleIntervals(k.mode);
    const int iv = mod12(c.root - k.tonic);
    int deg = indexOf(s, iv);
    const auto& qi = qualityInfo(c.quality);
    const Family f = qi.family;

    std::array<int, 4> tones{};
    int toneCount = 3;
    switch (f) {
    case F::sus:
        tones = {0, 5, 7};
        break;
    case F::min:
        tones = {0, 3, 7};
        break;
    case F::hdim:
    case F::dim:
        tones = {0, 3, 6};
        break;
    case F::aug:
        tones = {0, 4, 8};
        break;
    default:
        tones = {0, 4, 7};
        break;
    }
    // A seventh chord is only diatonic if its seventh is in the scale too.
    for (std::size_t i = 0; i < qi.intervalCount; ++i)
        if (qi.intervals[i] == 10 || qi.intervals[i] == 11) {
            tones[3] = qi.intervals[i];
            toneCount = 4;
            break;
        }
    bool diat = deg >= 0;
    for (int i = 0; i < toneCount && diat; ++i)
        diat = inScale(k, c.root + tones[static_cast<std::size_t>(i)]);
    if (k.mode == Mode::minor && iv == 7 && (f == F::dom || f == F::maj))
        diat = true; // harmonic-minor V
    if (k.mode == Mode::minor && iv == 11 && (f == F::dim || f == F::hdim)) {
        diat = true; // leading-tone vii°7
        deg = 6;
    }

    std::string num;
    if (deg >= 0) {
        num = std::string(kRoman[static_cast<std::size_t>(deg)]);
    } else {
        // Spell by the degree the root is altered from: raised 3rd/6th/7th in minor and the tritone read #, the rest
        // are lowered degrees.
        const int up = indexOf(s, mod12(iv + 1)), dn = indexOf(s, mod12(iv - 1));
        if (k.mode == Mode::minor && (iv == 4 || iv == 9 || iv == 11) && dn >= 0)
            num = "#" + std::string(kRoman[static_cast<std::size_t>(dn)]);
        else if (iv == 6)
            num = "#" + std::string(kRoman[3]);
        else if (up >= 0)
            num = "b" + std::string(kRoman[static_cast<std::size_t>(up)]);
        else
            num = "#" + std::string(kRoman[static_cast<std::size_t>(std::max(0, dn))]);
    }
    if (f == F::min || f == F::hdim || f == F::dim)
        num = lowerRoman(num);
    if (f == F::hdim)
        num += "\xc3\xb8"; // ø
    else if (f == F::dim)
        num += "\xc2\xb0"; // °
    else if (f == F::aug)
        num += "+";
    num += qi.numeral;

    if (!diat && f == F::dom) {
        const int td = indexOf(s, mod12(iv - 7));
        if (td > 0) {
            const Chord tc = diatonic(k, td, 0);
            const auto tf = qualityInfo(tc.quality).family;
            const bool minorTarget = (tf == F::min || tc.quality == Q::hdim) && !(k.mode == Mode::minor && td == 4);
            const std::string tn(kRoman[static_cast<std::size_t>(td)]);
            num = "V" + std::string(qi.numeral) + "/" + (minorTarget ? lowerRoman(tn) : tn);
        }
    }

    Function fn = Function::colour;
    if (diat) {
        if (deg == 0 || deg == 2 || deg == 5)
            fn = Function::home;
        else if (deg == 1 || deg == 3)
            fn = Function::away;
        else
            fn = Function::pull;
    }
    return {num, fn, diat, deg};
}

float tension(const Chord& c, const Analysis& a) {
    float t = 0.2f;
    switch (a.function) {
    case Function::home:
        t = 0.2f;
        break;
    case Function::away:
        t = 0.45f;
        break;
    case Function::pull:
        t = 0.84f;
        break;
    case Function::colour:
        t = 0.62f;
        break;
    }
    if (qualityInfo(c.quality).intervalCount > 4)
        t += 0.06f;
    if (c.quality == Q::dim7)
        t += 0.08f;
    return std::min(1.0f, t);
}

bool isPivot(const Chord& c, Key from, Key to) {
    return analyze(c, from).diatonic && (analyze(c, to).diatonic || analyze(c, parallel(to)).diatonic);
}

Voicing voiceChord(const Chord& c, const std::vector<int>* previousLead, bool voiceLead) {
    const auto& qi = qualityInfo(c.quality);
    std::vector<int> pcs;
    for (std::size_t i = 0; i < qi.voicingCount; ++i) {
        const int pc = mod12(c.root + qi.voicing[i]);
        if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end())
            pcs.push_back(pc);
    }
    const auto mean = [](const std::vector<int>& v) {
        return static_cast<double>(std::accumulate(v.begin(), v.end(), 0)) / static_cast<double>(v.size());
    };

    std::vector<int> best;
    if (!voiceLead) {
        int m = 57;
        while (mod12(m) != pcs.front())
            ++m;
        best.push_back(m);
        for (std::size_t i = 1; i < pcs.size(); ++i) {
            int n = best.back() + 1;
            while (mod12(n) != pcs[i])
                ++n;
            best.push_back(n);
        }
    } else {
        constexpr int lo = 55, hi = 80;
        std::vector<std::vector<int>> options(pcs.size());
        for (std::size_t i = 0; i < pcs.size(); ++i)
            for (int m = lo; m <= hi; ++m)
                if (mod12(m) == pcs[i])
                    options[i].push_back(m);
        double bestScore = 1e9;
        std::vector<std::size_t> idx(pcs.size(), 0);
        std::vector<int> candidate(pcs.size());
        while (true) {
            for (std::size_t i = 0; i < pcs.size(); ++i)
                candidate[i] = options[i][idx[i]];
            std::vector<int> sorted = candidate;
            std::sort(sorted.begin(), sorted.end());
            if (sorted.back() - sorted.front() <= 15) {
                double score = 0.0;
                if (previousLead != nullptr && !previousLead->empty()) {
                    if (previousLead->size() == sorted.size()) {
                        for (std::size_t k = 0; k < sorted.size(); ++k)
                            score += std::abs(sorted[k] - (*previousLead)[k]);
                    } else {
                        score = std::abs(mean(sorted) - mean(*previousLead)) * 1.5 + 2.0;
                    }
                }
                score += std::abs(mean(sorted) - 66.0) * 0.3;
                if (score < bestScore) {
                    bestScore = score;
                    best = sorted;
                }
            }
            std::size_t d = 0;
            while (d < idx.size() && ++idx[d] == options[d].size())
                idx[d++] = 0;
            if (d == idx.size())
                break;
        }
    }

    std::vector<int> notes = best;
    if (c.style == VoicingStyle::drop2 && notes.size() >= 4)
        notes[notes.size() - 2] -= 12;
    if (c.style == VoicingStyle::spread) {
        notes[0] -= 12;
        if (notes.size() >= 4)
            notes[1] -= 12;
    }
    std::sort(notes.begin(), notes.end());
    for (int i = 0; i < c.inversion; ++i) {
        notes.front() += 12;
        std::sort(notes.begin(), notes.end());
    }
    for (int i = 0; i > c.inversion; --i) {
        notes.back() -= 12;
        std::sort(notes.begin(), notes.end());
    }
    return {notes, best};
}

int bassPitch(int pc) noexcept {
    int m = 36;
    while (mod12(m) != mod12(pc))
        ++m;
    return m;
}

} // namespace lg
