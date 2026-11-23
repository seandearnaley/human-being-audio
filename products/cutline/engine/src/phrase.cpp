// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/phrase.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace cl {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

struct Rng {
    std::uint32_t state = 0x9e3779b9U;

    explicit constexpr Rng(std::uint32_t seed) noexcept CL_NONBLOCKING : state(hba::dsp::laneHash(seed ^ 0x9e3779b9U)) {
    }

    constexpr std::uint32_t nextU32() noexcept CL_NONBLOCKING {
        state = hba::dsp::laneHash(state + 0x9e3779b9U);
        return state;
    }

    constexpr double uniform() noexcept CL_NONBLOCKING { return static_cast<double>(nextU32()) / 4294967296.0; }

    constexpr int integer(int lo, int hi) noexcept CL_NONBLOCKING {
        if (hi <= lo)
            return lo;
        const auto span = static_cast<std::uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(nextU32() % span);
    }
};

struct ScaleDef {
    std::array<int, 12> intervals{};
    int count = 7;
};

constexpr std::array<ScaleDef, 6> kScales{{
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},   // Phrygian (dark neurofunk)
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},   // Natural Minor
    {{0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},   // Phrygian Dominant
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}, 7},   // Dorian
    {{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}, 5},   // Pentatonic Minor
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 12}, // Chromatic
}};

struct RhythmCell {
    std::array<double, 8> offsets{};
    int count = 4;
};

// Curated seed-selectable rhythmic skeletons per grammar (4 distinct cells each).
constexpr std::array<std::array<RhythmCell, 4>, 5> kGrammarCells{{
    // 0: Neuro (syncopated Reese & techstep pockets)
    {{
        {{0.0, 0.75, 1.5, 2.25, 2.75, 3.5, 0.0, 0.0}, 6},
        {{0.0, 0.5, 1.25, 2.0, 2.75, 3.25, 0.0, 0.0}, 6},
        {{0.0, 0.75, 1.75, 2.5, 3.25, 0.0, 0.0, 0.0}, 5},
        {{0.0, 1.0, 1.5, 2.25, 3.0, 3.5, 0.0, 0.0}, 6},
    }},
    // 1: BBCut11 (chopped breakbeat & amen syncopation)
    {{
        {{0.0, 0.75, 1.5, 2.5, 3.25, 3.75, 0.0, 0.0}, 6},
        {{0.0, 0.5, 1.0, 1.75, 2.5, 3.0, 3.5, 0.0}, 7},
        {{0.0, 0.75, 1.25, 2.0, 2.75, 3.5, 0.0, 0.0}, 6},
        {{0.0, 0.5, 1.5, 2.25, 2.75, 3.25, 3.75, 0.0}, 7},
    }},
    // 2: WarpCut (IDM / drill-n-bass odd-grouped cuts)
    {{
        {{0.0, 0.5, 1.25, 1.75, 2.5, 3.0, 3.5, 0.0}, 7},
        {{0.0, 0.375, 0.75, 1.5, 2.25, 2.625, 3.25, 0.0}, 7},
        {{0.0, 0.75, 1.5, 2.0, 2.5, 3.125, 3.625, 0.0}, 7},
        {{0.0, 0.625, 1.25, 2.0, 2.625, 3.25, 3.75, 0.0}, 7},
    }},
    // 3: Halftime (heavy 85/170 half-time space & sub stabs)
    {{
        {{0.0, 1.5, 2.75, 3.5, 0.0, 0.0, 0.0, 0.0}, 4},
        {{0.0, 1.25, 2.0, 3.25, 0.0, 0.0, 0.0, 0.0}, 4},
        {{0.0, 0.75, 2.25, 3.0, 3.75, 0.0, 0.0, 0.0}, 5},
        {{0.0, 1.75, 2.5, 3.5, 0.0, 0.0, 0.0, 0.0}, 4},
    }},
    // 4: Liquid (rolling legato chords & basslines)
    {{
        {{0.0, 1.25, 2.5, 3.5, 0.0, 0.0, 0.0, 0.0}, 4},
        {{0.0, 0.75, 1.5, 2.5, 3.25, 0.0, 0.0, 0.0}, 5},
        {{0.0, 1.5, 2.25, 3.0, 0.0, 0.0, 0.0, 0.0}, 4},
        {{0.0, 1.0, 2.25, 3.25, 3.75, 0.0, 0.0, 0.0}, 5},
    }},
}};

int scaleDegreeToMidi(int degree, int baseMidi, const ScaleDef& sc, bool octaveLift) noexcept CL_NONBLOCKING {
    int octShift = octaveLift ? 12 : 0;
    int d = degree;
    while (d < 0) {
        d += sc.count;
        octShift -= 12;
    }
    while (d >= sc.count) {
        d -= sc.count;
        octShift += 12;
    }
    return std::clamp(baseMidi + sc.intervals[static_cast<std::size_t>(d)] + octShift, 12, 120);
}

