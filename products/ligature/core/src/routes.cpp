// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/routes.h"

#include <array>

namespace lg {

namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(RouteId::count)> kIds{
    "pedal", "pivot", "backdoor", "dominant", "neapolitan", "dim7", "hexatonic", "slam"};

Chord withBeats(Chord c, float beats, int bass = -1) {
    c.beats = beats;
    if (bass >= 0)
        c.bass = mod12(bass);
    return c;
}

} // namespace

std::string_view routeIdString(RouteId id) noexcept { return kIds[static_cast<std::size_t>(id)]; }

std::optional<RouteId> routeFromString(std::string_view s) noexcept {
    for (std::size_t i = 0; i < kIds.size(); ++i)
        if (kIds[i] == s)
            return static_cast<RouteId>(i);
    return std::nullopt;
}

std::vector<Route> routesFor(Key from, Key to) {
    const int T = to.tonic;
    std::vector<Route> rs;

    // The pedal is the new tonic or its dominant, whichever the old key already has.
    int pedal = -1;
    if (inScale(from, T))
        pedal = mod12(T);
    else if (inScale(from, T + 7))
        pedal = mod12(T + 7);
    if (pedal >= 0) {
        rs.push_back(
            {RouteId::pedal,
             "Common tone",
             "The bass holds a note both keys share while the chords change above it.",
             1,
             {withBeats(diatonic(from, 0, 2), 4.0f, pedal), withBeats(makeChord(T + 5, Quality::m6), 4.0f, pedal)}});
    }

    // A pivot is a chord diatonic to both keys, preferring the new key's pre-dominants (IV, ii, vi).
    for (int d : {3, 1, 5, 2, 4, 6}) {
        const Chord c = diatonic(to, d, 2);
        const auto a = analyze(c, from);
        if (a.diatonic && a.degree >= 0 &&
            qualityInfo(diatonic(from, a.degree, 2).quality).family == qualityInfo(c.quality).family) {
            rs.push_back({RouteId::pivot,
                          "Pivot chord",
                          "A chord that belongs to both keys becomes the hinge.",
                          1,
                          {withBeats(c, 4.0f), makeChord(T + 7, Quality::d9, 4.0f)}});
            break;
        }
    }

    rs.push_back({RouteId::backdoor,
                  "Backdoor",
                  "bVI and bVII borrowed from the new key's minor side walk into its tonic.",
                  2,
                  {makeChord(T + 8, Quality::maj9, 4.0f), makeChord(T + 10, Quality::d9, 4.0f)}});

    const Chord ii = to.mode == Mode::major ? makeChord(T + 2, Quality::m9) : makeChord(T + 2, Quality::hdim);
    const Chord V = makeChord(T + 7, Quality::d7b9);
    rs.push_back({RouteId::dominant,
                  "ii-V",
                  "A ii-V of the new key. The classic, and the least surprising.",
                  2,
                  {withBeats(ii, 4.0f), withBeats(V, 4.0f)}});

    rs.push_back({RouteId::neapolitan,
                  "Neapolitan",
                  "bII of the new key moves to its dominant. Dark and cinematic.",
                  3,
                  {makeChord(T + 1, Quality::maj9, 4.0f), makeChord(T + 7, Quality::sus4, 2.0f),
                   makeChord(T + 7, Quality::d7b9, 2.0f)}});

    rs.push_back({RouteId::dim7,
                  "Diminished pivot",
                  "A diminished seventh can resolve to four keys; this one resolves to the new tonic.",
                  3,
                  {makeChord(T + 11, Quality::dim7, 4.0f), withBeats(ii, 2.0f), withBeats(V, 2.0f)}});

    // Hexatonic pole: minor r -> major r+4, major r -> minor r+8. No shared notes; every voice moves a semitone.
    const Chord pole = from.mode == Mode::minor ? makeChord(from.tonic + 4, Quality::maj9, 8.0f)
                                                : makeChord(from.tonic + 8, Quality::m9, 8.0f);
    rs.push_back({RouteId::hexatonic,
                  "Hexatonic pole",
                  "The home chord jumps to its hexatonic pole: no shared notes, but every voice moves one semitone. "
                  "The film-score move.",
                  4,
                  {pole}});

    rs.push_back({RouteId::slam,
                  "Stutter slam",
                  "The new dominant, stuttered, then a hard cut onto the downbeat.",
                  5,
                  {withBeats(V, 8.0f)}});
    return rs;
}

Route routeOrFirst(Key from, Key to, RouteId wanted) {
    auto rs = routesFor(from, to);
    for (auto& r : rs)
        if (r.id == wanted)
            return r;
    return rs.front();
}

} // namespace lg
