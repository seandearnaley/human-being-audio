// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/chord_matrix.h"

#include <hba/dsp/control.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace tg {

namespace {
// Pure Just Intonation intervals (in semitones from root) for 5-limit + harmonic 7th
// 1:1, 16:15, 9:8, 6:5, 5:4, 4:3, 7:5, 3:2, 8:5, 5:3, 7:4, 15:8, 2:1
constexpr std::array<double, 13> kJiSemitones{
    0.0,
    1.1173128527,  // 16/15
    2.0391000173,  // 9/8
    3.1564128700,  // 6/5
    3.8631371386,  // 5/4
    4.9804499913,  // 4/3
    5.8251219262,  // 7/5
    7.0195500087,  // 3/2
    8.1368628614,  // 8/5
    8.8435871300,  // 5/3
    9.6882590647,  // 7/4
    10.8826871473, // 15/8
    12.0,          // 2/1
};

// Apply periodic smooth potential well on a sorted list of pitch anchors spanning midi.
double applyAnchorWell(double midi, const double* anchors, int count, double strength) noexcept HBA_NONBLOCKING {
    if (count < 2 || strength <= 1e-4)
        return midi;
    double lo = anchors[0];
    double hi = anchors[count - 1];
    for (int i = 0; i < count - 1; ++i) {
        if (midi >= anchors[i] && midi <= anchors[i + 1]) {
            lo = anchors[i];
            hi = anchors[i + 1];
            break;
        }
    }
    const double span = std::max(hi - lo, 1e-4);
    const double u = std::clamp((midi - lo) / span, 0.0, 1.0);
    // Monotonic C^infinity potential well: derivative = 1 - g*cos(2*pi*u) >= 0.08 > 0 for g <= 0.92.
    const double g = std::clamp(strength, 0.0, 1.0) * 0.90;
    const double warped = u - (g / hba::dsp::kTwoPi) * std::sin(hba::dsp::kTwoPi * u);
    return lo + warped * span;
}

double applyScaleWells(double midi, int rootPc, int scaleIndex, double strength) noexcept HBA_NONBLOCKING {
    const std::uint16_t mask = scaleMask(scaleIndex);
    const int baseOct = static_cast<int>(std::floor((midi - static_cast<double>(rootPc)) / 12.0));
    std::array<double, 36> anchors{};
    int n = 0;
    for (int oct = baseOct - 1; oct <= baseOct + 1; ++oct) {
        for (int deg = 0; deg < 12; ++deg) {
            if (((mask >> deg) & 1u) != 0u && n < static_cast<int>(anchors.size())) {
                anchors[static_cast<std::size_t>(n++)] = static_cast<double>(rootPc + oct * 12 + deg);
            }
        }
    }
    return applyAnchorWell(midi, anchors.data(), n, strength);
}

double applyJiWells(double midi, double anchorMidi, double strength) noexcept HBA_NONBLOCKING {
    const double rel = midi - anchorMidi;
    const int baseOct = static_cast<int>(std::floor(rel / 12.0));
    std::array<double, 39> anchors{};
    int n = 0;
    for (int oct = baseOct - 1; oct <= baseOct + 1; ++oct) {
        for (int i = 0; i < 12; ++i) {
            anchors[static_cast<std::size_t>(n++)] =
                anchorMidi + static_cast<double>(oct * 12) + kJiSemitones[static_cast<std::size_t>(i)];
        }
    }
    return applyAnchorWell(midi, anchors.data(), n, strength);
}

// Compute next lexicographical permutation for a small fixed array (noexcept, zero alloc).
bool nextPerm5(std::array<int, kVoiceCount>& p, int start) noexcept HBA_NONBLOCKING {
    int i = kVoiceCount - 2;
    while (i >= start && p[static_cast<std::size_t>(i)] >= p[static_cast<std::size_t>(i + 1)])
        --i;
    if (i < start)
        return false;
    int j = kVoiceCount - 1;
    while (p[static_cast<std::size_t>(j)] <= p[static_cast<std::size_t>(i)])
        --j;
    std::swap(p[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(j)]);
    int l = i + 1;
    int r = kVoiceCount - 1;
    while (l < r) {
        std::swap(p[static_cast<std::size_t>(l)], p[static_cast<std::size_t>(r)]);
        ++l;
        --r;
    }
    return true;
}
} // namespace