int effectiveBaseMidi(const Params& params, const HeldNotes& held) noexcept CL_NONBLOCKING {
    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode == InputMode::transpose && held.count > 0) {
        // Most recently played key sets the live root pitch directly.
        return std::clamp(static_cast<int>(held.notes[static_cast<std::size_t>(held.count - 1)]), 12, 96);
    }
    const int root = std::clamp(params.choice(ParamId::rootNote), 0, 11);
    const int oct = std::clamp(params.choice(ParamId::octave), 0, 3) + 1; // MIDI C1=24, F1=29, F2=41
    return 12 * (oct + 1) + root;
}

int resolvePitch(int degree, const Params& params, const HeldNotes& held, bool octaveLift) noexcept CL_NONBLOCKING {
    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode == InputMode::chordCarve && held.count > 0) {
        const int idx = ((degree % held.count) + held.count) % held.count;
        const int octWrap = (degree >= held.count ? 12 : 0) + (octaveLift ? 12 : 0);
        const int base = static_cast<int>(held.notes[static_cast<std::size_t>(idx)]);
        return std::clamp(base + octWrap, 12, 120);
    }
    const auto scaleIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::scale), 0, 5));
    return scaleDegreeToMidi(degree, effectiveBaseMidi(params, held), kScales[scaleIdx], octaveLift);
}

int resolveHarmonyPitch(int baseDegree, int diatonicInterval, const Params& params, const HeldNotes& held,
                        bool octaveLift) noexcept CL_NONBLOCKING {
    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode == InputMode::chordCarve && held.count > 1) {
        const int idx = ((baseDegree + 1) % held.count + held.count) % held.count;
        const int base = static_cast<int>(held.notes[static_cast<std::size_t>(idx)]);
        return std::clamp(base + (octaveLift ? 12 : 0), 12, 120);
    }
    const auto scaleIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::scale), 0, 5));
    return scaleDegreeToMidi(baseDegree + diatonicInterval, effectiveBaseMidi(params, held), kScales[scaleIdx],
                             octaveLift);
}

Contour resolveContour(Contour requested, Grammar grammar, Rng& rng) noexcept CL_NONBLOCKING {
    if (requested != Contour::autoShape)
        return requested;
    switch (grammar) {
    case Grammar::neuro:
        return rng.uniform() < 0.60 ? Contour::pedal : Contour::arch;
    case Grammar::bbcut11:
        return rng.uniform() < 0.55 ? Contour::acid : Contour::arch;
    case Grammar::warpcut:
        return rng.uniform() < 0.50 ? Contour::acid : Contour::fall;
    case Grammar::halftime:
        return rng.uniform() < 0.65 ? Contour::pedal : Contour::fall;
    case Grammar::liquid:
        return rng.uniform() < 0.55 ? Contour::arch : Contour::rise;
    }
    return Contour::pedal;
}

int pickMotifDegree(int index, int count, Contour contour, Rng& rng) noexcept CL_NONBLOCKING {
    if (index == 0)
        return 0; // Strong root anchor on beat 1
    const double t = count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1) : 0.0;
    const double u = rng.uniform();
    switch (contour) {
    case Contour::autoShape:
    case Contour::pedal:
        if (u < 0.34)
            return 0;
        if (u < 0.58)
            return 1; // Dark b2 / 2nd sting
        if (u < 0.78)
            return 2; // 3rd
        if (u < 0.92)
            return 4; // 5th
        return 5;
    case Contour::arch: {
        const int peak = static_cast<int>(std::lround(4.5 * std::sin(3.141592653589793 * t)));
        return std::clamp(peak + (u < 0.3 ? -1 : (u > 0.75 ? 1 : 0)), 0, 6);
    }
    case Contour::rise:
        return std::clamp(static_cast<int>(std::lround(t * 5.0)) + (u > 0.7 ? 1 : 0), 0, 6);
    case Contour::fall:
        return std::clamp(static_cast<int>(std::lround((1.0 - t) * 5.0)) + (u < 0.25 ? 1 : 0), 0, 6);
    case Contour::acid:
        if (u < 0.28)
            return 0;
        if (u < 0.50)
            return 7; // Octave jump
        if (u < 0.72)
            return 4; // 5th
        if (u < 0.88)
            return 6; // b7
        return 1;
    }
    return 0;
}

void pushNote(PhrasePlan& out, const CompiledNote& note) noexcept CL_NONBLOCKING {
    if (out.noteCount >= kMaxPhraseNotes)
        return;
    out.notes[static_cast<std::size_t>(out.noteCount++)] = note;
}

