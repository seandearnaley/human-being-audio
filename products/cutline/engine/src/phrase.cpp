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

int resolvePitch(int degree, const Params& params, const HeldNotes& held, bool octaveLift) noexcept CL_NONBLOCKING {
    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 1));
    if (inputMode == InputMode::liveMidi && held.count > 0) {
        const int idx = ((degree % held.count) + held.count) % held.count;
        const int base = static_cast<int>(held.notes[static_cast<std::size_t>(idx)]);
        return std::clamp(base + (octaveLift ? 12 : 0), 12, 120);
    }
    const int root = std::clamp(params.choice(ParamId::rootNote), 0, 11);
    const int oct = std::clamp(params.choice(ParamId::octave), 0, 3) + 1; // MIDI C1=24, F1=29, F2=41
    const int baseMidi = 12 * (oct + 1) + root;
    const auto scaleIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::scale), 0, 5));
    const ScaleDef& sc = kScales[scaleIdx];
    const int wrapped = ((degree % sc.count) + sc.count) % sc.count;
    const int octShift = (degree >= sc.count ? 12 : 0) + (octaveLift ? 12 : 0);
    return std::clamp(baseMidi + sc.intervals[static_cast<std::size_t>(wrapped)] + octShift, 12, 120);
}

void pushNote(PhrasePlan& out, const CompiledNote& note) noexcept CL_NONBLOCKING {
    if (out.noteCount >= kMaxPhraseNotes)
        return;
    out.notes[static_cast<std::size_t>(out.noteCount++)] = note;
}

void pushSpan(PhrasePlan& out, double startBeats, double endBeats, Role role) noexcept CL_NONBLOCKING {
    if (out.spanCount >= kMaxRoleSpans)
        return;
    out.spans[static_cast<std::size_t>(out.spanCount++)] = {startBeats, endBeats, role};
}

} // namespace

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

std::uint32_t phraseSignature(const Params& params, std::int64_t phraseIndex,
                              const HeldNotes& held) noexcept CL_NONBLOCKING {
    std::uint32_t h = hba::dsp::laneHash(static_cast<std::uint32_t>(phraseIndex ^ 0x517cc1b7LL));
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

    if (static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 1)) == InputMode::liveMidi) {
        mix(static_cast<std::uint32_t>(held.count));
        for (int i = 0; i < held.count; ++i)
            mix(held.notes[static_cast<std::size_t>(i)]);
    }
    return h;
}