const std::array<VoicingPackInfo, kVoicingPackCount>& voicingPacks() {
    static const std::array<VoicingPackInfo, kVoicingPackCount> kPacks = {{
        // 0: Phrygian Tension -> Unison Sub Collapse
        {
            "Phrygian Tension",
            {"i Phryg b9", "bVI Lyd #11", "bVII Quartal", "i Sub Collapse"},
            {{{0, 7, 13, 15, 22}, {-4, 3, 14, 19, 24}, {-2, 5, 12, 15, 20}, {0, 0, 12, 19, 24}}},
            false,
        },
        // 1: Unison Seed -> Min9 -> Maj7#11 Bloom
        {
            "Unison Bloom",
            {"Unison Seed", "i Min9 Bloom", "bVI Maj7#11", "iv Phryg Sus"},
            {{{0, 12, 12, 12, 12}, {0, 7, 14, 15, 22}, {-4, 3, 12, 14, 19}, {-7, 0, 6, 10, 15}}},
            false,
        },
        // 2: Neo-Riemannian PLR Cinema (Chromatic Mediants)
        {
            "Neo-Riemann PLR",
            {"i (add9)", "bVI Maj7#11", "bIII m(maj7)", "i Open 5th"},
            {{{0, 7, 14, 15, 22}, {-4, 3, 12, 14, 19}, {-8, -1, 7, 15, 18}, {0, 7, 12, 19, 24}}},
            false,
        },
        // 3: Neuro Cluster Collapse
        {
            "Neuro Cluster",
            {"b9 Cluster", "bII Sus4", "Tritone Split", "Octave Reese"},
            {{{0, 1, 7, 13, 18}, {1, 6, 8, 13, 20}, {0, 6, 12, 18, 24}, {0, 0, 12, 12, 24}}},
            false,
        },
        // 4: Dark Pedal-Steel Copedent Pulls
        {
            "Slide Steel Dark",
            {"i Pedal 6/9", "iv9 Pull", "bVII13 Glide", "i (add9)"},
            {{{0, 7, 9, 14, 15}, {0, 5, 8, 14, 17}, {-2, 5, 9, 12, 17}, {0, 7, 12, 14, 15}}},
            false,
        },
        // 5: Quartal Drift Pad
        {
            "Quartal Drift",
            {"Quartal i", "Quartal bII", "Quartal bVI", "Unison Lock"},
            {{{0, 5, 10, 15, 20}, {1, 6, 11, 16, 21}, {-4, 1, 6, 11, 16}, {0, 7, 12, 17, 24}}},
            false,
        },
        // 6: Mobius Dim7 Axis (0 -> +3 -> +6 -> +9 -> +12 seamless octave torus!)
        {
            "Mobius Dim7",
            {"i Min7 (0)", "biii Min7 (+3)", "#iv Min7 (+6)", "vi Min7 (+9)"},
            {{{0, 7, 10, 15, 19}, {3, 10, 13, 18, 22}, {6, 13, 16, 21, 25}, {9, 16, 19, 24, 28}}},
            true,
        },
        // 7: Mobius Whole-Tone Augmented Spiral (0 -> +3 -> +6 -> +9 on augmented/tritone axis)
        {
            "Mobius Whole-Tone",
            {"Aug 0", "Aug +3", "Aug +6", "Aug +9"},
            {{{0, 4, 8, 14, 18}, {3, 7, 11, 17, 21}, {6, 10, 14, 20, 24}, {9, 13, 17, 23, 27}}},
            true,
        },
    }};
    return kPacks;
}

SceneMatrix matrixForPack(int packIndex) noexcept HBA_NONBLOCKING {
    const int clamped = std::clamp(packIndex, 0, kVoicingPackCount - 1);
    SceneMatrix m;
    m.offsets = voicingPacks()[static_cast<std::size_t>(clamped)].offsets;
    m.isCustom = false;
    return m;
}

SpiralChord spiralChordForStack(int stackIndex) noexcept HBA_NONBLOCKING {
    switch (std::clamp(stackIndex, 0, 5)) {
    case 0:
        return {{0.0, 0.0, 0.0, 0.0, 0.0}, 1}; // Single Bass
    case 1:
        return {{0.0, 7.0, 0.0, 0.0, 0.0}, 2}; // Power Fifth
    case 2:
        return {{0.0, 7.0, 13.0, 0.0, 0.0}, 3}; // Phrygian b9
    case 3:
        return {{0.0, 6.0, 12.0, 0.0, 0.0}, 3}; // Tritone Dyad
    case 4:
        return {{0.0, 5.0, 10.0, 15.0, 0.0}, 4}; // Quartal Dark
    case 5:
        return {{0.0, 3.0, 7.0, 10.0, 14.0}, 5}; // Minor 9th
    }
    return {{0.0, 0.0, 0.0, 0.0, 0.0}, 1};
}

