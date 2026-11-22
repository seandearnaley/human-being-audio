#include "pf/cubes.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace pf {

namespace {
constexpr double kPi = 3.14159265358979323846;

constexpr CornerSpec makeCorner(std::array<SectionSpec, kNumStages> stages, float makeupDb) {
    return { stages, makeupDb };
}

// Eight curated 14-pole Z-plane morph cubes. Each corner specifies 7 dipole
// sections {poleHz, poleQ, zeroHz, zeroQ} engineered so pole-zero pairs carve
// talking vocal peaks, razor notches, hollow tubes, and metallic clusters.
const std::array<CubeSpec, kNumCubes> kFactoryCubes { {
    // 0: Neuro Chew — iconic E-MU Morpheus talking bass cube
    // Alternating dipole orientation (zero above pole on even stages, below on odd)
    // cancels cumulative shelf tilt so all 7 formants & notches stay vivid up to 14 kHz.
    {
        "Neuro Chew",
        { "Vocal chew", "Phaser comb", "Hollow acid", "Metal ridge" },
        { {
            // Z=0: TL Vocal Chew (æ-like talking formants + sharp anti-formants)
            makeCorner({ {
                { 290.0f, 4.2f, 410.0f, 3.6f },
                { 660.0f, 5.8f, 520.0f, 4.4f },
                { 1420.0f, 6.2f, 1820.0f, 5.0f },
                { 2380.0f, 6.6f, 1980.0f, 5.2f },
                { 3450.0f, 5.6f, 4350.0f, 4.6f },
                { 5600.0f, 4.4f, 4700.0f, 3.8f },
                { 9400.0f, 3.2f, 11800.0f, 2.8f },
            } }, -4.5f),
            // Z=0: TR Phaser Comb (alternating razor peaks and deep phaser notches)
            makeCorner({ {
                { 185.0f, 4.6f, 235.0f, 7.2f },
                { 450.0f, 5.2f, 355.0f, 7.6f },
                { 920.0f, 5.8f, 1180.0f, 8.0f },
                { 1860.0f, 6.2f, 1480.0f, 7.8f },
                { 3520.0f, 5.6f, 4480.0f, 7.2f },
                { 6450.0f, 4.6f, 5200.0f, 6.2f },
                { 11200.0f, 3.4f, 13600.0f, 4.8f },
            } }, -3.5f),
            // Z=0: BL Hollow Acid (sub-mid resonant scoop + screaming upper mid)
            makeCorner({ {
                { 170.0f, 5.4f, 285.0f, 4.4f },
                { 510.0f, 5.0f, 395.0f, 6.2f },
                { 1040.0f, 3.8f, 1380.0f, 7.2f },
                { 1820.0f, 7.2f, 1480.0f, 5.0f },
                { 3120.0f, 5.4f, 3980.0f, 5.8f },
                { 5450.0f, 4.2f, 4450.0f, 4.4f },
                { 9600.0f, 3.2f, 12100.0f, 3.4f },
            } }, -5.5f),
            // Z=0: BR Metal Ridge (tight inharmonic resonant cluster)
            makeCorner({ {
                { 250.0f, 4.4f, 310.0f, 4.8f },
                { 640.0f, 6.8f, 520.0f, 5.6f },
                { 1490.0f, 7.8f, 1790.0f, 6.4f },
                { 2680.0f, 7.6f, 2240.0f, 6.2f },
                { 4350.0f, 7.0f, 5120.0f, 5.8f },
                { 6900.0f, 5.8f, 5850.0f, 5.0f },
                { 10800.0f, 4.4f, 12800.0f, 4.0f },
            } }, -6.5f),
            // Z=1: TL u-Throat (dark back vowel, chesty dip)
            makeCorner({ {
                { 220.0f, 5.8f, 360.0f, 4.8f },
                { 580.0f, 6.2f, 450.0f, 5.6f },
                { 1220.0f, 3.6f, 1620.0f, 6.6f },
                { 2320.0f, 6.0f, 1920.0f, 5.0f },
                { 3250.0f, 5.4f, 4180.0f, 5.0f },
                { 5300.0f, 3.8f, 4350.0f, 4.2f },
                { 8800.0f, 2.8f, 11400.0f, 3.0f },
            } }, -5.0f),
            // Z=1: TR Triple Notch (aggressive dual-dip neuro scoop)
            makeCorner({ {
                { 210.0f, 4.0f, 305.0f, 8.4f },
                { 540.0f, 4.8f, 420.0f, 8.8f },
                { 1140.0f, 6.0f, 1480.0f, 9.2f },
                { 2150.0f, 6.6f, 1720.0f, 8.6f },
                { 3920.0f, 5.4f, 4980.0f, 7.4f },
                { 6850.0f, 4.4f, 5600.0f, 6.2f },
                { 11400.0f, 3.2f, 13800.0f, 4.6f },
            } }, -4.0f),
            // Z=1: BL Razor Formant (high-Q vocal scream)
            makeCorner({ {
                { 340.0f, 6.4f, 480.0f, 5.6f },
                { 860.0f, 7.4f, 680.0f, 6.4f },
                { 1920.0f, 8.0f, 2360.0f, 6.6f },
                { 2940.0f, 7.4f, 2450.0f, 6.0f },
                { 4250.0f, 6.2f, 5200.0f, 5.2f },
                { 6650.0f, 5.0f, 5550.0f, 4.6f },
                { 10200.0f, 3.8f, 12400.0f, 3.6f },
            } }, -7.0f),
            // Z=1: BR Acid Scream (inverted biting resonance)
            makeCorner({ {
                { 280.0f, 6.0f, 205.0f, 4.6f },
                { 760.0f, 7.0f, 940.0f, 5.4f },
                { 1640.0f, 7.8f, 1320.0f, 6.2f },
                { 3080.0f, 7.4f, 3780.0f, 6.0f },
                { 5100.0f, 6.2f, 4200.0f, 5.2f },
                { 7900.0f, 5.0f, 9500.0f, 4.4f },
                { 11800.0f, 3.8f, 9800.0f, 3.6f },
            } }, -7.0f),
        } }
    },

    // 1: Vocal Matrix — hyper-articulate 8-vowel / anti-formant cube
    {
        "Vocal Matrix",
        { "Ah /a/", "Ee /i/", "Oh /o/", "Oo /u/" },
        { {
            makeCorner({ {
                { 600.0f, 6.4f, 840.0f, 4.2f }, { 1040.0f, 7.0f, 1480.0f, 4.8f }, { 2250.0f, 6.8f, 2650.0f, 4.4f },
                { 2450.0f, 6.2f, 3150.0f, 4.2f }, { 2750.0f, 5.4f, 3850.0f, 3.8f }, { 4500.0f, 3.2f, 6200.0f, 2.8f },
                { 7800.0f, 2.4f, 10800.0f, 2.2f },
            } }, -5.5f),
            makeCorner({ {
                { 260.0f, 5.8f, 540.0f, 5.6f }, { 920.0f, 2.6f, 1260.0f, 6.2f }, { 1780.0f, 7.2f, 2120.0f, 5.0f },
                { 2600.0f, 7.0f, 3020.0f, 4.8f }, { 3240.0f, 6.4f, 3980.0f, 4.4f }, { 5100.0f, 3.8f, 6800.0f, 3.2f },
                { 8400.0f, 2.6f, 11400.0f, 2.4f },
            } }, -5.5f),
            makeCorner({ {
                { 400.0f, 6.2f, 600.0f, 4.8f }, { 750.0f, 6.8f, 1180.0f, 5.4f }, { 1550.0f, 2.8f, 2020.0f, 5.8f },
                { 2400.0f, 6.0f, 2860.0f, 4.6f }, { 2900.0f, 5.2f, 3750.0f, 4.2f }, { 4800.0f, 3.0f, 6700.0f, 2.8f },
                { 8000.0f, 2.2f, 11000.0f, 2.2f },
            } }, -5.0f),
            makeCorner({ {
                { 320.0f, 6.5f, 480.0f, 5.0f }, { 600.0f, 6.8f, 960.0f, 5.8f }, { 1320.0f, 2.5f, 1850.0f, 6.4f },
                { 2400.0f, 5.4f, 2820.0f, 4.8f }, { 2950.0f, 4.8f, 3850.0f, 4.2f }, { 4700.0f, 2.8f, 6600.0f, 2.8f },
                { 7800.0f, 2.0f, 10800.0f, 2.2f },
            } }, -5.0f),
            // Back Z=1: æ, e, Nasal, Whisper
            makeCorner({ {
                { 660.0f, 6.8f, 960.0f, 4.6f }, { 1720.0f, 7.2f, 2140.0f, 5.2f }, { 2410.0f, 6.8f, 2890.0f, 4.8f },
                { 3300.0f, 5.8f, 3980.0f, 4.4f }, { 3750.0f, 5.2f, 4800.0f, 4.0f }, { 5800.0f, 3.6f, 7500.0f, 3.0f },
                { 9200.0f, 2.6f, 12000.0f, 2.2f },
            } }, -6.0f),
            makeCorner({ {
                { 400.0f, 6.0f, 640.0f, 4.8f }, { 1620.0f, 7.2f, 2020.0f, 5.2f }, { 2400.0f, 6.6f, 2840.0f, 4.8f },
                { 2800.0f, 6.0f, 3420.0f, 4.6f }, { 3400.0f, 5.0f, 4450.0f, 4.0f }, { 5600.0f, 3.4f, 7300.0f, 3.0f },
                { 8800.0f, 2.4f, 11600.0f, 2.2f },
            } }, -5.5f),
            makeCorner({ {
                { 280.0f, 5.4f, 520.0f, 7.8f }, { 1120.0f, 7.6f, 1540.0f, 7.4f }, { 2100.0f, 6.4f, 2580.0f, 7.0f },
                { 2980.0f, 5.8f, 3540.0f, 6.2f }, { 4200.0f, 4.8f, 5100.0f, 5.2f }, { 6400.0f, 3.6f, 8000.0f, 4.0f },
                { 9600.0f, 2.8f, 12200.0f, 3.0f },
            } }, -5.5f),
            makeCorner({ {
                { 420.0f, 3.0f, 290.0f, 5.4f }, { 1280.0f, 5.4f, 940.0f, 6.0f }, { 2350.0f, 6.8f, 1840.0f, 5.6f },
                { 3600.0f, 6.6f, 2920.0f, 5.4f }, { 5200.0f, 5.8f, 4250.0f, 4.8f }, { 7400.0f, 4.6f, 6100.0f, 4.0f },
                { 10800.0f, 3.6f, 8900.0f, 3.4f },
            } }, -5.5f),
        } }
    },

    // 2: Phase Razor — ultra-deep allpass-adjacent notches & comb sweeps
    {
        "Phase Razor",
        { "Deep comb", "Inverse rip", "Dual scoop", "Tube cavity" },
        { {
            makeCorner({ {
                { 160.0f, 4.6f, 195.0f, 9.5f }, { 340.0f, 4.8f, 420.0f, 9.5f }, { 710.0f, 5.2f, 870.0f, 9.5f },
                { 1480.0f, 5.6f, 1820.0f, 9.5f }, { 2960.0f, 5.4f, 3680.0f, 9.0f }, { 5800.0f, 4.6f, 7200.0f, 8.2f },
                { 10600.0f, 3.6f, 12900.0f, 6.8f },
            } }, -3.0f),
            makeCorner({ {
                { 220.0f, 5.2f, 175.0f, 9.2f }, { 490.0f, 5.6f, 395.0f, 9.4f }, { 1080.0f, 6.0f, 860.0f, 9.4f },
                { 2260.0f, 6.2f, 1820.0f, 9.2f }, { 4520.0f, 5.6f, 3640.0f, 8.6f }, { 8100.0f, 4.4f, 6600.0f, 7.6f },
                { 12600.0f, 3.4f, 10200.0f, 6.4f },
            } }, -3.5f),
            makeCorner({ {
                { 260.0f, 6.0f, 410.0f, 9.8f }, { 480.0f, 3.4f, 620.0f, 9.8f }, { 920.0f, 5.8f, 1480.0f, 9.8f },
                { 1820.0f, 3.6f, 2320.0f, 9.6f }, { 3450.0f, 6.2f, 4700.0f, 8.8f }, { 6400.0f, 4.2f, 8400.0f, 7.2f },
                { 11000.0f, 3.0f, 13600.0f, 5.4f },
            } }, -3.5f),
            makeCorner({ {
                { 195.0f, 6.8f, 390.0f, 6.4f }, { 585.0f, 6.8f, 780.0f, 6.4f }, { 975.0f, 6.6f, 1170.0f, 6.4f },
                { 1365.0f, 6.4f, 1560.0f, 6.4f }, { 2145.0f, 6.0f, 2535.0f, 6.0f }, { 3705.0f, 5.2f, 4485.0f, 5.2f },
                { 6825.0f, 4.2f, 8385.0f, 4.2f },
            } }, -5.5f),
            makeCorner({ {
                { 140.0f, 5.4f, 220.0f, 9.6f }, { 380.0f, 5.8f, 580.0f, 9.6f }, { 940.0f, 6.2f, 1420.0f, 9.6f },
                { 2180.0f, 6.4f, 3200.0f, 9.2f }, { 4600.0f, 5.8f, 6600.0f, 8.4f }, { 8400.0f, 4.6f, 11200.0f, 7.0f },
                { 13200.0f, 3.2f, 15400.0f, 5.4f },
            } }, -4.0f),
            makeCorner({ {
                { 310.0f, 6.4f, 240.0f, 9.6f }, { 780.0f, 6.8f, 610.0f, 9.6f }, { 1840.0f, 7.0f, 1420.0f, 9.6f },
                { 3900.0f, 6.6f, 3050.0f, 9.0f }, { 7200.0f, 5.6f, 5800.0f, 8.0f }, { 10800.0f, 4.2f, 8900.0f, 6.6f },
                { 14400.0f, 3.0f, 12000.0f, 5.2f },
            } }, -4.5f),
            makeCorner({ {
                { 180.0f, 4.0f, 290.0f, 9.8f }, { 640.0f, 6.8f, 960.0f, 9.8f }, { 1320.0f, 4.2f, 1980.0f, 9.8f },
                { 2680.0f, 7.0f, 3850.0f, 9.4f }, { 5200.0f, 4.8f, 7200.0f, 8.4f }, { 8800.0f, 4.2f, 11400.0f, 7.0f },
                { 13000.0f, 3.0f, 15200.0f, 5.2f },
            } }, -4.0f),
            makeCorner({ {
                { 240.0f, 7.4f, 480.0f, 7.0f }, { 720.0f, 7.2f, 960.0f, 7.0f }, { 1200.0f, 7.0f, 1440.0f, 7.0f },
                { 1920.0f, 6.8f, 2400.0f, 6.8f }, { 3120.0f, 6.2f, 3840.0f, 6.2f }, { 5280.0f, 5.2f, 6480.0f, 5.2f },
                { 8880.0f, 4.2f, 10800.0f, 4.2f },
            } }, -6.0f),
        } }
    },

    // 3: Metal Reactor — inharmonic metallic plate/bar modal resonances
    {
        "Metal Reactor",
        { "Steel plate", "Glass bell", "Spring tank", "Clang cluster" },
        { {
            makeCorner({ {
                { 215.0f, 7.2f, 260.0f, 5.2f }, { 592.0f, 8.0f, 705.0f, 5.8f }, { 1164.0f, 8.4f, 1380.0f, 6.2f },
                { 2048.0f, 8.6f, 2420.0f, 6.4f }, { 3390.0f, 8.2f, 3980.0f, 6.0f }, { 5420.0f, 7.0f, 6450.0f, 5.4f },
                { 8640.0f, 5.6f, 10300.0f, 4.6f },
            } }, -7.5f),
            makeCorner({ {
                { 380.0f, 8.2f, 440.0f, 6.0f }, { 1046.0f, 8.8f, 1210.0f, 6.4f }, { 2058.0f, 9.0f, 2380.0f, 6.6f },
                { 3410.0f, 8.6f, 3940.0f, 6.4f }, { 5260.0f, 7.8f, 6120.0f, 5.8f }, { 7820.0f, 6.6f, 9100.0f, 5.0f },
                { 11400.0f, 5.2f, 13400.0f, 4.2f },
            } }, -8.0f),
            makeCorner({ {
                { 180.0f, 6.4f, 235.0f, 7.2f }, { 430.0f, 7.4f, 540.0f, 7.8f }, { 890.0f, 7.8f, 1090.0f, 8.0f },
                { 1620.0f, 8.0f, 1980.0f, 8.0f }, { 2840.0f, 7.6f, 3460.0f, 7.4f }, { 4950.0f, 6.4f, 6080.0f, 6.4f },
                { 8300.0f, 5.0f, 10200.0f, 5.2f },
            } }, -6.5f),
            makeCorner({ {
                { 310.0f, 7.8f, 245.0f, 6.4f }, { 795.0f, 8.6f, 640.0f, 6.8f }, { 1490.0f, 9.0f, 1220.0f, 7.0f },
                { 2510.0f, 8.8f, 2080.0f, 6.8f }, { 4120.0f, 8.2f, 3420.0f, 6.4f }, { 6680.0f, 7.0f, 5580.0f, 5.6f },
                { 10200.0f, 5.6f, 8600.0f, 4.6f },
            } }, -8.0f),
            // Z=1 darker/heavier alloys
            makeCorner({ {
                { 165.0f, 7.4f, 205.0f, 5.4f }, { 455.0f, 8.2f, 550.0f, 6.0f }, { 910.0f, 8.6f, 1085.0f, 6.4f },
                { 1640.0f, 8.4f, 1960.0f, 6.2f }, { 2790.0f, 7.8f, 3320.0f, 5.8f }, { 4560.0f, 6.6f, 5480.0f, 5.2f },
                { 7400.0f, 5.2f, 8900.0f, 4.4f },
            } }, -7.5f),
            makeCorner({ {
                { 290.0f, 8.4f, 345.0f, 6.2f }, { 810.0f, 8.8f, 955.0f, 6.6f }, { 1620.0f, 9.0f, 1910.0f, 6.8f },
                { 2760.0f, 8.6f, 3260.0f, 6.4f }, { 4380.0f, 7.8f, 5180.0f, 5.8f }, { 6720.0f, 6.6f, 7950.0f, 5.0f },
                { 10100.0f, 5.2f, 12000.0f, 4.2f },
            } }, -8.0f),
            makeCorner({ {
                { 150.0f, 6.8f, 200.0f, 7.6f }, { 370.0f, 7.6f, 470.0f, 8.0f }, { 780.0f, 8.0f, 965.0f, 8.2f },
                { 1440.0f, 8.2f, 1780.0f, 8.0f }, { 2560.0f, 7.6f, 3150.0f, 7.4f }, { 4420.0f, 6.4f, 5460.0f, 6.4f },
                { 7600.0f, 5.0f, 9400.0f, 5.2f },
            } }, -6.5f),
            makeCorner({ {
                { 245.0f, 8.0f, 190.0f, 6.6f }, { 640.0f, 8.6f, 510.0f, 7.0f }, { 1230.0f, 9.0f, 995.0f, 7.2f },
                { 2120.0f, 8.8f, 1740.0f, 7.0f }, { 3540.0f, 8.2f, 2920.0f, 6.4f }, { 5800.0f, 7.0f, 4820.0f, 5.6f },
                { 9100.0f, 5.6f, 7650.0f, 4.6f },
            } }, -8.0f),
        } }
    },

    // 4: Break Sculpt — designed for drum breaks & Livecut Redux pairs
    {
        "Break Sculpt",
        { "Punch box", "Radio mid", "Snare ring", "Air crisp" },
        { {
            makeCorner({ {
                { 140.0f, 3.2f, 260.0f, 2.8f }, { 360.0f, 2.6f, 520.0f, 3.8f }, { 920.0f, 3.4f, 1320.0f, 3.2f },
                { 2150.0f, 4.2f, 2850.0f, 3.0f }, { 4400.0f, 4.0f, 5600.0f, 2.8f }, { 7800.0f, 3.2f, 9800.0f, 2.4f },
                { 12200.0f, 2.4f, 14800.0f, 2.0f },
            } }, -2.0f),
            makeCorner({ {
                { 420.0f, 4.6f, 190.0f, 3.2f }, { 880.0f, 5.4f, 520.0f, 4.2f }, { 1480.0f, 6.2f, 1950.0f, 4.8f },
                { 2450.0f, 5.8f, 3150.0f, 5.4f }, { 3600.0f, 3.4f, 5200.0f, 4.6f }, { 5600.0f, 2.2f, 8400.0f, 3.6f },
                { 8800.0f, 1.8f, 12600.0f, 2.8f },
            } }, -4.0f),
            makeCorner({ {
                { 205.0f, 6.8f, 310.0f, 5.6f }, { 460.0f, 4.2f, 680.0f, 5.8f }, { 1120.0f, 6.4f, 1540.0f, 5.2f },
                { 2680.0f, 6.0f, 3380.0f, 5.0f }, { 5100.0f, 5.4f, 6300.0f, 4.4f }, { 8200.0f, 4.2f, 10200.0f, 3.6f },
                { 12400.0f, 3.0f, 14800.0f, 2.8f },
            } }, -5.0f),
            makeCorner({ {
                { 280.0f, 2.2f, 160.0f, 3.6f }, { 640.0f, 2.8f, 440.0f, 4.2f }, { 1480.0f, 3.6f, 1080.0f, 4.0f },
                { 3200.0f, 4.8f, 2480.0f, 3.8f }, { 6200.0f, 5.8f, 4950.0f, 4.2f }, { 9800.0f, 5.4f, 8000.0f, 4.0f },
                { 13800.0f, 4.2f, 11400.0f, 3.4f },
            } }, -3.5f),
            // Z=1 darker dub / dub-siren break treatments
            makeCorner({ {
                { 120.0f, 4.4f, 240.0f, 4.2f }, { 320.0f, 4.0f, 540.0f, 5.0f }, { 780.0f, 4.6f, 1180.0f, 5.2f },
                { 1680.0f, 4.8f, 2420.0f, 5.0f }, { 3200.0f, 3.8f, 4600.0f, 4.4f }, { 5600.0f, 2.8f, 8000.0f, 3.6f },
                { 9200.0f, 2.0f, 12600.0f, 2.8f },
            } }, -3.5f),
            makeCorner({ {
                { 340.0f, 5.6f, 180.0f, 4.6f }, { 760.0f, 6.4f, 480.0f, 5.2f }, { 1360.0f, 7.0f, 1820.0f, 5.6f },
                { 2280.0f, 6.4f, 2980.0f, 5.8f }, { 3550.0f, 4.2f, 4900.0f, 5.0f }, { 5800.0f, 2.8f, 8200.0f, 4.0f },
                { 9000.0f, 2.2f, 12400.0f, 3.0f },
            } }, -5.0f),
            makeCorner({ {
                { 185.0f, 7.4f, 270.0f, 6.2f }, { 420.0f, 5.2f, 610.0f, 6.4f }, { 980.0f, 7.0f, 1350.0f, 6.0f },
                { 2340.0f, 6.8f, 2980.0f, 5.6f }, { 4500.0f, 5.8f, 5600.0f, 4.8f }, { 7400.0f, 4.6f, 9200.0f, 4.0f },
                { 11400.0f, 3.4f, 13800.0f, 3.2f },
            } }, -5.5f),
            makeCorner({ {
                { 220.0f, 3.0f, 140.0f, 4.4f }, { 560.0f, 3.8f, 390.0f, 4.8f }, { 1280.0f, 4.8f, 920.0f, 4.8f },
                { 2840.0f, 5.8f, 2180.0f, 4.6f }, { 5500.0f, 6.4f, 4350.0f, 4.8f }, { 8900.0f, 5.8f, 7200.0f, 4.4f },
                { 13000.0f, 4.6f, 10800.0f, 3.8f },
            } }, -4.5f),
        } }
    },

    // 5: Acid Ladder — steep multi-pole lowpass / bandpass / twin-peak / highpass
    {
        "Acid Ladder",
        { "Dark LP", "Twin peak", "Reso BP", "Bite HP" },
        { {
            makeCorner({ {
                { 240.0f, 5.2f, 820.0f, 1.4f }, { 310.0f, 4.4f, 1280.0f, 1.4f }, { 420.0f, 3.6f, 2100.0f, 1.4f },
                { 620.0f, 2.8f, 3400.0f, 1.4f }, { 980.0f, 2.2f, 5400.0f, 1.4f }, { 1650.0f, 1.8f, 8200.0f, 1.4f },
                { 2900.0f, 1.5f, 12000.0f, 1.4f },
            } }, -4.0f),
            makeCorner({ {
                { 340.0f, 7.2f, 680.0f, 4.2f }, { 440.0f, 5.6f, 960.0f, 4.8f }, { 1380.0f, 7.6f, 1980.0f, 4.6f },
                { 1720.0f, 6.0f, 2650.0f, 4.4f }, { 3400.0f, 4.2f, 4900.0f, 3.6f }, { 5900.0f, 3.0f, 8200.0f, 3.0f },
                { 9800.0f, 2.2f, 13000.0f, 2.4f },
            } }, -6.5f),
            makeCorner({ {
                { 540.0f, 6.2f, 210.0f, 2.4f }, { 740.0f, 7.0f, 360.0f, 2.8f }, { 980.0f, 7.4f, 1680.0f, 3.2f },
                { 1280.0f, 6.4f, 2450.0f, 3.0f }, { 2100.0f, 4.2f, 3900.0f, 2.8f }, { 3800.0f, 2.8f, 6500.0f, 2.4f },
                { 6800.0f, 2.0f, 10800.0f, 2.0f },
            } }, -6.5f),
            makeCorner({ {
                { 960.0f, 6.6f, 240.0f, 1.6f }, { 1380.0f, 6.2f, 420.0f, 1.8f }, { 2100.0f, 5.6f, 680.0f, 2.0f },
                { 3250.0f, 5.0f, 1120.0f, 2.2f }, { 5100.0f, 4.4f, 1920.0f, 2.4f }, { 8100.0f, 3.8f, 3400.0f, 2.6f },
                { 12200.0f, 3.0f, 5800.0f, 2.6f },
            } }, -5.5f),
            // Z=1 brighter open ladder states
            makeCorner({ {
                { 480.0f, 6.0f, 1480.0f, 1.6f }, { 680.0f, 5.2f, 2240.0f, 1.6f }, { 960.0f, 4.4f, 3400.0f, 1.6f },
                { 1420.0f, 3.6f, 5100.0f, 1.6f }, { 2200.0f, 2.8f, 7600.0f, 1.6f }, { 3600.0f, 2.2f, 10800.0f, 1.6f },
                { 6200.0f, 1.8f, 14400.0f, 1.6f },
            } }, -4.5f),
            makeCorner({ {
                { 560.0f, 7.6f, 980.0f, 5.0f }, { 740.0f, 6.0f, 1380.0f, 5.2f }, { 2280.0f, 8.0f, 3100.0f, 5.2f },
                { 2840.0f, 6.4f, 4050.0f, 4.8f }, { 4800.0f, 4.8f, 6800.0f, 4.0f }, { 7600.0f, 3.6f, 10400.0f, 3.2f },
                { 11800.0f, 2.6f, 14800.0f, 2.6f },
            } }, -7.0f),
            makeCorner({ {
                { 880.0f, 6.8f, 320.0f, 2.8f }, { 1220.0f, 7.4f, 540.0f, 3.2f }, { 1680.0f, 7.8f, 2680.0f, 3.8f },
                { 2240.0f, 6.6f, 3850.0f, 3.4f }, { 3400.0f, 4.8f, 5900.0f, 3.0f }, { 5600.0f, 3.2f, 8900.0f, 2.6f },
                { 9200.0f, 2.4f, 13200.0f, 2.2f },
            } }, -7.0f),
            makeCorner({ {
                { 1480.0f, 7.2f, 340.0f, 1.8f }, { 2150.0f, 6.8f, 580.0f, 2.0f }, { 3200.0f, 6.2f, 960.0f, 2.2f },
                { 4700.0f, 5.6f, 1580.0f, 2.4f }, { 6900.0f, 4.8f, 2650.0f, 2.6f }, { 9900.0f, 4.0f, 4400.0f, 2.8f },
                { 13800.0f, 3.2f, 7200.0f, 2.8f },
            } }, -6.0f),
        } }
    },

    // 6: Subterranean — dark techstep & halftime mid-bass cavities
    {
        "Subterranean",
        { "Chest cave", "Sub notch", "Wood pipe", "Swamp mud" },
        { {
            makeCorner({ {
                { 155.0f, 4.8f, 265.0f, 4.6f }, { 340.0f, 5.6f, 560.0f, 5.2f }, { 740.0f, 4.4f, 1180.0f, 5.8f },
                { 1480.0f, 3.6f, 2320.0f, 5.2f }, { 2680.0f, 2.8f, 4200.0f, 4.2f }, { 4600.0f, 2.2f, 7400.0f, 3.4f },
                { 7800.0f, 1.8f, 11800.0f, 2.6f },
            } }, -3.5f),
            makeCorner({ {
                { 175.0f, 3.6f, 230.0f, 8.8f }, { 395.0f, 4.8f, 520.0f, 9.0f }, { 820.0f, 5.2f, 1140.0f, 8.6f },
                { 1650.0f, 4.4f, 2350.0f, 7.6f }, { 3150.0f, 3.4f, 4600.0f, 6.2f }, { 5600.0f, 2.6f, 8200.0f, 4.8f },
                { 9400.0f, 2.0f, 12800.0f, 3.6f },
            } }, -3.0f),
            makeCorner({ {
                { 195.0f, 6.4f, 330.0f, 4.8f }, { 480.0f, 6.8f, 760.0f, 5.4f }, { 960.0f, 6.2f, 1440.0f, 5.6f },
                { 1820.0f, 5.0f, 2680.0f, 5.0f }, { 3300.0f, 3.8f, 4900.0f, 4.2f }, { 5800.0f, 2.8f, 8400.0f, 3.4f },
                { 9600.0f, 2.2f, 12800.0f, 2.6f },
            } }, -5.0f),
            makeCorner({ {
                { 140.0f, 5.4f, 290.0f, 6.8f }, { 360.0f, 3.2f, 620.0f, 7.4f }, { 890.0f, 6.2f, 1340.0f, 6.8f },
                { 1720.0f, 3.8f, 2580.0f, 6.0f }, { 3100.0f, 4.4f, 4700.0f, 5.0f }, { 5400.0f, 3.0f, 8000.0f, 3.8f },
                { 8800.0f, 2.2f, 12200.0f, 2.8f },
            } }, -4.0f),
            // Z=1 resonant upper-mid reveal
            makeCorner({ {
                { 210.0f, 5.4f, 340.0f, 5.0f }, { 520.0f, 6.0f, 810.0f, 5.4f }, { 1140.0f, 5.8f, 1680.0f, 5.8f },
                { 2250.0f, 5.6f, 3150.0f, 5.2f }, { 3980.0f, 4.4f, 5400.0f, 4.4f }, { 6600.0f, 3.2f, 9000.0f, 3.4f },
                { 10400.0f, 2.4f, 13400.0f, 2.6f },
            } }, -4.5f),
            makeCorner({ {
                { 235.0f, 4.2f, 315.0f, 9.0f }, { 580.0f, 5.4f, 760.0f, 9.2f }, { 1260.0f, 6.0f, 1680.0f, 8.8f },
                { 2520.0f, 5.4f, 3420.0f, 7.8f }, { 4600.0f, 4.2f, 6300.0f, 6.4f }, { 7600.0f, 3.2f, 10200.0f, 5.0f },
                { 11800.0f, 2.4f, 14600.0f, 3.8f },
            } }, -3.5f),
            makeCorner({ {
                { 260.0f, 6.8f, 440.0f, 5.2f }, { 660.0f, 7.2f, 1020.0f, 5.8f }, { 1380.0f, 6.8f, 2040.0f, 5.8f },
                { 2620.0f, 5.8f, 3780.0f, 5.2f }, { 4650.0f, 4.4f, 6500.0f, 4.4f }, { 7600.0f, 3.4f, 10400.0f, 3.4f },
                { 11600.0f, 2.6f, 14400.0f, 2.8f },
            } }, -5.5f),
            makeCorner({ {
                { 195.0f, 6.0f, 385.0f, 7.0f }, { 510.0f, 4.0f, 840.0f, 7.6f }, { 1260.0f, 6.8f, 1880.0f, 7.0f },
                { 2460.0f, 4.8f, 3580.0f, 6.2f }, { 4420.0f, 5.0f, 6200.0f, 5.2f }, { 7200.0f, 3.6f, 9900.0f, 4.0f },
                { 11000.0f, 2.6f, 14000.0f, 3.0f },
            } }, -4.5f),
        } }
    },

    // 7: Imprint Cube — Corner 0 accepts Burg LPC WAV imprints; other 7 corners provide complementary morph targets
    {
        "Imprint Cube",
        { "Imprint A", "Phaser comb", "Hollow tube", "Metal ridge" },
        { {
            // Default Corner 0 before user drops/loads a WAV imprint
            makeCorner({ {
                { 310.0f, 4.6f, 470.0f, 3.8f }, { 740.0f, 5.8f, 1090.0f, 4.6f }, { 1540.0f, 6.2f, 2080.0f, 5.0f },
                { 2520.0f, 6.0f, 3180.0f, 4.8f }, { 3820.0f, 5.0f, 4880.0f, 4.2f }, { 6100.0f, 3.8f, 7800.0f, 3.2f },
                { 9800.0f, 2.8f, 12400.0f, 2.4f },
            } }, -4.5f),
            makeCorner({ {
                { 185.0f, 4.4f, 245.0f, 7.4f }, { 430.0f, 5.0f, 585.0f, 7.8f }, { 910.0f, 5.6f, 1250.0f, 8.0f },
                { 1820.0f, 6.0f, 2480.0f, 7.6f }, { 3450.0f, 5.2f, 4680.0f, 6.8f }, { 6300.0f, 4.2f, 8300.0f, 5.6f },
                { 11000.0f, 3.0f, 13800.0f, 4.2f },
            } }, -3.5f),
            makeCorner({ {
                { 195.0f, 6.4f, 390.0f, 5.6f }, { 585.0f, 6.4f, 780.0f, 5.6f }, { 1120.0f, 6.2f, 1480.0f, 5.8f },
                { 1960.0f, 6.0f, 2520.0f, 5.6f }, { 3380.0f, 5.2f, 4320.0f, 5.0f }, { 5800.0f, 4.0f, 7400.0f, 4.2f },
                { 9600.0f, 3.0f, 12200.0f, 3.2f },
            } }, -5.0f),
            makeCorner({ {
                { 250.0f, 5.2f, 315.0f, 5.0f }, { 630.0f, 7.0f, 810.0f, 5.8f }, { 1490.0f, 7.8f, 1780.0f, 6.4f },
                { 2680.0f, 7.4f, 3160.0f, 6.2f }, { 4350.0f, 6.8f, 5120.0f, 5.8f }, { 6900.0f, 5.4f, 8200.0f, 4.6f },
                { 10400.0f, 4.0f, 12600.0f, 3.6f },
            } }, -6.0f),
            // Z=1 inverted/mirrored shadows of the front plane
            makeCorner({ {
                { 420.0f, 5.2f, 290.0f, 4.6f }, { 1020.0f, 6.2f, 710.0f, 5.2f }, { 2120.0f, 6.6f, 1520.0f, 5.4f },
                { 3450.0f, 6.2f, 2580.0f, 5.0f }, { 5200.0f, 5.2f, 3980.0f, 4.4f }, { 7900.0f, 4.0f, 6200.0f, 3.4f },
                { 11800.0f, 3.0f, 9400.0f, 2.6f },
            } }, -5.0f),
            makeCorner({ {
                { 230.0f, 5.0f, 180.0f, 8.4f }, { 540.0f, 5.6f, 420.0f, 8.6f }, { 1180.0f, 6.0f, 920.0f, 8.6f },
                { 2420.0f, 6.2f, 1920.0f, 8.2f }, { 4680.0f, 5.4f, 3720.0f, 7.2f }, { 8200.0f, 4.2f, 6600.0f, 6.0f },
                { 12600.0f, 3.2f, 10200.0f, 4.6f },
            } }, -4.0f),
            makeCorner({ {
                { 165.0f, 5.8f, 340.0f, 6.2f }, { 480.0f, 5.2f, 760.0f, 6.6f }, { 1040.0f, 4.4f, 1460.0f, 7.0f },
                { 1820.0f, 7.0f, 2380.0f, 5.2f }, { 3150.0f, 5.2f, 4250.0f, 5.6f }, { 5500.0f, 3.6f, 7200.0f, 4.2f },
                { 9400.0f, 2.6f, 12400.0f, 3.2f },
            } }, -5.0f),
            makeCorner({ {
                { 280.0f, 6.2f, 195.0f, 5.0f }, { 760.0f, 7.2f, 560.0f, 5.8f }, { 1640.0f, 8.0f, 1240.0f, 6.4f },
                { 3080.0f, 7.6f, 2420.0f, 6.0f }, { 5100.0f, 6.4f, 4100.0f, 5.2f }, { 7900.0f, 5.0f, 6400.0f, 4.4f },
                { 11800.0f, 3.8f, 9600.0f, 3.6f },
            } }, -6.5f),
        } }
    },
} };

double logTrilerp(double c000, double c100, double c010, double c110,
                  double c001, double c101, double c011, double c111,
                  double x, double y, double z) noexcept PF_NONBLOCKING {
    const double l00 = std::log(c000) * (1.0 - x) + std::log(c100) * x;
    const double l10 = std::log(c010) * (1.0 - x) + std::log(c110) * x;
    const double l01 = std::log(c001) * (1.0 - x) + std::log(c101) * x;
    const double l11 = std::log(c011) * (1.0 - x) + std::log(c111) * x;
    const double f0 = l00 * (1.0 - y) + l10 * y;
    const double f1 = l01 * (1.0 - y) + l11 * y;
    return std::exp(f0 * (1.0 - z) + f1 * z);
}

double linTrilerp(double c000, double c100, double c010, double c110,
                  double c001, double c101, double c011, double c111,
                  double x, double y, double z) noexcept PF_NONBLOCKING {
    const double l00 = c000 * (1.0 - x) + c100 * x;
    const double l10 = c010 * (1.0 - x) + c110 * x;
    const double l01 = c001 * (1.0 - x) + c101 * x;
    const double l11 = c011 * (1.0 - x) + c111 * x;
    const double f0 = l00 * (1.0 - y) + l10 * y;
    const double f1 = l01 * (1.0 - y) + l11 * y;
    return f0 * (1.0 - z) + f1 * z;
}
}