void compilePhrase(const Params& params, std::int64_t phraseIndex, const HeldNotes& held,
                   PhrasePlan& out) noexcept CL_NONBLOCKING {
    out.phraseIndex = phraseIndex;
    out.noteCount = 0;
    out.spanCount = 0;

    const auto barsIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::phraseBars), 0, 2));
    const int bars = kPhraseBarCounts[barsIdx];
    const double totalBeats = static_cast<double>(bars * 4);
    out.phraseBeats = totalBeats;
    out.signature = phraseSignature(params, phraseIndex, held);

    const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 1));
    if (inputMode == InputMode::liveMidi && held.count <= 0) {
        // Live MIDI mode stays silent when no host keys are held.
        pushSpan(out, 0.0, totalBeats, Role::statement);
        return;
    }

    const auto grammar = static_cast<Grammar>(std::clamp(params.choice(ParamId::grammar), 0, 4));
    const auto mode = static_cast<OutputMode>(std::clamp(params.choice(ParamId::outputMode), 0, 2));
    const auto attractor = static_cast<Attractor>(std::clamp(params.choice(ParamId::attractor), 0, 3));
    const double gravity = static_cast<double>(std::clamp(params[ParamId::gravity], -1.0f, 1.0f));
    const float ratchetAmt = std::clamp(params[ParamId::ratchet], 0.0f, 1.0f);
    const float glideAmt = std::clamp(params[ParamId::glide], 0.0f, 1.0f);
    const float densityAmt = std::clamp(params[ParamId::density], 0.0f, 1.0f);
    const float mutateAmt = std::clamp(params[ParamId::mutate], 0.0f, 1.0f);
    const float velArcAmt = std::clamp(params[ParamId::velArc], 0.0f, 1.0f);
    const float legatoAmt = std::clamp(params[ParamId::legato], 0.0f, 1.0f);
    const int seed = std::clamp(params.integer(ParamId::seed), 1, 999);

    const std::uint32_t phraseSeedVal = hba::dsp::laneHash(static_cast<std::uint32_t>(seed) * 2654435761U ^
                                                           static_cast<std::uint32_t>(phraseIndex * 2246822519LL) ^
                                                           (static_cast<std::uint32_t>(grammar) << 24));
    Rng rng(phraseSeedVal);

    // 1. Partition phrase into structural BBCut-inspired roles.
    if (bars == 2) {
        const double ratStart = ratchetAmt > 0.15f ? 6.0 : 6.75;
        pushSpan(out, 0.0, 3.5, Role::statement);
        pushSpan(out, 3.5, ratStart, Role::answer);
        if (ratchetAmt > 0.15f)
            pushSpan(out, ratStart, 7.25, Role::ratchet);
        pushSpan(out, 7.25, 8.0, Role::turnaround);
    } else if (bars == 4) {
        const double ratStart = 12.0 + (1.0 - static_cast<double>(ratchetAmt)) * 1.5;
        pushSpan(out, 0.0, 4.0, Role::statement);
        pushSpan(out, 4.0, 8.0, Role::answer);
        if (ratchetAmt > 0.10f && ratStart < 14.5) {
            pushSpan(out, 8.0, ratStart, Role::mutation);
            pushSpan(out, ratStart, 14.5, Role::ratchet);
        } else {
            pushSpan(out, 8.0, 14.5, Role::mutation);
        }
        pushSpan(out, 14.5, 16.0, Role::turnaround);
    } else {
        pushSpan(out, 0.0, 8.0, Role::statement);
        pushSpan(out, 8.0, 16.0, Role::answer);
        pushSpan(out, 16.0, 24.0, Role::mutation);
        if (ratchetAmt > 0.10f)
            pushSpan(out, 24.0, 29.0, Role::ratchet);
        else
            pushSpan(out, 24.0, 29.0, Role::mutation);
        pushSpan(out, 29.0, 32.0, Role::turnaround);
    }

    // 2. Motif rhythmic skeleton per grammar.
    std::array<double, 8> motifOffsets{};
    int motifCount = 0;
    switch (grammar) {
    case Grammar::neuro:
        motifOffsets = {0.0, 0.75, 1.5, 2.25, 2.75, 3.5, 0.0, 0.0};
        motifCount = 6;
        break;
    case Grammar::bbcut11:
        motifOffsets = {0.0, 0.75, 1.5, 2.5, 3.25, 3.75, 0.0, 0.0};
        motifCount = 6;
        break;
    case Grammar::warpcut:
        motifOffsets = {0.0, 0.5, 1.25, 1.75, 2.5, 3.0, 3.5, 0.0};
        motifCount = 7;
        break;
    case Grammar::halftime:
        motifOffsets = {0.0, 1.5, 2.75, 3.5, 0.0, 0.0, 0.0, 0.0};
        motifCount = 4;
        break;
    case Grammar::liquid:
        motifOffsets = {0.0, 1.25, 2.5, 3.5, 0.0, 0.0, 0.0, 0.0};
        motifCount = 4;
        break;
    }

    // Deterministic pitch degrees for the statement motif (reused & mutated in answer/mutation).
    std::array<int, 8> motifDegrees{};
    for (int i = 0; i < motifCount; ++i)
        motifDegrees[static_cast<std::size_t>(i)] = i == 0 ? 0 : rng.integer(0, 5);

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

    // 3. Compile notes across each structural role span.
    for (int s = 0; s < out.spanCount; ++s) {
        const RoleSpan& span = out.spans[static_cast<std::size_t>(s)];
        const double spanLen = std::max(0.25, span.endBeats - span.startBeats);

        if (mode == OutputMode::drum) {
            const double step = span.role == Role::ratchet ? (ratchetAmt > 0.65f ? 0.1875 : 0.25) : 0.5;
            for (double b = span.startBeats; b < span.endBeats - 1.0e-5; b += step) {
                const double barBeat = b - std::floor(b / 4.0) * 4.0;
                const double prog = (b - span.startBeats) / spanLen;
                int row = -1;
                int baseVel = 92;

                if (span.role == Role::ratchet) {
                    row = rng.uniform() < 0.62 ? 5 : 2;
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
                    if (rng.uniform() < prob) {
                        const double pick = rng.uniform();
                        row = pick < 0.44 ? 2 : (pick < 0.84 ? 3 : 4);
                        baseVel = row == 2 ? rng.integer(46, 74) : rng.integer(68, 98);
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
            constexpr std::array<double, 6> kWarpSteps{0.5, 0.375, 0.25, 0.25, 0.1875, 0.1875};
            constexpr std::array<double, 4> kEvenSteps{0.25, 0.25, 0.25, 0.25};
            double cursor = span.startBeats;
            int idx = 0;
            while (cursor < span.endBeats - 0.06) {
                const double rawStep = (grammar == Grammar::warpcut)
                                           ? kWarpSteps[static_cast<std::size_t>(idx) % kWarpSteps.size()]
                                           : kEvenSteps[static_cast<std::size_t>(idx) % kEvenSteps.size()];
                const double step = std::min(rawStep, span.endBeats - cursor);
                const double warped = warpBeat(cursor, gravity, attractor);
                const double prog = (cursor - span.startBeats) / spanLen;
                const int pitch = resolvePitch(idx + 2, params, held, (idx % 3) == 2 && mutateAmt > 0.35f);
                const double maxDur = std::max(0.04, totalBeats - warped - 0.02);

                CompiledNote note;
                note.startBeats = cursor;
                note.warpedBeats = warped;
                note.durationBeats = std::min(step * 0.80, maxDur);
                note.noteNumber = static_cast<std::int8_t>(pitch);
                note.velocity = shapeVel(72 + static_cast<int>(50.0 * prog), prog);
                note.bendSemitones = ((idx % 2 == 0) ? 2.0f : -3.0f) * glideAmt;
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
            const int leadPitch = resolvePitch(5, params, held, mutateAmt > 0.5f);

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
                upper.noteNumber = static_cast<std::int8_t>(std::clamp(leadPitch + 7, 12, 120));
                upper.velocity = 94;
                upper.bendSemitones = 5.0f * glideAmt;
                upper.channel = allocChannel();
                pushNote(out, upper);
            }
        } else {
            const int localBars = std::max(1, static_cast<int>(std::ceil(spanLen / 4.0)));
            for (int lb = 0; lb < localBars; ++lb) {
                const double barBase = span.startBeats + static_cast<double>(lb * 4);
                for (int i = 0; i < motifCount; ++i) {
                    const double beat = barBase + motifOffsets[static_cast<std::size_t>(i)];
                    if (beat >= span.endBeats - 0.05)
                        continue;

                    // Keep beat 0 anchored; thin secondary notes smoothly with Density.
                    if (i > 0 && rng.uniform() > (0.28 + 0.72 * static_cast<double>(densityAmt)))
                        continue;

                    const double nextBeat =
                        (i + 1 < motifCount)
                            ? std::min(span.endBeats, barBase + motifOffsets[static_cast<std::size_t>(i + 1)])
                            : std::min(span.endBeats, barBase + 4.0);

                    int degree = motifDegrees[static_cast<std::size_t>(i)];
                    bool lift = false;
                    if (span.role == Role::answer && i >= 2) {
                        degree += rng.integer(1, 2);
                    } else if (span.role == Role::mutation) {
                        if (rng.uniform() < static_cast<double>(mutateAmt))
                            degree += rng.integer(1, 3);
                        lift = (i == motifCount - 1) && mutateAmt > 0.45f;
                    }

                    const int pitch = resolvePitch(degree, params, held, lift);
                    const double warped = warpBeat(beat, gravity, attractor);
                    const double gap = std::max(0.20, nextBeat - beat);
                    const bool doLegato = (grammar == Grammar::liquid) || (i > 0 && legatoAmt > 0.45f && (i % 2 == 1));
                    const double gateMul = doLegato ? (0.96 + 0.14 * static_cast<double>(legatoAmt)) : 0.80;
                    const double maxDur = std::max(0.06, totalBeats - warped - 0.02);
                    const double prog = (beat - span.startBeats) / spanLen;

                    float bend = 0.0f;
                    if (glideAmt > 0.05f &&
                        (i == motifCount - 1 || grammar == Grammar::liquid || rng.uniform() < 0.35)) {
                        bend = (rng.uniform() < 0.5 ? 2.5f : -2.0f) * glideAmt;
                    }

                    CompiledNote note;
                    note.startBeats = beat;
                    note.warpedBeats = warped;
                    note.durationBeats = std::min(gap * gateMul, maxDur);
                    note.noteNumber = static_cast<std::int8_t>(pitch);
                    note.velocity = shapeVel(i == 0 ? 118 : rng.integer(76, 108), prog);
                    note.bendSemitones = bend;
                    note.role = span.role;
                    note.channel = allocChannel();
                    pushNote(out, note);

                    if (mode == OutputMode::mpe && (i == 0 || i == 2)) {
                        CompiledNote harm = note;
                        harm.noteNumber = static_cast<std::int8_t>(std::clamp(pitch + (i == 0 ? 7 : 10), 12, 120));
                        harm.velocity = static_cast<std::uint8_t>(std::max(40, static_cast<int>(note.velocity) - 20));
                        harm.bendSemitones = -bend * 0.65f;
                        harm.channel = allocChannel();
                        pushNote(out, harm);
                    }
                }
            }
        }
    }

    // Sort strictly by warped onset (allocation-free insertion sort).
    for (int i = 1; i < out.noteCount; ++i) {
        const CompiledNote key = out.notes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && out.notes[static_cast<std::size_t>(j)].warpedBeats > key.warpedBeats) {
            out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
            --j;
        }
        out.notes[static_cast<std::size_t>(j + 1)] = key;
    }
}

} // namespace cl
