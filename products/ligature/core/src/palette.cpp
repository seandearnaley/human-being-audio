// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/palette.h"

#include <algorithm>
#include <set>
#include <string>

namespace lg {

namespace {

using Q = Quality;

const std::vector<Progression>& library() {
    static const std::vector<Progression> kLibrary{
        // Minor
        {"Garage minor", "Garage", Mode::minor, {{0, Q::m9}, {8, Q::maj9}, {5, Q::m9}, {7, Q::d7b9}}},
        {"Future garage", "Garage", Mode::minor, {{0, Q::m9}, {7, Q::m7}, {8, Q::maj9}, {5, Q::m9}}},
        {"m9 planing", "House", Mode::minor, {{0, Q::m9}, {10, Q::m9}, {8, Q::m9}, {10, Q::m9}}},
        {"Dorian vamp", "House", Mode::minor, {{0, Q::m9}, {5, Q::d9}, {0, Q::m9}, {5, Q::d13}}},
        {"Liquid lift", "DnB", Mode::minor, {{0, Q::m9}, {8, Q::maj9}, {3, Q::maj9}, {10, Q::six9}}},
        {"Epic minor", "Trance", Mode::minor, {{0, Q::m9}, {8, Q::maj9}, {3, Q::maj9}, {10, Q::d9}}},
        {"Andalusian", "Pop", Mode::minor, {{0, Q::m9}, {10, Q::d9}, {8, Q::maj9}, {7, Q::d7b9}}},
        {"Neo-soul turn", "Neo-soul", Mode::minor, {{5, Q::m9}, {7, Q::d7b9}, {0, Q::m11}, {8, Q::maj9}}},
        {"Afro minor", "Afro", Mode::minor, {{0, Q::m9}, {5, Q::m9}, {10, Q::d9}, {3, Q::maj9}}},
        {"Drill", "Drill", Mode::minor, {{0, Q::min}, {8, Q::maj7}, {7, Q::d7b9}, {0, Q::m6}}},
        {"Lo-fi ii-V-i", "Lo-fi", Mode::minor, {{2, Q::hdim}, {7, Q::d7b9}, {0, Q::m9}, {0, Q::m9}}},
        {"Phrygian pulse", "Techno", Mode::minor, {{0, Q::m9}, {1, Q::maj9}, {0, Q::m9}, {10, Q::d9}}},
        // Major
        {"Axis", "Pop", Mode::major, {{0, Q::maj9}, {7, Q::add9, 11}, {9, Q::m9}, {5, Q::maj9}}},
        {"Sad axis", "Pop", Mode::major, {{9, Q::m9}, {5, Q::maj9}, {0, Q::maj9}, {7, Q::add9}}},
        {"Doo-wop", "Pop", Mode::major, {{0, Q::maj9}, {9, Q::m9}, {5, Q::maj9}, {7, Q::d9}}},
        {"Royal road", "City pop", Mode::major, {{5, Q::maj9}, {7, Q::d13}, {4, Q::m7}, {9, Q::m9}}},
        {"City pop", "City pop", Mode::major, {{5, Q::maj9}, {4, Q::m7}, {9, Q::m9}, {2, Q::d9}}},
        {"Garage major", "Garage", Mode::major, {{2, Q::m9}, {7, Q::d13}, {0, Q::maj9}, {9, Q::m9}}},
        {"Mixolydian", "House", Mode::major, {{0, Q::add9}, {10, Q::maj9}, {5, Q::maj9}, {0, Q::add9}}},
        {"Creep", "Pop", Mode::major, {{0, Q::maj9}, {4, Q::d9}, {5, Q::maj9}, {5, Q::m6}}},
        {"Lydian float", "Ambient", Mode::major, {{0, Q::maj9s11}, {2, Q::add9}, {0, Q::maj9s11}, {2, Q::d9}}},
        {"Minor plagal", "Ambient", Mode::major, {{0, Q::maj9}, {4, Q::m7}, {5, Q::maj9}, {5, Q::m6}}},
        {"Gospel lift", "Neo-soul", Mode::major, {{5, Q::maj9}, {4, Q::m7}, {2, Q::m9}, {7, Q::sus4}}},
        {"Pachelbel",
         "Pop",
         Mode::major,
         {{0, Q::maj9}, {7, Q::add9}, {9, Q::m9}, {4, Q::m7}, {5, Q::maj9}, {0, Q::maj9}, {5, Q::maj9}, {7, Q::d9}}},
    };
    return kLibrary;
}

} // namespace

std::vector<PaletteEntry> neighbourhood(const Section& section, int index) {
    std::vector<PaletteEntry> out;
    if (section.chords.empty())
        return out;
    const int n = static_cast<int>(section.chords.size());
    const int i = std::clamp(index, 0, n - 1);
    const Key k = section.key;
    const Chord& c = section.chords[static_cast<std::size_t>(i)];
    const Chord& prev = section.chords[static_cast<std::size_t>((i - 1 + n) % n)];
    const Chord& next = section.chords[static_cast<std::size_t>((i + 1) % n)];
    const int lvl = std::clamp(levelOf(c), 1, 2);
    const int T = k.tonic;

    std::vector<Chord> inner;
    for (int d = 0; d < 7; ++d) {
        const Chord x = diatonic(k, d, lvl);
        if (x.root != c.root)
            inner.push_back(x);
    }
    if (k.mode == Mode::minor)
        for (auto& x : inner)
            if (x.root == mod12(T + 7))
                x = makeChord(T + 7, Q::d7b9); // harmonic V

    std::vector<Chord> mid =
        k.mode == Mode::major
            ? std::vector<Chord>{makeChord(T + 5, Q::m9), makeChord(T + 8, Q::maj9), makeChord(T + 10, Q::d9),
                                 makeChord(T + 3, Q::maj9), makeChord(T + 2, Q::hdim)}
            : std::vector<Chord>{makeChord(T + 5, Q::d9), makeChord(T + 1, Q::maj9), makeChord(T, Q::maj9),
                                 makeChord(T + 2, Q::m9), makeChord(T + 11, Q::dim7)};
    mid.push_back(makeChord(next.root + 7, Q::d7b9)); // dominant of the next chord
    mid.push_back(makeChord(next.root + 1, Q::d9));   // its tritone substitute

    const int r = c.root;
    const auto fam = qualityInfo(c.quality).family;
    const bool isMin = fam == Family::min || fam == Family::hdim || fam == Family::dim;
    const Q same = isMin ? Q::m9 : Q::maj9;
    const std::vector<Chord> outside{makeChord(r, isMin ? Q::maj9 : Q::m9),                       // P
                                     isMin ? makeChord(r - 4, Q::maj9) : makeChord(r + 4, Q::m9), // L
                                     isMin ? makeChord(r + 3, Q::maj9) : makeChord(r - 3, Q::m9), // R
                                     makeChord(r + 4, same),
                                     makeChord(r - 4, same), // chromatic mediants
                                     makeChord(r + 3, same),
                                     makeChord(r - 3, same),
                                     isMin ? makeChord(r + 4, Q::maj9) : makeChord(r + 8, Q::m9), // hexatonic pole
                                     makeChord(r, Q::q4),
                                     makeChord(r + 6, same)};

    std::set<std::string> seen{chordName(c)};
    const auto add = [&](const std::vector<Chord>& list, Ring ring, int limit) {
        int count = 0;
        for (const auto& x : list) {
            if (count >= limit)
                break;
            if (!seen.insert(chordName(x)).second)
                continue;
            Chord placed = x;
            placed.beats = c.beats;
            out.push_back({placed, ring, static_cast<int>(sharedPcs(x, prev).size()),
                           static_cast<int>(sharedPcs(x, next).size())});
            ++count;
        }
    };
    add(inner, Ring::inKey, kRingSize[0]);
    add(mid, Ring::borrowed, kRingSize[1]);
    add(outside, Ring::outside, kRingSize[2]);
    return out;
}

std::optional<Chord> identifyChord(std::uint16_t heldMask, int lowestPc) {
    int held = 0;
    for (int b = 0; b < 12; ++b)
        held += (heldMask >> b) & 1u;
    if (held < 3)
        return std::nullopt;
    int bestScore = -1000;
    Chord best;
    for (int q = 0; q < static_cast<int>(Quality::count); ++q) {
        const auto quality = static_cast<Quality>(q);
        if (quality == Quality::q4)
            continue;
        for (int root = 0; root < 12; ++root) {
            const std::uint16_t tpl = pcMask(makeChord(root, quality));
            int hits = 0, extra = 0, missing = 0;
            for (int b = 0; b < 12; ++b) {
                const bool in = ((heldMask >> b) & 1u) != 0u, want = ((tpl >> b) & 1u) != 0u;
                hits += in && want ? 1 : 0;
                extra += in && !want ? 1 : 0;
                missing += !in && want ? 1 : 0;
            }
            // Every held note should be explained; a missing fifth or ninth costs less than a wrong note.
            const int score = hits * 4 - extra * 5 - missing * 2 + (root == mod12(lowestPc) ? 2 : 0) -
                              static_cast<int>(qualityInfo(quality).intervalCount) / 4;
            if (score > bestScore) {
                bestScore = score;
                best = makeChord(root, quality, 4.0f, mod12(lowestPc) != root ? lowestPc : -1);
            }
        }
    }
    return best;
}

std::span<const Progression> progressions() { return library(); }

std::vector<const Progression*> progressionsFor(Mode mode) {
    std::vector<const Progression*> out;
    for (const auto& p : library())
        if (p.mode == mode)
            out.push_back(&p);
    return out;
}

std::vector<Chord> realise(const Progression& p, Key key, int bars) {
    std::vector<Chord> out;
    const int count = static_cast<int>(p.chords.size());
    const float total = static_cast<float>(bars * 4);
    float at = 0.0f;
    for (int i = 0; i < count; ++i) {
        const auto& pc = p.chords[static_cast<std::size_t>(i)];
        // Share the bars evenly on the quarter-note grid; the last chord takes whatever is left.
        const float end =
            i == count - 1 ? total : std::round(total * static_cast<float>(i + 1) / static_cast<float>(count));
        const float beats = std::max(1.0f, end - at);
        out.push_back(makeChord(key.tonic + pc.interval, pc.quality, beats,
                                pc.bassInterval >= 0 ? key.tonic + pc.bassInterval : -1));
        at += beats;
        if (at >= total)
            break;
    }
    return out;
}

} // namespace lg