std::uint16_t scaleMask(int scaleIndex) noexcept HBA_NONBLOCKING {
    constexpr auto maskOf = [](std::initializer_list<int> pcs) constexpr {
        std::uint16_t m = 0;
        for (int pc : pcs)
            m = static_cast<std::uint16_t>(m | (1u << (pc % 12)));
        return m;
    };
    switch (std::clamp(scaleIndex, 0, kScaleCount - 1)) {
    case 0:
        return maskOf({0, 1, 3, 5, 7, 8, 10}); // Phrygian
    case 1:
        return maskOf({0, 2, 3, 5, 7, 8, 10}); // Aeolian
    case 2:
        return maskOf({0, 2, 3, 5, 7, 8, 11}); // Harmonic Minor
    case 3:
        return maskOf({0, 2, 3, 5, 7, 9, 10}); // Dorian
    case 4:
        return maskOf({0, 2, 4, 6, 7, 9, 11}); // Lydian #11
    case 5:
        return maskOf({0, 2, 4, 6, 8, 10}); // Whole Tone
    case 6:
        return maskOf({0, 1, 3, 4, 6, 7, 9, 10}); // Half-Whole Diminished
    case 7:
        return maskOf({0, 2, 3, 7, 8}); // Hirajoshi
    case 8:
        return 0x0FFFu; // Chromatic
    }
    return 0x0FFFu;
}

void optimizeVoiceLeading(SceneMatrix& matrix, bool keepBass) noexcept HBA_NONBLOCKING {
    // Sort Scene 0 low-to-high so voices start in clean register order.
    std::sort(matrix.offsets[0].begin(), matrix.offsets[0].end());

    const int startIdx = keepBass ? 1 : 0;
    for (int s = 1; s < kSceneCount; ++s) {
        const auto& prev = matrix.offsets[static_cast<std::size_t>(s - 1)];
        auto& curr = matrix.offsets[static_cast<std::size_t>(s)];
        const auto orig = curr;

        std::array<int, kVoiceCount> perm{0, 1, 2, 3, 4};
        std::array<int, kVoiceCount> bestPerm = perm;
        float bestCost = 1.0e9f;

        do {
            float cost = 0.0f;
            for (int v = 0; v < kVoiceCount; ++v) {
                const float d = orig[static_cast<std::size_t>(perm[static_cast<std::size_t>(v)])] -
                                prev[static_cast<std::size_t>(v)];
                // Quadratic tie-breaker favors smooth parallel voice motion over giant jumps.
                cost += std::abs(d) + 0.02f * d * d;
            }
            if (cost < bestCost) {
                bestCost = cost;
                bestPerm = perm;
            }
        } while (nextPerm5(perm, startIdx));

        for (int v = 0; v < kVoiceCount; ++v)
            curr[static_cast<std::size_t>(v)] = orig[static_cast<std::size_t>(bestPerm[static_cast<std::size_t>(v)])];
    }
}

bool captureChordIntoScene(SceneMatrix& matrix, int sceneIndex, const int* midiNotes, int count, int rootMidi,
                           bool autoLead, bool keepBass) noexcept HBA_NONBLOCKING {
    if (midiNotes == nullptr || count <= 0)
        return false;
    const int sIdx = std::clamp(sceneIndex, 0, kSceneCount - 1);
    std::array<int, kVoiceCount> sorted{};
    const int n = std::min(count, kVoiceCount);
    for (int i = 0; i < n; ++i)
        sorted[static_cast<std::size_t>(i)] = midiNotes[i];
    std::sort(sorted.begin(), sorted.begin() + n);

    // Pad gracefully if 1..4 notes are held (e.g., 1 note -> unison/octave stack; 3 notes -> double bass + top octave)
    for (int i = n; i < kVoiceCount; ++i) {
        if (n == 1)
            sorted[static_cast<std::size_t>(i)] = sorted[0] + (i >= 2 ? 12 : 0);
        else
            sorted[static_cast<std::size_t>(i)] = sorted[static_cast<std::size_t>(i - n)] + 12;
    }

    auto& dst = matrix.offsets[static_cast<std::size_t>(sIdx)];
    for (int v = 0; v < kVoiceCount; ++v) {
        const float off = static_cast<float>(sorted[static_cast<std::size_t>(v)] - rootMidi);
        dst[static_cast<std::size_t>(v)] = std::clamp(off, -24.0f, 48.0f);
    }
    matrix.isCustom = true;
    if (autoLead)
        optimizeVoiceLeading(matrix, keepBass);
    return true;
}