const std::array<CubeSpec, kNumCubes>& factoryCubes() noexcept PF_NONBLOCKING { return kFactoryCubes; }

const CubeSpec& cubeAt(int index) noexcept PF_NONBLOCKING {
    return kFactoryCubes[static_cast<std::size_t>(std::clamp(index, 0, kNumCubes - 1))];
}

ResolvedLattice resolveLattice(int cubeIndex, float morphX, float morphY, float morphZ, float freqSemitones,
                               float resAmount, float transform, float stereoOffsetSt,
                               const ImprintData* imprint) noexcept PF_NONBLOCKING {
    const int ci = std::clamp(cubeIndex, 0, kNumCubes - 1);
    const CubeSpec& cube = kFactoryCubes[static_cast<std::size_t>(ci)];

    const auto corner = [&](int idx) -> const CornerSpec& {
        if (idx == 0 && ci == kNumCubes - 1 && imprint != nullptr && imprint->active) return imprint->corner;
        return cube.corners[static_cast<std::size_t>(idx)];
    };

    const double x = std::clamp(static_cast<double>(morphX), 0.0, 1.0);
    const double y = std::clamp(static_cast<double>(morphY), 0.0, 1.0);
    const double z = std::clamp(static_cast<double>(morphZ), 0.0, 1.0);

    const double freqMul = std::pow(2.0, static_cast<double>(freqSemitones + stereoOffsetSt) / 12.0);
    // Resonance scales pole Q smoothly from broad (0.28x) to razor-sharp (2.6x),
    // clamped safely well inside stability bounds.
    const double resScale = 0.28 * std::pow(9.2, std::clamp(static_cast<double>(resAmount), 0.0, 1.0));
    const double xform = std::clamp(static_cast<double>(transform), -1.0, 1.0);

    ResolvedLattice out;
    const double makeupDb = linTrilerp(corner(0).makeupDb, corner(1).makeupDb, corner(2).makeupDb, corner(3).makeupDb,
                                       corner(4).makeupDb, corner(5).makeupDb, corner(6).makeupDb, corner(7).makeupDb,
                                       x, y, z);
    // Higher resonance raises peaks across cascaded stages; gently trim makeup so
    // sweeping Resonance keeps perceived loudness controlled.
    const double resTrimDb = -5.5 * std::max(0.0, static_cast<double>(resAmount) - 0.45);
    out.makeupLinear = static_cast<float>(std::pow(10.0, (makeupDb + resTrimDb) / 20.0));

    for (std::size_t s = 0; s < kNumStages; ++s) {
        const auto pf = logTrilerp(corner(0).stages[s].poleHz, corner(1).stages[s].poleHz,
                                   corner(2).stages[s].poleHz, corner(3).stages[s].poleHz,
                                   corner(4).stages[s].poleHz, corner(5).stages[s].poleHz,
                                   corner(6).stages[s].poleHz, corner(7).stages[s].poleHz, x, y, z);
        const auto pq = logTrilerp(corner(0).stages[s].poleQ, corner(1).stages[s].poleQ,
                                   corner(2).stages[s].poleQ, corner(3).stages[s].poleQ,
                                   corner(4).stages[s].poleQ, corner(5).stages[s].poleQ,
                                   corner(6).stages[s].poleQ, corner(7).stages[s].poleQ, x, y, z);
        const auto zf = logTrilerp(corner(0).stages[s].zeroHz, corner(1).stages[s].zeroHz,
                                   corner(2).stages[s].zeroHz, corner(3).stages[s].zeroHz,
                                   corner(4).stages[s].zeroHz, corner(5).stages[s].zeroHz,
                                   corner(6).stages[s].zeroHz, corner(7).stages[s].zeroHz, x, y, z);
        const auto zq = logTrilerp(corner(0).stages[s].zeroQ, corner(1).stages[s].zeroQ,
                                   corner(2).stages[s].zeroQ, corner(3).stages[s].zeroQ,
                                   corner(4).stages[s].zeroQ, corner(5).stages[s].zeroQ,
                                   corner(6).stages[s].zeroQ, corner(7).stages[s].zeroQ, x, y, z);

        // Transform warps zero-to-pole frequency ratio (pushing zeros wider or inverting across poles).
        const double ratio = zf / std::max(pf, 20.0);
        const double warpedRatio = std::pow(ratio, 1.0 + 0.85 * xform) * std::pow(2.0, 0.35 * xform);

        const double finalPoleHz = std::clamp(pf * freqMul, 30.0, 18500.0);
        const double finalZeroHz = std::clamp(finalPoleHz * warpedRatio, 30.0, 19500.0);
        const double finalPoleQ = std::clamp(pq * resScale, 0.55, 22.0);
        const double finalZeroQ = std::clamp(zq * (0.55 + 0.75 * std::clamp(static_cast<double>(resAmount), 0.0, 1.0)), 0.55, 18.0);

        out.stages[s] = {
            static_cast<float>(finalPoleHz),
            static_cast<float>(finalPoleQ),
            static_cast<float>(finalZeroHz),
            static_cast<float>(finalZeroQ)
        };
    }
    return out;
}

