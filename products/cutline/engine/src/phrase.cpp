// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/phrase.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

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
        const int key = held.lastPressed > 0 ? static_cast<int>(held.lastPressed)
                                             : static_cast<int>(held.notes[static_cast<std::size_t>(held.count - 1)]);
        return std::clamp(key, 12, 96);
    }
    const int root = std::clamp(params.choice(ParamId::rootNote), 0, 11);
    const int oct = std::clamp(params.choice(ParamId::octave), 0, 3) + 1; // MIDI C1=24, F1=29, F2=41
    return 12 * (oct + 1) + root;
}

int resolvePitch(int degree, const Params& params, const HeldNotes& held, bool octaveLift) noexcept CL_NONBLOCKING {
    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode == InputMode::chordCarve && held.count > 0) {
        const int oct = degree >= 0 ? (degree / held.count) : ((degree - held.count + 1) / held.count);
        const int idx = degree - oct * held.count;
        const int octWrap = (oct + (octaveLift ? 1 : 0)) * 12;
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
        const int step = std::max(1, diatonicInterval - 1);
        const int targetDeg = baseDegree + step;
        const int oct = targetDeg >= 0 ? (targetDeg / held.count) : ((targetDeg - held.count + 1) / held.count);
        const int idx = targetDeg - oct * held.count;
        const int octWrap = (oct + (octaveLift ? 1 : 0)) * 12;
        const int base = static_cast<int>(held.notes[static_cast<std::size_t>(idx)]);
        return std::clamp(base + octWrap, 12, 120);
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
    mix(static_cast<std::uint32_t>(params.choice(ParamId::drumStyle)));

    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
    if (inputMode != InputMode::seed) {
        mix(static_cast<std::uint32_t>(held.count));
        mix(static_cast<std::uint32_t>(held.lastPressed));
        for (int i = 0; i < held.count; ++i)
            mix(held.notes[static_cast<std::size_t>(i)]);
    }

    if (overrides.hasAny()) {
        mix(q(overrides.fillStartFrac));
        mix(q(overrides.turnStartFrac));
        for (std::int8_t c : overrides.barChordOffsets)
            mix(static_cast<std::uint32_t>(static_cast<int>(c) + 16));
        for (const auto& s : overrides.spans) {
            mix((s.locked ? 1U : 0U) | (static_cast<std::uint32_t>(s.lockedSeed) << 4U) |
                (static_cast<std::uint32_t>(s.roleOverride + 2) << 16U) |
                (static_cast<std::uint32_t>(s.octaveShift + 4) << 20U) | (s.invert ? (1U << 24U) : 0U) |
                (static_cast<std::uint32_t>(s.timeScale + 2) << 25U));
        }
        for (const auto& n : overrides.notes) {
            if (!n.active())
                continue;
            mix(static_cast<std::uint32_t>(n.key.spanIdx + 2) | (static_cast<std::uint32_t>(n.key.stepTick + 2) << 6U) |
                (static_cast<std::uint32_t>(n.key.voiceIdx + 2) << 19U) |
                (static_cast<std::uint32_t>(n.slotIndex + 2) << 24U));
            mix(static_cast<std::uint32_t>(n.pitchDelta + 32) |
                (static_cast<std::uint32_t>(n.velocityDelta + 64) << 7U) |
                (static_cast<std::uint32_t>(n.lengthSixteenths + 32) << 15U) |
                (static_cast<std::uint32_t>(n.microNudgeTicks + 32) << 22U));
            mix(static_cast<std::uint32_t>(n.ratchetCount) | (n.muted ? 0x10U : 0U) | (n.flipBend ? 0x20U : 0U));
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

    const auto barChordOf = [&](double beat) noexcept CL_NONBLOCKING -> int {
        const int bar = std::clamp(static_cast<int>(std::floor(beat / 4.0)), 0, PhraseOverrides::kMaxChordBars - 1);
        return static_cast<int>(overrides.barChordOffsets[static_cast<std::size_t>(bar)]);
    };

    const auto makeKey = [](int spanIdx, double startBeat, int voiceIdx) noexcept CL_NONBLOCKING -> NoteKey {
        return {static_cast<std::int8_t>(std::clamp(spanIdx, 0, 31)),
                static_cast<std::int16_t>(std::clamp<long long>(std::llround(startBeat * 24.0), 0LL, 4095LL)),
                static_cast<std::int8_t>(std::clamp(voiceIdx, 0, 15))};
    };

    // Resolve breakbeat kit style (Auto maps directly from active Grammar).
    const auto reqDrumStyle = static_cast<DrumStyle>(std::clamp(params.choice(ParamId::drumStyle), 0, 5));
    const DrumStyle effDrumStyle = [&]() noexcept CL_NONBLOCKING {
        if (reqDrumStyle != DrumStyle::autoStyle)
            return reqDrumStyle;
        switch (grammar) {
        case Grammar::bbcut11:
            return DrumStyle::amen;
        case Grammar::liquid:
            return DrumStyle::think;
        case Grammar::neuro:
            return DrumStyle::neuro;
        case Grammar::halftime:
            return DrumStyle::halftime;
        case Grammar::warpcut:
            return DrumStyle::drill;
        }
        return DrumStyle::neuro;
    }();

    // 16-step polyphonic breakbeat pattern bitmasks: [style 0..4][row 0..5] (bit 0 = step 0 .. bit 15 = step 15).
    // Rows: 0=Kick, 1=Snare, 2=Ghost, 3=ClosedHat, 4=OpenHat, 5=RatchetPerc.
    constexpr auto maskOf = [](std::initializer_list<int> steps) constexpr -> std::uint16_t {
        std::uint16_t m = 0;
        for (int st : steps)
            m = static_cast<std::uint16_t>(m | (1U << static_cast<unsigned>(st)));
        return m;
    };
    const std::array<std::array<std::uint16_t, 6>, 5> kDrumPatterns{{
        // 0: Amen (iconic 1, 1.5, 3.5, 3.75 kick; 2 & 4 snare; 2.75/3.25/4.75 ghosts; 8th ride + open-hat lift)
        {maskOf({0, 2, 10, 11}), maskOf({4, 12}), maskOf({7, 9, 15}), maskOf({0, 2, 4, 6, 8, 10, 12}), maskOf({14}),
         maskOf({})},
        // 1: Think (rolling soulful kick + 16th shaker/hat drive + open-hat accents on 7 & 15)
        {maskOf({0, 10}), maskOf({4, 12}), maskOf({7, 9, 15}), maskOf({0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14}),
         maskOf({7, 15}), maskOf({})},
        // 2: Neuro (hard 2-step kick, razor snare, tight ghost chatter, driving 8th/16th hats)
        {maskOf({0, 10}), maskOf({4, 12}), maskOf({7, 9, 14}), maskOf({0, 2, 3, 4, 6, 8, 10, 11, 12, 14}), maskOf({6}),
         maskOf({})},
        // 3: Halftime (colossal beat-3 snare, heavy 1 & 3.75 kick, spacious syncopated hats)
        {maskOf({0, 11}), maskOf({8}), maskOf({5, 14, 15}), maskOf({0, 2, 3, 4, 6, 8, 10, 12, 13}), maskOf({14}),
         maskOf({})},
        // 4: Drill (chopped IDM / drill-n-bass polyrhythmic kick/snare + frantic perc/hat grid)
        {maskOf({0, 3, 7, 10, 14}), maskOf({4, 11, 13}), maskOf({1, 5, 6, 9, 15}),
         maskOf({0, 1, 3, 4, 6, 7, 8, 9, 11, 13, 14, 15}), maskOf({2, 12}), maskOf({6, 14})},
    }};
    const int drumStyleIdx = std::clamp(static_cast<int>(effDrumStyle) - 1, 0, 4);
    const auto& drumKitMask = kDrumPatterns[static_cast<std::size_t>(drumStyleIdx)];

    // 2. Compile notes across each structural role span.
    for (int s = 0; s < out.spanCount; ++s) {
        const RoleSpan& span = out.spans[static_cast<std::size_t>(s)];
        if (span.role == Role::rest)
            continue;
        const double spanLen = std::max(0.25, span.endBeats - span.startBeats);

        // Locked spans use their captured seed and stay frozen across phrase evolutions.
        int spanSeed = seed;
        bool spanLocked = false;
        int spanOctShift = 0;
        bool spanInvert = false;
        int spanTimeScale = 0;
        if (s < PhraseOverrides::kMaxSpanOverrides) {
            const auto& sov = overrides.spans[static_cast<std::size_t>(s)];
            if (sov.locked && sov.lockedSeed >= 1 && sov.lockedSeed <= 999) {
                spanSeed = static_cast<int>(sov.lockedSeed);
                spanLocked = true;
            }
            spanOctShift = std::clamp(static_cast<int>(sov.octaveShift), -2, 2);
            spanInvert = sov.invert;
            spanTimeScale = std::clamp(static_cast<int>(sov.timeScale), -1, 1);
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
            // True polyphonic 16th-grid Breakbeat Drum Compiler (simultaneous Kick/Snare/Ghost/Hats/Perc).
            const double baseStep = (spanTimeScale < 0) ? 0.5 : (spanTimeScale > 0 ? 0.125 : 0.25);
            const double step = span.role == Role::ratchet ? (ratchetAmt > 0.65f ? 0.125 : 0.1875) : baseStep;

            const auto emitDrum = [&](double b, int row, int baseVel, double prog,
                                      bool ratchetFlag) noexcept CL_NONBLOCKING {
                const double warped = warpBeat(b, gravity, attractor);
                const double maxDur = std::max(0.04, totalBeats - warped - 0.02);
                CompiledNote note;
                note.startBeats = b;
                note.warpedBeats = warped;
                note.durationBeats = std::min(step * (row == 4 ? 1.4 : 0.78), maxDur);
                note.key = makeKey(s, b, row);
                note.noteNumber = kDrumNotes[static_cast<std::size_t>(row)];
                note.drumRow = static_cast<std::int8_t>(row);
                note.velocity = shapeVel(baseVel, prog);
                note.bendSemitones = 0.0f;
                note.role = span.role;
                note.channel = 1;
                note.isRatchet = ratchetFlag;
                pushNote(out, note);
            };

            for (double b = span.startBeats; b < span.endBeats - 1.0e-5; b += step) {
                const double barBeat = b - std::floor(b / 4.0) * 4.0;
                const int step16 = static_cast<int>(std::llround(barBeat * 4.0)) & 15;
                const std::uint16_t bit = static_cast<std::uint16_t>(1U << static_cast<unsigned>(step16));
                const double prog = (b - span.startBeats) / spanLen;

                if (span.role == Role::ratchet) {
                    const int row = spanRng.uniform() < 0.56 ? 5 : (spanRng.uniform() < 0.65 ? 2 : 1);
                    const int vel = 54 + static_cast<int>(68.0 * prog);
                    emitDrum(b, row, vel, prog, true);
                    if ((step16 % 4) == 0 && densityAmt > 0.45f)
                        emitDrum(b, 3, 76, prog, false);
                    continue;
                }

                if (span.role == Role::turnaround) {
                    // Punchy snare-flam + kick pickup turnaround with crash/open hat on final hit.
                    if (prog < 0.40) {
                        emitDrum(b, 1, 116, prog, false);
                        if (densityAmt > 0.35f)
                            emitDrum(b, 4, 96, prog, false);
                    } else if (prog < 0.75) {
                        if (spanRng.uniform() < 0.45 + 0.50 * static_cast<double>(densityAmt))
                            emitDrum(b, 5, 88 + static_cast<int>(26.0 * prog), prog, true);
                    } else {
                        emitDrum(b, 0, 122, prog, false);
                        emitDrum(b, 1, 118, prog, false);
                    }
                    continue;
                }

                // Core polyphonic layers: Kick (0), Snare (1), Ghost (2), Closed Hat (3), Open Hat (4), Perc (5)
                bool kickHit = (drumKitMask[0] & bit) != 0U;
                bool snareHit = (drumKitMask[1] & bit) != 0U;
                bool ghostHit = (drumKitMask[2] & bit) != 0U;
                bool chHit = (drumKitMask[3] & bit) != 0U;
                bool ohHit = (drumKitMask[4] & bit) != 0U;
                bool percHit = (drumKitMask[5] & bit) != 0U;

                // Role-aware musical variation (Answer adds pickup kick, Mutation shifts snare/adds fills)
                if (span.role == Role::answer && step16 == 6 && effDrumStyle == DrumStyle::halftime)
                    kickHit = true;
                if (span.role == Role::mutation) {
                    if (step16 == 15 && mutateAmt > 0.35f && spanRng.uniform() < static_cast<double>(mutateAmt))
                        kickHit = true;
                    if (effDrumStyle == DrumStyle::amen && step16 == 14 && mutateAmt > 0.4f)
                        snareHit = true;
                    if (!percHit && (step16 == 5 || step16 == 13) &&
                        spanRng.uniform() < static_cast<double>(mutateAmt) * 0.45)
                        percHit = true;
                }
                if (evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.25) {
                    if (!ghostHit && (step16 % 2 == 1))
                        ghostHit = true;
                }

                // Thin hats/ghosts smoothly with Density while keeping core Kick/Snare anchor solid
                const double dens = static_cast<double>(densityAmt);
                if (ghostHit && dens < 0.85 && spanRng.uniform() > (0.15 + 0.95 * dens))
                    ghostHit = false;
                if (chHit && (step16 % 2 == 1) && spanRng.uniform() > (0.10 + 0.90 * dens))
                    chHit = false;
                if (chHit && (step16 % 2 == 0) && dens < 0.28 && spanRng.uniform() > dens * 3.0)
                    chHit = false;
                if (ohHit && dens < 0.22)
                    ohHit = false;
                if (ohHit)
                    chHit = false; // Open hat replaces closed hat on the same step

                if (kickHit)
                    emitDrum(b, 0, step16 == 0 ? 124 : 114, prog, false);
                if (snareHit)
                    emitDrum(b, 1, 118, prog, false);
                if (ghostHit)
                    emitDrum(b, 2, spanRng.integer(48, 74), prog, true);
                if (chHit)
                    emitDrum(b, 3, (step16 % 2 == 0) ? spanRng.integer(84, 102) : spanRng.integer(58, 78), prog, false);
                if (ohHit)
                    emitDrum(b, 4, spanRng.integer(88, 108), prog, false);
                if (percHit)
                    emitDrum(b, 5, spanRng.integer(76, 104), prog, true);
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
            const double timeMul = (spanTimeScale < 0) ? 2.0 : (spanTimeScale > 0 ? 0.5 : 1.0);

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
                const double step = std::min(std::max(0.08, rawStep * timeMul), span.endBeats - cursor);
                const double warped = warpBeat(cursor, gravity, attractor);
                const double prog = (cursor - span.startBeats) / spanLen;
                const int degBump =
                    evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.45 ? evoRng.integer(1, 3) : 0;
                int rawDeg = idx + 1 + degBump;
                if (spanInvert)
                    rawDeg = -rawDeg;
                rawDeg += barChordOf(cursor);
                const int pitch = std::clamp(resolvePitch(rawDeg, params, held, (idx % 3) == 2 && mutateAmt > 0.35f) +
                                                 spanOctShift * 12,
                                             12, 120);
                const double maxDur = std::max(0.04, totalBeats - warped - 0.02);

                CompiledNote note;
                note.startBeats = cursor;
                note.warpedBeats = warped;
                note.durationBeats = std::min(step * 0.80, maxDur);
                note.key = makeKey(s, cursor, 0);
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
            int turnDeg = evolveActive && evoRng.uniform() < static_cast<double>(mutateAmt) * 0.5 ? 4 : 5;
            if (spanInvert)
                turnDeg = -turnDeg;
            turnDeg += barChordOf(span.startBeats);
            const int leadPitch =
                std::clamp(resolvePitch(turnDeg, params, held, mutateAmt > 0.5f) + spanOctShift * 12, 12, 120);

            CompiledNote dive;
            dive.startBeats = span.startBeats;
            dive.warpedBeats = warped;
            dive.durationBeats = std::min(spanLen * 0.90, maxDur);
            dive.key = makeKey(s, span.startBeats, 0);
            dive.noteNumber = static_cast<std::int8_t>(leadPitch);
            dive.velocity = 122;
            dive.bendSemitones = -12.0f * glideAmt; // Signature neuro pitch-dive turnaround
            dive.role = Role::turnaround;
            dive.channel = allocChannel();
            pushNote(out, dive);

            if (mode == OutputMode::mpe) {
                CompiledNote upper = dive;
                upper.key = makeKey(s, span.startBeats, 1);
                upper.noteNumber = static_cast<std::int8_t>(std::clamp(
                    resolveHarmonyPitch(turnDeg, 4, params, held, mutateAmt > 0.5f) + spanOctShift * 12, 12, 120));
                upper.velocity = 94;
                upper.bendSemitones = 5.0f * glideAmt;
                upper.channel = allocChannel();
                pushNote(out, upper);
            }
        } else {
            const double timeMul = (spanTimeScale < 0) ? 2.0 : (spanTimeScale > 0 ? 0.5 : 1.0);
            const double cellPeriod = 4.0 * timeMul;
            const int localCycles = std::max(1, static_cast<int>(std::ceil(spanLen / cellPeriod)));
            for (int lb = 0; lb < localCycles; ++lb) {
                const double barBase = span.startBeats + static_cast<double>(lb) * cellPeriod;
                for (int i = 0; i < motif.count; ++i) {
                    const double beat = barBase + motif.offsets[static_cast<std::size_t>(i)] * timeMul;
                    if (beat >= span.endBeats - 0.05)
                        continue;

                    // Keep beat 0 anchored; thin secondary notes smoothly with Density.
                    if (i > 0 && spanRng.uniform() > (0.28 + 0.72 * static_cast<double>(densityAmt)))
                        continue;

                    const double nextBeat =
                        (i + 1 < motif.count)
                            ? std::min(span.endBeats,
                                       barBase + motif.offsets[static_cast<std::size_t>(i + 1)] * timeMul)
                            : std::min(span.endBeats, barBase + cellPeriod);

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
                    if (spanInvert)
                        degree = -degree;
                    degree += barChordOf(beat);

                    const int pitch = std::clamp(resolvePitch(degree, params, held, lift) + spanOctShift * 12, 12, 120);
                    const double warped = warpBeat(beat, gravity, attractor);
                    const double gap = std::max(0.12, nextBeat - beat);
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
                    note.key = makeKey(s, beat, 0);
                    note.noteNumber = static_cast<std::int8_t>(pitch);
                    note.velocity = shapeVel(i == 0 ? 118 : (isAcidAccent ? 114 : spanRng.integer(76, 108)), prog);
                    note.bendSemitones = 0.0f; // Filled by target-pitch glide pass below
                    note.role = span.role;
                    note.channel = allocChannel();
                    pushNote(out, note);

                    if (mode == OutputMode::mpe && (i == 0 || i == 2)) {
                        // Scale-diatonic 5th (degree + 4) on beat 1, diatonic 3rd (degree + 2) on stab.
                        const int harmInterval = (i == 0) ? 4 : 2;
                        CompiledNote harm = note;
                        harm.key = makeKey(s, beat, 1);
                        harm.noteNumber = static_cast<std::int8_t>(std::clamp(
                            resolveHarmonyPitch(degree, harmInterval, params, held, lift) + spanOctShift * 12, 12,
                            120));
                        harm.velocity = static_cast<std::uint8_t>(std::max(40, static_cast<int>(note.velocity) - 20));
                        harm.channel = allocChannel();
                        pushNote(out, harm);
                    }
                }
            }
        }
    }

    // 3. Sort strictly by warped onset (allocation-free insertion sort).
    const auto sortNotes = [&]() noexcept CL_NONBLOCKING {
        for (int i = 1; i < out.noteCount; ++i) {
            const CompiledNote key = out.notes[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && out.notes[static_cast<std::size_t>(j)].warpedBeats > key.warpedBeats) {
                out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
                --j;
            }
            out.notes[static_cast<std::size_t>(j + 1)] = key;
        }
    };
    sortNotes();

    // 4. Target-pitch glide pass: compute true pitch-bend glides into the next note's exact interval.
    if (mode != OutputMode::drum && glideAmt > 0.05f) {
        for (int i = 0; i < out.noteCount; ++i) {
            auto& n = out.notes[static_cast<std::size_t>(i)];
            if (n.role == Role::turnaround || n.role == Role::ratchet)
                continue;
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

    // 5. Assign deterministic slot indices and apply musical-coordinate per-note overrides
    //    (pitch, velocity, length, micro-nudge, mute, bend flip, and per-step ratchet bursts).
    const int baseNoteCount = out.noteCount;
    bool needsResort = false;
    for (int i = 0; i < baseNoteCount; ++i) {
        auto& n = out.notes[static_cast<std::size_t>(i)];
        n.slotIndex = static_cast<std::int16_t>(i);
        if (const NoteOverride* ov = overrides.findNoteFor(n.key, i)) {
            if (mode != OutputMode::drum && ov->pitchDelta != 0)
                n.noteNumber =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(n.noteNumber) + ov->pitchDelta, 12, 120));
            if (ov->velocityDelta != 0)
                n.velocity =
                    static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.velocity) + ov->velocityDelta, 1, 127));
            if (ov->microNudgeTicks != 0) {
                n.warpedBeats =
                    std::clamp(n.warpedBeats + static_cast<double>(ov->microNudgeTicks) / 96.0, 0.0, totalBeats - 0.04);
                needsResort = true;
            }
            if (ov->lengthSixteenths != 0) {
                const double maxDur = std::max(0.05, totalBeats - n.warpedBeats - 0.02);
                n.durationBeats =
                    std::clamp(n.durationBeats + static_cast<double>(ov->lengthSixteenths) * 0.25, 0.05, maxDur);
            }
            if (ov->flipBend)
                n.bendSemitones =
                    std::abs(n.bendSemitones) > 0.25f ? -n.bendSemitones : -12.0f * std::max(0.5f, glideAmt);
            n.muted = ov->muted;
            if (!n.muted &&
                (ov->ratchetCount == 2 || ov->ratchetCount == 3 || ov->ratchetCount == 4 || ov->ratchetCount == 6)) {
                const int rc = static_cast<int>(ov->ratchetCount);
                const double totalDur = std::max(0.12, n.durationBeats);
                const double subStep = totalDur / static_cast<double>(rc);
                const CompiledNote parent = n;
                n.durationBeats = std::max(0.03, subStep * 0.80);
                n.ratchetCount = ov->ratchetCount;
                n.isRatchet = true;
                n.velocity = static_cast<std::uint8_t>(std::clamp(static_cast<int>(parent.velocity) - 18, 24, 127));
                for (int r = 1; r < rc; ++r) {
                    const double rStart = parent.warpedBeats + static_cast<double>(r) * subStep;
                    if (rStart >= totalBeats - 0.03)
                        break;
                    CompiledNote sub = parent;
                    sub.startBeats = parent.startBeats + static_cast<double>(r) * subStep;
                    sub.warpedBeats = rStart;
                    sub.durationBeats = std::min(std::max(0.03, subStep * 0.80), totalBeats - rStart - 0.01);
                    sub.key = {}; // Sub-hits inherit edits from their parent note
                    sub.slotIndex = -1;
                    const double ramp = static_cast<double>(r) / static_cast<double>(rc - 1);
                    sub.velocity = static_cast<std::uint8_t>(
                        std::clamp(static_cast<int>(parent.velocity) - 18 + static_cast<int>(24.0 * ramp), 24, 127));
                    sub.channel = allocChannel();
                    sub.ratchetCount = 0;
                    sub.isRatchet = true;
                    pushNote(out, sub);
                }
                needsResort = true;
            }
        }
    }
    if (needsResort)
        sortNotes();
}

} // namespace cl