double applyGravity(double midi, int rootPc, int scaleIndex, GravityMode mode, double strength,
                    double anchorMidi) noexcept HBA_NONBLOCKING {
    if (strength <= 1e-4)
        return midi;
    switch (mode) {
    case GravityMode::scaleWells:
        return applyScaleWells(midi, rootPc, scaleIndex, strength);
    case GravityMode::justIntonation:
        return applyJiWells(midi, anchorMidi, strength);
    case GravityMode::hybrid: {
        const double scaled = applyScaleWells(midi, rootPc, scaleIndex, strength * 0.85);
        return applyJiWells(scaled, anchorMidi, strength * 0.45);
    }
    }
    return midi;
}

MorphEval evaluateMorphPitches(const SceneMatrix& matrix, double rootMidi, int rootPc, int scaleIndex, double morph01,
                               double gravity, GravityMode gravMode, double stagger, StaggerOrder order, bool anchorSub,
                               bool mobiusWrap) noexcept HBA_NONBLOCKING {
    MorphEval result;
    const int segTotal = mobiusWrap ? kSceneCount : (kSceneCount - 1);
    const double clampedMorph = mobiusWrap ? hba::dsp::frac(morph01) : std::clamp(morph01, 0.0, 1.0);
    const double scaled = clampedMorph * static_cast<double>(segTotal);
    const int segIdx = std::min(static_cast<int>(std::floor(scaled)), segTotal - 1);
    const double localT = scaled - static_cast<double>(segIdx);
    result.activeScene = segIdx % kSceneCount;

    const auto& fromOffsets = matrix.offsets[static_cast<std::size_t>(segIdx % kSceneCount)];
    const int nextIdx = (segIdx + 1) % kSceneCount;
    const auto& toOffsets = matrix.offsets[static_cast<std::size_t>(nextIdx)];
    const double wrapShift = (mobiusWrap && segIdx == kSceneCount - 1) ? 12.0 : 0.0;

    constexpr std::array<std::array<double, kVoiceCount>, 4> kOrders{{
        {0.0, 0.35, 0.12, 0.68, 1.0}, // Inner First (Pedal-Steel classic)
        {0.0, 0.25, 0.50, 0.75, 1.0}, // Low to High
        {1.0, 0.75, 0.50, 0.25, 0.0}, // High to Low
        {0.0, 0.65, 1.0, 0.65, 0.0},  // Outside In
    }};
    const auto& orderDelays = kOrders[static_cast<std::size_t>(std::clamp(static_cast<int>(order), 0, 3))];
    const double stagClamped = std::clamp(stagger, 0.0, 1.0);
    const double windowLen = std::max(0.15, 1.0 - stagClamped * 0.45);

    // Evaluate Voice 0 first so Just-Intonation gravity can reference it as the harmonic root!
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        double pitch = 0.0;
        if (v == 0 && anchorSub && !mobiusWrap) {
            pitch = rootMidi + static_cast<double>(matrix.offsets[0][0]);
        } else {
            const double delay = orderDelays[vi] * stagClamped * 0.45;
            const double vt = std::clamp((localT - delay) / windowLen, 0.0, 1.0);
            const double smooth = vt * vt * (3.0 - 2.0 * vt);
            const double a = rootMidi + static_cast<double>(fromOffsets[vi]);
            const double b = rootMidi + static_cast<double>(toOffsets[vi]) + wrapShift;
            pitch = a + (b - a) * smooth;
            const double jiAnchor = (v == 0) ? rootMidi : result.pitches[0];
            pitch = applyGravity(pitch, rootPc, scaleIndex, gravMode, gravity, jiAnchor);
        }
        result.pitches[vi] = pitch;
    }

    return result;
}

} // namespace tg