float evaluateMagnitudeDb(const ResolvedLattice& lattice, int activeStages, int topology, double hz,
                          double sampleRate) noexcept {
    const int n = std::clamp(activeStages, 1, kNumStages);
    const double fs = std::max(sampleRate, 22050.0);
    const double gEval = std::tan(kPi * std::clamp(hz, 10.0, 0.48 * fs) / fs);

    const auto stageH = [&](const SectionSpec& s) -> std::complex<double> {
        const double gp = std::tan(kPi * std::clamp(static_cast<double>(s.poleHz), 20.0, 0.47 * fs) / fs);
        const double gz = std::tan(kPi * std::clamp(static_cast<double>(s.zeroHz), 20.0, 0.47 * fs) / fs);
        const double kp = 1.0 / std::clamp(static_cast<double>(s.poleQ), 0.5, 24.0);
        const double kz = 1.0 / std::clamp(static_cast<double>(s.zeroQ), 0.5, 24.0);
        const double r = gz / gp;
        // Geometric-mean normalisation (1 / r) balances DC and Nyquist shelves
        // symmetrically across 7 cascaded dipole stages without dulling high frequencies.
        const double norm = 1.0 / std::max(0.25, r);
        const std::complex<double> sNorm(0.0, gEval / gp);
        const std::complex<double> s2 = sNorm * sNorm;
        const std::complex<double> num = norm * (s2 + (r * kz) * sNorm + r * r);
        const std::complex<double> den = s2 + kp * sNorm + 1.0;
        return num / den;
    };

    std::complex<double> total(1.0, 0.0);
    if (topology == 0) {  // Serial cascade
        for (int i = 0; i < n; ++i) total *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
    } else if (topology == 2) {  // Parallel alternating-sign bank
        total = { 0.0, 0.0 };
        for (int i = 0; i < n; ++i) {
            const double sign = (i & 1) == 0 ? 1.0 : -1.0;
            total += (sign / static_cast<double>(n)) * stageH(lattice.stages[static_cast<std::size_t>(i)]);
        }
    } else {  // Hybrid: two parallel 3-4 stage branches summed
        std::complex<double> a(1.0, 0.0), b(1.0, 0.0);
        for (int i = 0; i < n; ++i) {
            if ((i & 1) == 0) a *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
            else b *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
        }
        total = 0.5 * (a + b);
    }

    const double mag = std::abs(total) * static_cast<double>(lattice.makeupLinear);
    return static_cast<float>(20.0 * std::log10(std::max(mag, 1.0e-4)));
}

