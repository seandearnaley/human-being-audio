// Formant data for the tract. The engine filters with it and the interface draws
// the tract curve from it.
#pragma once

#include <array>

namespace tf {

inline constexpr int kFormantCount = 5;

struct Vowel {
    std::array<float, kFormantCount> hz;
    std::array<float, kFormantCount> gainDb;
    std::array<float, kFormantCount> bandwidthHz;
};

// Order matches choices::vowel: a, æ, e, i, o, u.
// a/e/i/o/u: bass voice, Csound manual formant appendix. æ: Peterson & Barney
// male means for F1-F3; the rest of that row is an estimate.
inline constexpr std::array<Vowel, 6> kVowels { {
    { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 }, { 60, 70, 110, 120, 130 } },
    { { 660, 1720, 2410, 3300, 3750 }, { 0, -8, -14, -20, -28 }, { 70, 90, 110, 130, 140 } },
    { { 400, 1620, 2400, 2800, 3100 }, { 0, -12, -9, -12, -18 }, { 40, 80, 100, 120, 120 } },
    { { 250, 1750, 2600, 3050, 3340 }, { 0, -30, -16, -22, -28 }, { 60, 90, 100, 120, 120 } },
    { { 400, 750, 2400, 2600, 2900 }, { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
    { { 350, 600, 2400, 2675, 2950 }, { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } },
} };

}
