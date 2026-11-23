// 14-pole Z-plane filter cubes (7 cascaded 2nd-order pole-zero stages),
// polar-stable trilinear interpolation, analytical passband auto-makeup,
// exact TPT transfer response evaluation, and offline 14th-order Burg LPC imprinting.
#pragma once

#include "pf/nonblocking.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>

namespace pf {

inline constexpr int kNumStages = 7;       // 7 conjugate pole-zero pairs = 14 poles + 14 zeros
inline constexpr int kNumCorners = 8;      // 2 x 2 x 2 morph cube (X, Y, Z)
inline constexpr int kNumImprintSlots = 4; // 4 front-face user-imprintable corners (TL, TR, BL, BR)
inline constexpr int kNumCubes = 12;

// One second-order pole-zero section in continuous-frequency polar coordinates.
// Storing frequencies in Hz and Q factors keeps bandwidth invariant across
// sample rates and oversampling factors.
struct SectionSpec {
    float poleHz = 400.0f;
    float poleQ = 4.0f;
    float zeroHz = 520.0f;
    float zeroQ = 3.0f;

    friend bool operator==(const SectionSpec&, const SectionSpec&) = default;
};

struct CornerSpec {
    std::array<SectionSpec, kNumStages> stages {};
    float makeupDb = 0.0f;

    friend bool operator==(const CornerSpec&, const CornerSpec&) = default;
};

struct CubeSpec {
    std::string_view name;
    // Labels for all 8 corners: 0..3 front face Z=0 (TL, TR, BL, BR), 4..7 back face Z=1 (TL, TR, BL, BR).
    std::array<std::string_view, kNumCorners> cornerNames;
    // Corner index = (z ? 4 : 0) + (y ? 2 : 0) + (x ? 1 : 0).
    std::array<CornerSpec, kNumCorners> corners;
};

// Resolved 7-stage filter state ready for audio DSP or exact UI evaluation.
struct ResolvedLattice {
    std::array<SectionSpec, kNumStages> stages {};
    float makeupLinear = 1.0f;
};

struct ZPlanePoint {
    float poleR = 0.0f;
    float poleTheta = 0.0f;  // radians in [0, pi]
    float zeroR = 0.0f;
    float zeroTheta = 0.0f;
};

const std::array<CubeSpec, kNumCubes>& factoryCubes() noexcept PF_NONBLOCKING;
const CubeSpec& cubeAt(int index) noexcept PF_NONBLOCKING;

// Multi-corner user-imprinted spectral data (up to 4 front-face corners on Imprint Cube).
// Legacy single-slot `active` / `corner` fields mirror slot 0 for simple callers.
struct ImprintData {
    bool active = false;
    CornerSpec corner {};
    std::array<bool, kNumImprintSlots> slotActive{};
    std::array<CornerSpec, kNumImprintSlots> slots{};

    bool hasAny() const noexcept PF_NONBLOCKING {
        if (active)
            return true;
        for (bool s : slotActive)
            if (s)
                return true;
        return false;
    }

    bool slotEnabled(int idx) const noexcept PF_NONBLOCKING {
        if (idx < 0 || idx >= kNumImprintSlots)
            return false;
        return idx == 0 ? (active || slotActive[0]) : slotActive[static_cast<std::size_t>(idx)];
    }

    const CornerSpec& slotCorner(int idx) const noexcept PF_NONBLOCKING {
        if (idx == 0 && active && !slotActive[0])
            return corner;
        return slots[static_cast<std::size_t>(std::clamp(idx, 0, kNumImprintSlots - 1))];
    }

    void setSlot(int idx, const CornerSpec& c) noexcept {
        if (idx < 0 || idx >= kNumImprintSlots)
            return;
        slotActive[static_cast<std::size_t>(idx)] = true;
        slots[static_cast<std::size_t>(idx)] = c;
        if (idx == 0) {
            active = true;
            corner = c;
        }
    }

    void clearSlot(int idx) noexcept {
        if (idx < 0 || idx >= kNumImprintSlots)
            return;
        slotActive[static_cast<std::size_t>(idx)] = false;
        slots[static_cast<std::size_t>(idx)] = {};
        if (idx == 0) {
            active = false;
            corner = {};
        }
    }

    friend bool operator==(const ImprintData&, const ImprintData&) = default;
};

// Interpolates a 3D cube in log-frequency and log-Q coordinates, applies frequency
// shift (semitones), resonance scaling (0..1 -> Q up to 45), dipole transform (-1..1),
// alternating binaural stereo channel shift (semitones), and analytical passband auto-makeup.
ResolvedLattice resolveLattice(int cubeIndex, float morphX, float morphY, float morphZ, float freqSemitones,
                               float resAmount, float transform, float stereoOffsetSt,
                               const ImprintData* imprint = nullptr, int activeStages = kNumStages,
                               int topology = 0) noexcept PF_NONBLOCKING;

// Exact frequency magnitude response in dB of the TPT pole-zero SVF lattice at
// `hz` (matching the DSP engine to numerical precision across Serial/Hybrid/Parallel).
float evaluateMagnitudeDb(const ResolvedLattice& lattice, int activeStages, int topology, double hz,
                          double sampleRate) noexcept;

// Discrete-time Z-plane polar coordinates (via bilinear transform at `sampleRate`)
// for plotting conjugate poles and zeros inside the unit circle.
ZPlanePoint stageZPoint(const SectionSpec& s, double sampleRate) noexcept;

// Offline 14th-order Burg LPC + Durand-Kerner complex root finder.
// Removes DC, anti-alias decimates any sample rate to ~24 kHz, selects the highest-energy
// sustained frame, filters strictly complex-conjugate formants (no duplicate stacking),
// places balanced alternating dipole zeros, and computes passband makeup.
CornerSpec imprintFromAudio(std::span<const float> monoSamples, double sampleRate);
}
