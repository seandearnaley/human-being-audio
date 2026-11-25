// SPDX-License-Identifier: AGPL-3.0-or-later
// Physical eigenfrequency ratios, nodal symmetries, and decay weights for Strike Body's
// 24-mode nonlinear tensioned resonator bank.
#pragma once

#include "sb/params.h"

#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sb {

struct ModalGeometry {
    // Frequency ratio relative to shell fundamental for each of the 24 modes.
    std::array<double, kModeCount> ratios{};
    // Relative modal excitation weights (baseline balance before strike-position shaping).
    std::array<double, kModeCount> gains{};
    // Nodal radial indices (0 = center-peaked, higher = outer rim/edge rings) for strike-pos physics & Chladni UI.
    std::array<double, kModeCount> radialOrder{};
    // Angular nodal indices (m) for Chladni visual scatter & stereo alternation.
    std::array<int, kModeCount> angularOrder{};
};

namespace detail {
// 1. Snare Shell: tensioned circular head Bessel zeros interleaved with brass shell overtones
inline constexpr ModalGeometry kSnareShell{
    {1.000, 1.482, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.940, 4.185,
     4.490, 4.782, 5.131, 5.440, 5.815, 6.192, 6.620, 7.055, 7.510, 8.040, 8.610, 9.240},
    {1.00, 0.84, 0.92, 0.78, 0.74, 0.68, 0.62, 0.58, 0.54, 0.52, 0.48, 0.45,
     0.42, 0.39, 0.36, 0.34, 0.31, 0.28, 0.26, 0.24, 0.22, 0.20, 0.18, 0.16},
    {0.08, 0.24, 0.32, 0.44, 0.20, 0.56, 0.48, 0.66, 0.36, 0.72, 0.58, 0.80,
     0.52, 0.76, 0.64, 0.86, 0.70, 0.90, 0.78, 0.92, 0.84, 0.95, 0.88, 0.98},
    {0, 1, 1, 2, 0, 3, 1, 4, 2, 0, 3, 5, 2, 4, 1, 5, 3, 6, 4, 2, 5, 6, 3, 7},
};

// 2. Kirchhoff Steel Plate: dense metallic square/rectangular plate eigenmodes (industrial clang!)
inline constexpr ModalGeometry kKirchhoffPlate{
    {1.000, 1.385, 1.792, 2.198, 2.612, 2.945, 3.380, 3.742, 4.190, 4.620, 5.015, 5.485,
     5.910, 6.380, 6.890, 7.340, 7.890, 8.420, 9.010, 9.580, 10.22, 10.89, 11.62, 12.40},
    {0.82, 0.95, 0.90, 0.86, 0.82, 0.78, 0.76, 0.72, 0.68, 0.65, 0.62, 0.58,
     0.55, 0.52, 0.49, 0.46, 0.43, 0.40, 0.38, 0.35, 0.33, 0.30, 0.28, 0.25},
    {0.15, 0.34, 0.28, 0.52, 0.46, 0.62, 0.58, 0.70, 0.66, 0.76, 0.72, 0.82,
     0.78, 0.85, 0.82, 0.88, 0.86, 0.91, 0.89, 0.94, 0.92, 0.96, 0.95, 0.98},
    {0, 1, 2, 1, 3, 2, 4, 3, 5, 2, 4, 6, 3, 5, 4, 7, 5, 6, 4, 7, 6, 8, 5, 7},
};

// 3. Pure Circular Drumhead: analytical Bessel J_m(alpha_mn) membrane roots (toms, timpani, kicks)
inline constexpr ModalGeometry kDrumhead{
    {1.000, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154,
     4.480, 4.602, 4.904, 5.131, 5.420, 5.650, 5.960, 6.240, 6.580, 6.910, 7.280, 7.680},
    {1.00, 0.76, 0.62, 0.58, 0.50, 0.46, 0.42, 0.38, 0.36, 0.33, 0.30, 0.28,
     0.25, 0.23, 0.21, 0.19, 0.18, 0.16, 0.15, 0.14, 0.12, 0.11, 0.10, 0.09},
    {0.05, 0.35, 0.52, 0.18, 0.64, 0.42, 0.72, 0.55, 0.28, 0.78, 0.62, 0.82,
     0.48, 0.86, 0.70, 0.88, 0.60, 0.90, 0.76, 0.92, 0.84, 0.94, 0.90, 0.97},
    {0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 1, 2, 6, 4, 0, 3, 5, 2, 6, 4, 7, 5, 6},
};

// 4. Metallic Bar: free-free Euler-Bernoulli beam + torsional overtones (anvil, cowbell, industrial hit)
inline constexpr ModalGeometry kMetallicBar{
    {1.000, 2.756, 3.410, 5.404, 6.180, 8.933, 9.850, 11.42, 13.34, 14.95, 16.82, 18.60,
     2.020, 4.120, 7.150, 10.60, 12.40, 15.90, 17.80, 19.60, 21.40, 23.10, 24.80, 26.50},
    {1.00, 0.88, 0.66, 0.74, 0.52, 0.62, 0.45, 0.48, 0.42, 0.36, 0.34, 0.30,
     0.58, 0.46, 0.40, 0.34, 0.30, 0.26, 0.22, 0.20, 0.18, 0.16, 0.14, 0.12},
    {0.10, 0.28, 0.42, 0.50, 0.58, 0.65, 0.70, 0.76, 0.80, 0.84, 0.88, 0.92,
     0.22, 0.46, 0.62, 0.74, 0.82, 0.86, 0.90, 0.92, 0.94, 0.96, 0.97, 0.99},
    {0, 1, 2, 1, 3, 2, 3, 4, 2, 4, 5, 3, 1, 2, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6},
};

// 5. Hollow Pipe: resonant tube air column + cylindrical wall modes (neuro clank, woody tube)
inline constexpr ModalGeometry kHollowPipe{
    {1.000, 2.008, 2.994, 4.022, 4.985, 6.038, 7.012, 8.065, 1.518, 2.525, 3.542, 4.560,
     5.590, 6.640, 7.720, 8.850, 9.420, 10.15, 11.08, 12.12, 13.25, 14.40, 15.65, 17.00},
    {1.00, 0.86, 0.78, 0.64, 0.58, 0.48, 0.42, 0.36, 0.72, 0.60, 0.50, 0.44,
     0.38, 0.34, 0.30, 0.26, 0.24, 0.22, 0.20, 0.18, 0.16, 0.15, 0.13, 0.12},
    {0.12, 0.26, 0.38, 0.48, 0.56, 0.64, 0.72, 0.80, 0.30, 0.44, 0.54, 0.66,
     0.74, 0.82, 0.86, 0.90, 0.78, 0.84, 0.88, 0.92, 0.94, 0.95, 0.97, 0.98},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 2, 3, 2, 3, 4, 1, 2, 3, 4, 5, 3, 4, 5},
};

