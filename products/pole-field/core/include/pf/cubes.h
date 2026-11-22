// 14-pole Z-plane filter cubes (7 cascaded 2nd-order pole-zero stages),
// polar-stable trilinear interpolation, exact TPT transfer response evaluation,
// and offline 14th-order Burg LPC spectral imprinting.
#pragma once

#include "pf/nonblocking.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>

namespace pf {

inline constexpr int kNumStages = 7;   // 7 conjugate pole-zero pairs = 14 poles + 14 zeros
inline constexpr int kNumCorners = 8;  // 2 x 2 x 2 morph cube (X, Y, Z)
inline constexpr int kNumCubes = 8;

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
    // Labels for the four front-face X/Y corners: TL(0,0), TR(1,0), BL(0,1), BR(1,1).
    std::array<std::string_view, 4> cornerNames;
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

// Optional user-imprinted corner (replaces corner 0 of Imprint Cube, or can
// blend into any cube when active).
struct ImprintData {
    bool active = false;
    CornerSpec corner {};

    friend bool operator==(const ImprintData&, const ImprintData&) = default;
};

// Interpolates a 3D cube in log-frequency and log-Q coordinates, then applies
// frequency shift (semitones), resonance scaling (0..1), dipole transform (-1..1),
// and stereo channel shift (semitones, opposite sign for L/R).
ResolvedLattice resolveLattice(int cubeIndex, float morphX, float morphY, float morphZ, float freqSemitones,
                               float resAmount, float transform, float stereoOffsetSt,
                               const ImprintData* imprint = nullptr) noexcept PF_NONBLOCKING;

// Exact frequency magnitude response in dB of the TPT pole-zero SVF lattice at
// `hz` (matching the DSP engine to numerical precision).
float evaluateMagnitudeDb(const ResolvedLattice& lattice, int activeStages, int topology, double hz,
                          double sampleRate) noexcept;

// Discrete-time Z-plane polar coordinates (via bilinear transform at `sampleRate`)
// for plotting conjugate poles and zeros inside the unit circle.
ZPlanePoint stageZPoint(const SectionSpec& s, double sampleRate) noexcept;

// Offline 14th-order Burg LPC + Durand-Kerner complex root finder.
// Extracts 7 conjugate pole pairs from the highest-energy voiced/resonant frame
// of `samples` and constructs complementary dipole zeros for a playable Z-plane corner.
CornerSpec imprintFromAudio(std::span<const float> monoSamples, double sampleRate) noexcept;

}