ZPlanePoint stageZPoint(const SectionSpec& s, double sampleRate) noexcept {
    const double fs = std::max(sampleRate, 22050.0);
    const auto bilinearRoot = [&](double hz, double q) -> std::pair<float, float> {
        const double g = std::tan(kPi * std::clamp(hz, 20.0, 0.47 * fs) / fs);
        const double k = 1.0 / std::clamp(q, 0.5, 24.0);
        const double disc = std::max(0.0, 1.0 - 0.25 * k * k);
        // Continuous-time root scaled by g: s = g * (-k/2 + j*sqrt(1 - k^2/4))
        const std::complex<double> sc(-0.5 * g * k, g * std::sqrt(disc));
        const std::complex<double> z = (1.0 + sc) / (1.0 - sc);
        return { static_cast<float>(std::clamp(std::abs(z), 0.0, 0.999)),
                 static_cast<float>(std::clamp(std::abs(std::arg(z)), 0.0, kPi)) };
    };
    const auto [pr, pt] = bilinearRoot(s.poleHz, s.poleQ);
    const auto [zr, zt] = bilinearRoot(s.zeroHz, s.zeroQ);
    return { pr, pt, zr, zt };
}

// -----------------------------------------------------------------------------
// Offline 14th-order Burg's Method LPC + Durand-Kerner complex root finder.
// Finds the highest-energy 2048-sample analysis window, pre-emphasises gently,
// computes 14 reflection coefficients guaranteed strictly inside unit circle
// (|k_m| < 1 -> minimum-phase stable poles), and solves the degree-14 polynomial.
// -----------------------------------------------------------------------------
CornerSpec imprintFromAudio(std::span<const float> monoSamples, double sampleRate) noexcept {
    CornerSpec fallback = kFactoryCubes[kNumCubes - 1].corners[0];
    if (monoSamples.size() < 64 || sampleRate < 8000.0) return fallback;

    // Pick the loudest 2048-sample frame so leading silence never spoils the imprint.
    const std::size_t winLen = std::min<std::size_t>(2048, monoSamples.size());
    std::size_t bestStart = 0;
    double bestEnergy = 0.0;
    const std::size_t hop = std::max<std::size_t>(1, winLen / 4);
    for (std::size_t start = 0; start + winLen <= monoSamples.size(); start += hop) {
        double e = 0.0;
        for (std::size_t i = 0; i < winLen; ++i) {
            const double v = monoSamples[start + i];
            e += v * v;
        }
        if (e > bestEnergy) {
            bestEnergy = e;
            bestStart = start;
        }
    }
    if (bestEnergy < 1.0e-9) return fallback;

    // Decimate analysis signal to <= ~24 kHz so 14 poles concentrate on audible formants
    // (0..12 kHz) rather than wasting poles on ultrasonic noise.
    const int decim = sampleRate > 36000.0 ? 2 : 1;
    const double effFs = sampleRate / static_cast<double>(decim);
    const std::size_t n = winLen / static_cast<std::size_t>(decim);
    if (n < 32) return fallback;

    std::vector<double> x(n, 0.0);
    double prev = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int d = 0; d < decim; ++d) s += monoSamples[bestStart + i * static_cast<std::size_t>(decim) + static_cast<std::size_t>(d)];
        s /= static_cast<double>(decim);
        const double pre = s - 0.65 * prev;  // mild pre-emphasis reveals upper formants
        prev = s;
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        x[i] = pre * hann;
    }

    constexpr int P = 2 * kNumStages;  // order 14
    std::vector<double> ef = x;
    std::vector<double> eb = x;
    std::array<double, P + 1> a {};
    a[0] = 1.0;

    for (int m = 1; m <= P; ++m) {
        double num = 0.0, den = 0.0;
        for (std::size_t i = static_cast<std::size_t>(m); i < n; ++i) {
            num += ef[i] * eb[i - 1];
            den += ef[i] * ef[i] + eb[i - 1] * eb[i - 1];
        }
        if (den < 1.0e-18) break;
        const double km = std::clamp(-2.0 * num / den, -0.995, 0.995);

        std::array<double, P + 1> nextA = a;
        for (int j = 1; j < m; ++j)
            nextA[static_cast<std::size_t>(j)] = a[static_cast<std::size_t>(j)] + km * a[static_cast<std::size_t>(m - j)];
        nextA[static_cast<std::size_t>(m)] = km;
        a = nextA;

        for (std::size_t i = n - 1; i >= static_cast<std::size_t>(m); --i) {
            const double f = ef[i] + km * eb[i - 1];
            const double b = eb[i - 1] + km * ef[i];
            ef[i] = f;
            eb[i] = b;
        }
    }

    // Solve z^P + a[1]*z^(P-1) + ... + a[P] = 0 via Durand-Kerner iteration.
    using C = std::complex<double>;
    std::array<C, P> roots {};
    for (int i = 0; i < P; ++i) {
        const double angle = 2.0 * kPi * (static_cast<double>(i) + 0.37) / static_cast<double>(P);
        roots[static_cast<std::size_t>(i)] = std::polar(0.82, angle);
    }
    const auto evalPoly = [&](C z) -> C {
        C v(1.0, 0.0);
        for (int j = 1; j <= P; ++j) v = v * z + a[static_cast<std::size_t>(j)];
        return v;
    };
    for (int iter = 0; iter < 96; ++iter) {
        double maxStep = 0.0;
        for (int i = 0; i < P; ++i) {
            const C zi = roots[static_cast<std::size_t>(i)];
            C denom(1.0, 0.0);
            for (int j = 0; j < P; ++j) {
                if (i == j) continue;
                denom *= (zi - roots[static_cast<std::size_t>(j)]);
            }
            if (std::abs(denom) < 1.0e-18) continue;
            const C delta = evalPoly(zi) / denom;
            roots[static_cast<std::size_t>(i)] -= delta;
            maxStep = std::max(maxStep, std::abs(delta));
        }
        if (maxStep < 1.0e-9) break;
    }

    // Extract upper-half-plane roots (im >= 0) and sort by pole frequency.
    struct Extracted {
        double hz;
        double q;
    };
    std::vector<Extracted> poles;
    poles.reserve(P);
    for (const C& r : roots) {
        if (r.imag() < -1.0e-5) continue;
        const double radius = std::clamp(std::abs(r), 0.25, 0.992);
        const double theta = std::clamp(std::abs(std::arg(r)), 0.008, kPi * 0.96);
        const double hz = std::clamp(theta * effFs / (2.0 * kPi), 55.0, 14000.0);
        const double bw = std::max(18.0, -std::log(radius) * effFs / kPi);
        const double q = std::clamp(hz / bw, 1.2, 12.0);
        // Deduplicate near-identical real-axis roots
        bool duplicate = false;
        for (const auto& existing : poles) {
            if (std::abs(existing.hz - hz) < 12.0 && std::abs(r.imag()) < 1.0e-3) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) poles.push_back({ hz, q });
    }

    if (poles.empty()) return fallback;
    std::sort(poles.begin(), poles.end(), [](const Extracted& u, const Extracted& v) { return u.hz < v.hz; });

    // Fill exactly 7 stages, padding gracefully from fallback if fewer than 7 conjugate pairs appeared.
    CornerSpec result = fallback;
    result.makeupDb = -4.5f;
    for (int i = 0; i < kNumStages; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const Extracted& p = poles[static_cast<std::size_t>(std::min<int>(i, static_cast<int>(poles.size()) - 1))];
        // Place dipole zero midway geometrically toward next formant so imprinted audio
        // gets crisp anti-formant valleys between resonant peaks.
        double zeroHz = p.hz * 1.28;
        if (i + 1 < static_cast<int>(poles.size())) {
            const double nextHz = poles[static_cast<std::size_t>(i + 1)].hz;
            if (nextHz > p.hz * 1.08) zeroHz = std::sqrt(p.hz * nextHz);
        }
        result.stages[idx] = {
            static_cast<float>(std::clamp(p.hz, 55.0, 14000.0)),
            static_cast<float>(std::clamp(p.q, 1.4, 11.0)),
            static_cast<float>(std::clamp(zeroHz, 70.0, 16500.0)),
            static_cast<float>(std::clamp(p.q * 0.78, 1.4, 9.0))
        };
    }
    return result;
}

}