void pushSpan(PhrasePlan& out, double startBeats, double endBeats, Role role, bool locked) noexcept CL_NONBLOCKING {
    if (out.spanCount >= kMaxRoleSpans)
        return;
    out.spans[static_cast<std::size_t>(out.spanCount++)] = {startBeats, endBeats, role, locked};
}

} // namespace

bool isNoteInScale(int midiNote, int root, Scale scale) noexcept CL_NONBLOCKING {
    const int pc = ((midiNote - root) % 12 + 12) % 12;
    const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(scale), 0, 5));
    const ScaleDef& sc = kScales[idx];
    for (int i = 0; i < sc.count; ++i) {
        if (sc.intervals[static_cast<std::size_t>(i)] == pc)
            return true;
    }
    return false;
}

int snapNoteToScale(int midiNote, int root, Scale scale) noexcept CL_NONBLOCKING {
    const int clamped = std::clamp(midiNote, 12, 120);
    if (isNoteInScale(clamped, root, scale))
        return clamped;
    for (int delta = 1; delta <= 6; ++delta) {
        if (clamped + delta <= 120 && isNoteInScale(clamped + delta, root, scale))
            return clamped + delta;
        if (clamped - delta >= 12 && isNoteInScale(clamped - delta, root, scale))
            return clamped - delta;
    }
    return clamped;
}

double warpBeat(double beat, double gravityNorm, Attractor attractor) noexcept CL_NONBLOCKING {
    const double g = std::clamp(gravityNorm, -1.0, 1.0);
    if (std::abs(g) < 1.0e-5)
        return beat;

    const double barIdx = std::floor(beat / 4.0);
    const double u = (beat - barIdx * 4.0) * 0.25; // [0, 1)

    double k = 4.0;
    double u0 = 0.0;
    switch (attractor) {
    case Attractor::downbeat:
        k = 2.0;
        u0 = 0.0;
        break;
    case Attractor::backbeat:
        k = 2.0;
        u0 = 0.25;
        break;
    case Attractor::offbeat16:
        k = 4.0;
        u0 = 0.0;
        break;
    case Attractor::triplet:
        k = 3.0;
        u0 = 0.0;
        break;
    }

    // |alpha| <= 0.40 < 1.0 guarantees tau'(u) = 1 + alpha * cos(...) >= 0.60 > 0 everywhere.
    const double alpha = 0.40 * g;
    const double warpedU = u + (alpha / (kTwoPi * k)) * std::sin(kTwoPi * k * (u - u0));
    const double clampedU = std::clamp(warpedU, 0.0, 0.9999);
    return barIdx * 4.0 + clampedU * 4.0;
}

std::uint32_t phraseSignature(const Params& params, std::int64_t phraseIndex, const HeldNotes& held,
                              const PhraseOverrides& overrides) noexcept CL_NONBLOCKING {
    const auto phraseMode = static_cast<PhraseMode>(std::clamp(params.choice(ParamId::phraseMode), 0, 1));
    const std::int64_t effIdx = phraseMode == PhraseMode::loop ? 0 : phraseIndex;

    std::uint32_t h = hba::dsp::laneHash(static_cast<std::uint32_t>(effIdx ^ 0x517cc1b7LL));
    const auto mix = [&](std::uint32_t v) noexcept CL_NONBLOCKING {
        h = hba::dsp::laneHash(h ^ (v * 0x9e3779b9U + 0x27d4eb2dU));
    };
    const auto q = [](float f) noexcept CL_NONBLOCKING {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(f * 1000.0f)));
    };

    mix(static_cast<std::uint32_t>(params.choice(ParamId::grammar)));
    mix(q(params[ParamId::gravity]));
    mix(q(params[ParamId::ratchet]));
    mix(q(params[ParamId::glide]));
    mix(q(params[ParamId::density]));
    mix(q(params[ParamId::mutate]));
    mix(q(params[ParamId::velArc]));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::outputMode)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::inputMode)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::phraseBars)));
    mix(static_cast<std::uint32_t>(params.integer(ParamId::seed)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::attractor)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::rootNote)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::octave)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::scale)));
    mix(q(params[ParamId::legato]));
    mix(static_cast<std::uint32_t>(phraseMode));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::contour)));

    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode != InputMode::seed) {
        mix(static_cast<std::uint32_t>(held.count));
        for (int i = 0; i < held.count; ++i)
            mix(held.notes[static_cast<std::size_t>(i)]);
    }

    if (overrides.hasAny()) {
        mix(q(overrides.fillStartFrac));
        mix(q(overrides.turnStartFrac));
        for (const auto& s : overrides.spans) {
            mix((s.locked ? 1U : 0U) | (static_cast<std::uint32_t>(s.lockedSeed) << 4U) |
                (static_cast<std::uint32_t>(s.roleOverride + 2) << 16U));
        }
        for (const auto& n : overrides.notes) {
            if (!n.active())
                continue;
            mix(static_cast<std::uint32_t>(n.slotIndex) | (static_cast<std::uint32_t>(n.pitchDelta + 32) << 9U) |
                (static_cast<std::uint32_t>(n.velocityDelta + 64) << 16U) | (n.muted ? (1U << 24U) : 0U) |
                (n.flipBend ? (1U << 25U) : 0U));
        }
    }
    return h;
}