// 6. Glass Bell: shimmering harmonic + minor-third bell cluster
inline constexpr ModalGeometry kGlassBell{
    {1.000, 2.000, 2.382, 3.004, 4.015, 4.210, 5.040, 5.425, 6.080, 6.720, 7.180, 8.090,
     8.840, 9.520, 10.35, 11.20, 12.15, 13.08, 14.12, 15.20, 16.40, 17.65, 19.00, 20.45},
    {1.00, 0.88, 0.82, 0.76, 0.70, 0.64, 0.58, 0.54, 0.48, 0.44, 0.40, 0.36,
     0.33, 0.30, 0.28, 0.25, 0.23, 0.21, 0.19, 0.17, 0.16, 0.14, 0.13, 0.11},
    {0.10, 0.22, 0.34, 0.40, 0.48, 0.56, 0.62, 0.68, 0.72, 0.76, 0.80, 0.84,
     0.86, 0.88, 0.90, 0.91, 0.93, 0.94, 0.95, 0.96, 0.97, 0.98, 0.98, 0.99},
    {0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 6, 5, 6, 7, 5, 6, 7, 8, 6, 7},
};
} // namespace detail

inline const ModalGeometry& modalGeometry(ShellModel model) noexcept HBA_NONBLOCKING {
    switch (model) {
    case ShellModel::snareShell:
        return detail::kSnareShell;
    case ShellModel::kirchhoffPlate:
        return detail::kKirchhoffPlate;
    case ShellModel::drumhead:
        return detail::kDrumhead;
    case ShellModel::metallicBar:
        return detail::kMetallicBar;
    case ShellModel::hollowPipe:
        return detail::kHollowPipe;
    case ShellModel::glassBell:
        return detail::kGlassBell;
    }
    return detail::kSnareShell;
}

// Evaluate effective stretched frequency ratio for mode k (stretch in [-0.5, +1.0]).
inline double stretchedRatio(double baseRatio, double stretch) noexcept HBA_NONBLOCKING {
    if (baseRatio <= 1.0001)
        return baseRatio;
    const double expPow = std::clamp(1.0 + stretch * 0.45, 0.55, 1.55);
    return std::pow(baseRatio, expPow);
}

// Compute physical excitation amplitude weight for mode k given strike position r in [0, 1] (center -> rim).
inline double strikeModeWeight(double baseGain, double radialOrder, int angularOrder,
                               double strikePos) noexcept HBA_NONBLOCKING {
    // Center strikes (r ~= 0) drive axisymmetric (m=0) low-radial modes strongly and suppress rim modes;
    // Edge strikes (r -> 1) excite higher angular & outer-radial overtones cleanly.
    const double r = std::clamp(strikePos, 0.0, 1.0);
    const double dist = std::abs(r - radialOrder);
    const double radialMatch = 0.22 + 0.78 * std::exp(-3.2 * dist * dist);
    const double angularFactor = (angularOrder == 0) ? (1.0 - 0.55 * r) : (0.18 + 0.82 * std::pow(r, 0.65));
    return baseGain * radialMatch * angularFactor;
}

} // namespace sb