void compilePhrase(const Params& params, std::int64_t phraseIndex, const HeldNotes& held, PhrasePlan& out,
                   const PhraseOverrides& overrides) noexcept CL_NONBLOCKING {
    out.phraseIndex = phraseIndex;
    out.noteCount = 0;
    out.spanCount = 0;

    const auto barsIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::phraseBars), 0, 2));
    const int bars = kPhraseBarCounts[barsIdx];
    const double totalBeats = static_cast<double>(bars * 4);
    out.phraseBeats = totalBeats;
    out.signature = phraseSignature(params, phraseIndex, held, overrides);

    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode == InputMode::chordCarve && held.count <= 0) {
        pushSpan(out, 0.0, totalBeats, Role::statement, false);
        return;
    }

    const auto grammar = static_cast<Grammar>(std::clamp(params.choice(ParamId::grammar), 0, 4));
    const auto mode = static_cast<OutputMode>(std::clamp(params.choice(ParamId::outputMode), 0, 2));
    const auto attractor = static_cast<Attractor>(std::clamp(params.choice(ParamId::attractor), 0, 3));
    const auto phraseMode = static_cast<PhraseMode>(std::clamp(params.choice(ParamId::phraseMode), 0, 1));
    const auto requestedContour = static_cast<Contour>(std::clamp(params.choice(ParamId::contour), 0, 5));
    const double gravity = static_cast<double>(std::clamp(params[ParamId::gravity], -1.0f, 1.0f));
    const float ratchetAmt = std::clamp(params[ParamId::ratchet], 0.0f, 1.0f);
    const float glideAmt = std::clamp(params[ParamId::glide], 0.0f, 1.0f);
    const float densityAmt = std::clamp(params[ParamId::density], 0.0f, 1.0f);
    const float mutateAmt = std::clamp(params[ParamId::mutate], 0.0f, 1.0f);
    const float velArcAmt = std::clamp(params[ParamId::velArc], 0.0f, 1.0f);
    const float legatoAmt = std::clamp(params[ParamId::legato], 0.0f, 1.0f);
    const int seed = std::clamp(params.integer(ParamId::seed), 1, 999);
    const std::int64_t effPhraseIdx = (phraseMode == PhraseMode::loop) ? 0 : phraseIndex;

    // 1. Partition phrase into structural BBCut-inspired roles with optional divider overrides.
    const double defaultTurnStart = (bars == 2) ? 7.25 : (bars == 4 ? 14.5 : 29.0);
    const double turnStart =
        overrides.turnStartFrac >= 0.0f
            ? std::clamp(std::round(static_cast<double>(overrides.turnStartFrac) * totalBeats * 4.0) * 0.25,
                         totalBeats * 0.72, totalBeats - 0.5)
            : defaultTurnStart;

    if (bars == 2) {
        const double defRat = ratchetAmt > 0.15f ? 6.0 : turnStart;
        const double ratStart =
            overrides.fillStartFrac >= 0.0f
                ? std::clamp(std::round(static_cast<double>(overrides.fillStartFrac) * totalBeats * 4.0) * 0.25, 4.0,
                             turnStart)
                : std::min(defRat, turnStart);
        pushSpan(out, 0.0, 3.5, Role::statement, false);
        pushSpan(out, 3.5, ratStart, Role::answer, false);
        if (ratStart < turnStart - 0.20)
            pushSpan(out, ratStart, turnStart, Role::ratchet, false);
        pushSpan(out, turnStart, 8.0, Role::turnaround, false);
    } else if (bars == 4) {
        const double defRat = 12.0 + (1.0 - static_cast<double>(ratchetAmt)) * 1.5;
        const double ratStart =
            overrides.fillStartFrac >= 0.0f
                ? std::clamp(std::round(static_cast<double>(overrides.fillStartFrac) * totalBeats * 4.0) * 0.25, 8.5,
                             turnStart)
                : std::min(defRat, turnStart);
        pushSpan(out, 0.0, 4.0, Role::statement, false);
        pushSpan(out, 4.0, 8.0, Role::answer, false);
        if ((ratchetAmt > 0.10f || overrides.fillStartFrac >= 0.0f) && ratStart < turnStart - 0.25) {
            pushSpan(out, 8.0, ratStart, Role::mutation, false);
            pushSpan(out, ratStart, turnStart, Role::ratchet, false);
        } else {
            pushSpan(out, 8.0, turnStart, Role::mutation, false);
        }
        pushSpan(out, turnStart, 16.0, Role::turnaround, false);
    } else {
        const double defRat = 24.0;
        const double ratStart =
            overrides.fillStartFrac >= 0.0f
                ? std::clamp(std::round(static_cast<double>(overrides.fillStartFrac) * totalBeats * 4.0) * 0.25, 17.0,
                             turnStart)
                : std::min(defRat, turnStart);
        pushSpan(out, 0.0, 8.0, Role::statement, false);
        pushSpan(out, 8.0, 16.0, Role::answer, false);
        pushSpan(out, 16.0, ratStart, Role::mutation, false);
        if ((ratchetAmt > 0.10f || overrides.fillStartFrac >= 0.0f) && ratStart < turnStart - 0.5)
            pushSpan(out, ratStart, turnStart, Role::ratchet, false);
        else
            pushSpan(out, ratStart, turnStart, Role::mutation, false);
        pushSpan(out, turnStart, 32.0, Role::turnaround, false);
    }

    // Apply span lock flags and explicit role overrides (including Role::rest).
    for (int s = 0; s < out.spanCount && s < PhraseOverrides::kMaxSpanOverrides; ++s) {
        const auto& sov = overrides.spans[static_cast<std::size_t>(s)];
        out.spans[static_cast<std::size_t>(s)].locked = sov.locked;
        if (sov.roleOverride >= 0 && sov.roleOverride <= static_cast<int>(Role::rest))
            out.spans[static_cast<std::size_t>(s)].role = static_cast<Role>(sov.roleOverride);
    }

    // Helper: deterministic core motif (rhythm + pitch degrees) from a given seed & grammar.
    // Never depends on phraseIndex, so Bar 1-4 and Bar 5-8 share one recognizable musical hook!
    const auto buildCoreMotif = [&](int spanSeed, RhythmCell& outCell, std::array<int, 8>& outDegrees,
                                    Contour& outContour) noexcept CL_NONBLOCKING {
        const std::uint32_t coreKey = hba::dsp::laneHash(static_cast<std::uint32_t>(spanSeed) * 2654435761U ^
                                                         (static_cast<std::uint32_t>(grammar) << 24U));
        Rng coreRng(coreKey);

        const auto gIdx = static_cast<std::size_t>(grammar);
        const auto cellIdx = static_cast<std::size_t>(coreRng.integer(0, 3));
        outCell = kGrammarCells[gIdx][cellIdx];

        // Recursive BBCut / WarpCut subdivision split on one random cell gap when density allows.
        if ((grammar == Grammar::bbcut11 || grammar == Grammar::warpcut) && outCell.count < 8 &&
            coreRng.uniform() < 0.65) {
            const int splitIdx = coreRng.integer(0, outCell.count - 2);
            const double mid = 0.5 * (outCell.offsets[static_cast<std::size_t>(splitIdx)] +
                                      outCell.offsets[static_cast<std::size_t>(splitIdx + 1)]);
            for (int j = outCell.count; j > splitIdx + 1; --j)
                outCell.offsets[static_cast<std::size_t>(j)] = outCell.offsets[static_cast<std::size_t>(j - 1)];
            outCell.offsets[static_cast<std::size_t>(splitIdx + 1)] = mid;
            ++outCell.count;
        }

        outContour = resolveContour(requestedContour, grammar, coreRng);
        for (int i = 0; i < outCell.count; ++i)
            outDegrees[static_cast<std::size_t>(i)] = pickMotifDegree(i, outCell.count, outContour, coreRng);
    };

    std::uint8_t nextMpeChannel = 2;
    const auto allocChannel = [&]() noexcept CL_NONBLOCKING -> std::uint8_t {
        if (mode != OutputMode::mpe)
            return 1;
        const std::uint8_t ch = nextMpeChannel;
        nextMpeChannel = nextMpeChannel >= 15 ? 2 : static_cast<std::uint8_t>(nextMpeChannel + 1);
        return ch;
    };

    const auto shapeVel = [&](int baseVel, double progress) noexcept CL_NONBLOCKING -> std::uint8_t {
        const double arc = (1.0 - static_cast<double>(velArcAmt)) +
                           static_cast<double>(velArcAmt) * (0.62 + 0.38 * std::sin(3.141592653589793 * progress));
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(baseVel * arc)), 24, 127));
    };

    // 2. Compile notes across each structural role span.
    for (int s = 0; s < out.spanCount; ++s) {
        const RoleSpan& span = out.spans[static_cast<std::size_t>(s)];
        if (span.role == Role::rest)
            continue;
        const double spanLen = std::max(0.25, span.endBeats - span.startBeats);

        // Locked spans use their captured seed and stay frozen across phrase evolutions.
        int spanSeed = seed;
        bool spanLocked = false;
        if (s < PhraseOverrides::kMaxSpanOverrides) {
            const auto& sov = overrides.spans[static_cast<std::size_t>(s)];
            if (sov.locked && sov.lockedSeed >= 1 && sov.lockedSeed <= 999) {
                spanSeed = static_cast<int>(sov.lockedSeed);
                spanLocked = true;
            }
        }

        RhythmCell motif{};
        std::array<int, 8> motifDegrees{};
        Contour activeContour = Contour::pedal;
        buildCoreMotif(spanSeed, motif, motifDegrees, activeContour);

        // Span-local deterministic RNG (independent of phraseIndex) + separate Evolution RNG.
        Rng spanRng(hba::dsp::laneHash(static_cast<std::uint32_t>(spanSeed) * 2246822519U ^
                                       (static_cast<std::uint32_t>(s + 1) * 3266489917U) ^
                                       (static_cast<std::uint32_t>(grammar) << 20U)));
        const bool evolveActive = !spanLocked && effPhraseIdx != 0 && mutateAmt > 0.05f;
        Rng evoRng(hba::dsp::laneHash(static_cast<std::uint32_t>(spanSeed) * 668265263U ^
                                      static_cast<std::uint32_t>(effPhraseIdx * 2246822519LL) ^
                                      (static_cast<std::uint32_t>(s + 7) * 374761393U)));

        if (mode == OutputMode::drum) {
            const double step = span.role == Role::ratchet ? (ratchetAmt > 0.65f ? 0.1875 : 0.25) : 0.5;
            for (double b = span.startBeats; b < span.endBeats - 1.0e-5; b += step) {
                const double barBeat = b - std::floor(b / 4.0) * 4.0;
                const double prog = (b - span.startBeats) / spanLen;
                int row = -1;
                int baseVel = 92;

                if (span.role == Role::ratchet) {
                    row = spanRng.uniform() < 0.62 ? 5 : 2;
                    baseVel = 58 + static_cast<int>(64.0 * prog);
                } else if (span.role == Role::turnaround) {
                    row = prog < 0.55 ? 1 : 0;
                    baseVel = 118;
                } else if (std::abs(barBeat - 0.0) < 0.01 ||
                           (grammar != Grammar::halftime && std::abs(barBeat - 2.5) < 0.01)) {
                    row = 0; // Kick
                    baseVel = 120;
                } else if (std::abs(barBeat - 1.0) < 0.01 || std::abs(barBeat - 3.0) < 0.01) {
                    row = (grammar == Grammar::halftime && std::abs(barBeat - 1.0) < 0.01) ? 3 : 1;
                    baseVel = row == 1 ? 114 : 84;
                } else {
                    const double prob = static_cast<double>(densityAmt) * (span.role == Role::mutation ? 0.92 : 0.70);
                    if (spanRng.uniform() < prob) {
                        const double pick = evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.4
                                                ? evoRng.uniform()
                                                : spanRng.uniform();
                        row = pick < 0.44 ? 2 : (pick < 0.84 ? 3 : 4);
                        baseVel = row == 2 ? spanRng.integer(46, 74) : spanRng.integer(68, 98);
                    }
                }

                if (row >= 0) {
                    const double warped = warpBeat(b, gravity, attractor);
                    const double maxDur = std::max(0.05, totalBeats - warped - 0.02);
                    CompiledNote note;
                    note.startBeats = b;
                    note.warpedBeats = warped;
                    note.durationBeats = std::min(step * 0.72, maxDur);
                    note.noteNumber = kDrumNotes[static_cast<std::size_t>(row)];
                    note.drumRow = static_cast<std::int8_t>(row);
                    note.velocity = shapeVel(baseVel, prog);
                    note.bendSemitones = 0.0f;
                    note.role = span.role;
                    note.channel = 1;
                    note.isRatchet = (span.role == Role::ratchet || row == 2);
                    pushNote(out, note);
                }
            }
            continue;
        }

        // Mono Bass & MPE Lower Zone synthesis
        if (span.role == Role::ratchet) {
            // Bouncing-ball geometric accelerando / decelerando curves for WarpCut & BBCut11.
            constexpr std::array<double, 6> kAccelSteps{0.5, 0.375, 0.25, 0.1875, 0.125, 0.125};
            constexpr std::array<double, 6> kDecelSteps{0.125, 0.1875, 0.25, 0.25, 0.375, 0.5};
            constexpr std::array<double, 4> kEvenSteps{0.25, 0.25, 0.25, 0.25};
            const bool useBouncing = (grammar == Grammar::warpcut || grammar == Grammar::bbcut11);
            const bool useDecel = useBouncing && ((spanSeed & 1) == 1) && ratchetAmt > 0.5f;

            double cursor = span.startBeats;
            int idx = 0;
            while (cursor < span.endBeats - 0.06) {
                double rawStep = kEvenSteps[static_cast<std::size_t>(idx) % kEvenSteps.size()];
                if (useBouncing) {
                    rawStep = useDecel ? kDecelSteps[static_cast<std::size_t>(idx) % kDecelSteps.size()]
                                       : kAccelSteps[static_cast<std::size_t>(idx) % kAccelSteps.size()];
                } else if (ratchetAmt > 0.72f) {
                    rawStep = 0.1875;
                }
                const double step = std::min(rawStep, span.endBeats - cursor);
                const double warped = warpBeat(cursor, gravity, attractor);
                const double prog = (cursor - span.startBeats) / spanLen;
                const int degBump =
                    evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.45 ? evoRng.integer(1, 3) : 0;
                const int pitch = resolvePitch(idx + 1 + degBump, params, held, (idx % 3) == 2 && mutateAmt > 0.35f);
                const double maxDur = std::max(0.04, totalBeats - warped - 0.02);

                CompiledNote note;
                note.startBeats = cursor;
                note.warpedBeats = warped;
                note.durationBeats = std::min(step * 0.80, maxDur);
                note.noteNumber = static_cast<std::int8_t>(pitch);
                note.velocity = shapeVel(68 + static_cast<int>(54.0 * prog), prog);
                note.bendSemitones = ((idx % 2 == 0) ? 2.0f : -2.5f) * glideAmt;
                note.role = Role::ratchet;
                note.channel = allocChannel();
                note.isRatchet = true;
                pushNote(out, note);
                cursor += step;
                ++idx;
            }
        } else if (span.role == Role::turnaround) {
            const double warped = warpBeat(span.startBeats, gravity, attractor);
            const double maxDur = std::max(0.12, totalBeats - warped - 0.04);
            const int turnDeg = evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.5 ? 4 : 5;
            const int leadPitch = resolvePitch(turnDeg, params, held, mutateAmt > 0.5f);

            CompiledNote dive;
            dive.startBeats = span.startBeats;
            dive.warpedBeats = warped;
            dive.durationBeats = std::min(spanLen * 0.90, maxDur);
            dive.noteNumber = static_cast<std::int8_t>(leadPitch);
            dive.velocity = 122;
            dive.bendSemitones = -12.0f * glideAmt; // Signature neuro pitch-dive turnaround
            dive.role = Role::turnaround;
            dive.channel = allocChannel();
            pushNote(out, dive);

            if (mode == OutputMode::mpe) {
                CompiledNote upper = dive;
                upper.noteNumber = static_cast<std::int8_t>(
                    resolveHarmonyPitch(turnDeg, 4, params, held, mutateAmt > 0.5f)); // Diatonic 5th
                upper.velocity = 94;
                upper.bendSemitones = 5.0f * glideAmt;
                upper.channel = allocChannel();
                pushNote(out, upper);
            }
        } else {
            const int localBars = std::max(1, static_cast<int>(std::ceil(spanLen / 4.0)));
            for (int lb = 0; lb < localBars; ++lb) {
                const double barBase = span.startBeats + static_cast<double>(lb * 4);
                for (int i = 0; i < motif.count; ++i) {
                    const double beat = barBase + motif.offsets[static_cast<std::size_t>(i)];
                    if (beat >= span.endBeats - 0.05)
                        continue;

                    // Keep beat 0 anchored; thin secondary notes smoothly with Density.
                    if (i > 0 && spanRng.uniform() > (0.28 + 0.72 * static_cast<double>(densityAmt)))
                        continue;

                    const double nextBeat =
                        (i + 1 < motif.count)
                            ? std::min(span.endBeats, barBase + motif.offsets[static_cast<std::size_t>(i + 1)])
                            : std::min(span.endBeats, barBase + 4.0);

                    int degree = motifDegrees[static_cast<std::size_t>(i)];
                    bool lift = false;
                    if (span.role == Role::answer && i >= 2) {
                        degree += spanRng.integer(1, 2);
                    } else if (span.role == Role::mutation) {
                        if (spanRng.uniform() < static_cast<double>(mutateAmt))
                            degree += spanRng.integer(1, 3);
                        lift = (i == motif.count - 1) && mutateAmt > 0.45f;
                    }
                    // Controlled phrase-to-phrase evolution (Evolve mode only, scaled by Mutate).
                    if (evolveActive && i > 0 && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.38) {
                        degree += evoRng.integer(-1, 2);
                        if (i == motif.count - 1 && evoRng.uniform() < 0.4)
                            lift = !lift;
                    }

                    const int pitch = resolvePitch(degree, params, held, lift);
                    const double warped = warpBeat(beat, gravity, attractor);
                    const double gap = std::max(0.20, nextBeat - beat);
                    const bool doLegato = (grammar == Grammar::liquid) || (activeContour == Contour::acid && i > 0) ||
                                          (i > 0 && legatoAmt > 0.45f && (i % 2 == 1));
                    const double gateMul = doLegato ? (0.96 + 0.14 * static_cast<double>(legatoAmt)) : 0.80;
                    const double maxDur = std::max(0.06, totalBeats - warped - 0.02);
                    const double prog = (beat - span.startBeats) / spanLen;

                    const bool isAcidAccent = (activeContour == Contour::acid) && (i % 2 == 1);
                    CompiledNote note;
                    note.startBeats = beat;
                    note.warpedBeats = warped;
                    note.durationBeats = std::min(gap * gateMul, maxDur);
                    note.noteNumber = static_cast<std::int8_t>(pitch);
                    note.velocity = shapeVel(i == 0 ? 118 : (isAcidAccent ? 114 : spanRng.integer(76, 108)), prog);
                    note.bendSemitones = 0.0f; // Filled by target-pitch glide pass below
                    note.role = span.role;
                    note.channel = allocChannel();
                    pushNote(out, note);

                    if (mode == OutputMode::mpe && (i == 0 || i == 2)) {
                        // Scale-diatonic 5th (degree + 4) on beat 1, diatonic 3rd (degree + 2) or 7th (+6) on stab.
                        const int harmInterval = (i == 0) ? 4 : 2;
                        CompiledNote harm = note;
                        harm.noteNumber =
                            static_cast<std::int8_t>(resolveHarmonyPitch(degree, harmInterval, params, held, lift));
                        harm.velocity = static_cast<std::uint8_t>(std::max(40, static_cast<int>(note.velocity) - 20));
                        harm.channel = allocChannel();
                        pushNote(out, harm);
                    }
                }
            }
        }
    }

    // 3. Sort strictly by warped onset (allocation-free insertion sort).
    for (int i = 1; i < out.noteCount; ++i) {
        const CompiledNote key = out.notes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && out.notes[static_cast<std::size_t>(j)].warpedBeats > key.warpedBeats) {
            out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
            --j;
        }
        out.notes[static_cast<std::size_t>(j + 1)] = key;
    }

    // 4. Target-pitch glide pass: compute true pitch-bend glides into the next note's exact interval.
    if (mode != OutputMode::drum && glideAmt > 0.05f) {
        for (int i = 0; i < out.noteCount; ++i) {
            auto& n = out.notes[static_cast<std::size_t>(i)];
            if (n.role == Role::turnaround || n.role == Role::ratchet)
                continue;
            // Find the next note strictly later in time.
            const CompiledNote* next = nullptr;
            for (int k = i + 1; k < out.noteCount; ++k) {
                const auto& cand = out.notes[static_cast<std::size_t>(k)];
                if (cand.warpedBeats > n.warpedBeats + 0.04) {
                    next = &cand;
                    break;
                }
            }
            if (next != nullptr) {
                const int interval = static_cast<int>(next->noteNumber) - static_cast<int>(n.noteNumber);
                const bool shouldGlide = (grammar == Grammar::liquid) || (n.durationBeats >= 0.55) ||
                                         (requestedContour == Contour::acid && interval != 0) || ((i % 3) == 2);
                if (shouldGlide && interval != 0 && std::abs(interval) <= 12) {
                    n.bendSemitones = static_cast<float>(interval) * glideAmt;
                } else if (shouldGlide && interval == 0 && glideAmt > 0.45f) {
                    n.bendSemitones = ((i % 2 == 0) ? 1.5f : -1.5f) * glideAmt;
                }
            }
        }
    }

    // 5. Assign deterministic slot indices and apply non-destructive per-note overrides.
    for (int i = 0; i < out.noteCount; ++i) {
        auto& n = out.notes[static_cast<std::size_t>(i)];
        n.slotIndex = static_cast<std::int16_t>(i);
        if (const NoteOverride* ov = overrides.findNote(i)) {
            if (mode != OutputMode::drum && ov->pitchDelta != 0)
                n.noteNumber =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(n.noteNumber) + ov->pitchDelta, 12, 120));
            if (ov->velocityDelta != 0)
                n.velocity =
                    static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.velocity) + ov->velocityDelta, 1, 127));
            if (ov->flipBend)
                n.bendSemitones =
                    std::abs(n.bendSemitones) > 0.25f ? -n.bendSemitones : -12.0f * std::max(0.5f, glideAmt);
            n.muted = ov->muted;
        }
    }
}

} // namespace cl
