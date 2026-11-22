// 7-stage (14-pole + 14-zero) TPT Cytomic Pole-Zero SVF Lattice.
// Each section realizes H(s) = G * (s^2 + r*kz*s + r^2) / (s^2 + kp*s + 1)
// via y = m0*v0 + m1*v1 + m2*v2, staying strictly BIBO-stable and artifact-free
// even under audio-rate X/Y/Z cube sweeps and pitch modulation.
#pragma once

#include "fold.h"
#include "pf/cubes.h"
#include "svf.h"

#include <array>
#include <cmath>

namespace pf::dsp {

struct PoleZeroCoefs {
    SvfCoefs svf {};
    double m0 = 1.0;
    double m1 = 0.0;
    double m2 = 0.0;

    static PoleZeroCoefs make(const SectionSpec& s, double sampleRate) noexcept PF_NONBLOCKING {
        const double gp = SvfCoefs::prewarp(static_cast<double>(s.poleHz), sampleRate);
        const double gz = SvfCoefs::prewarp(static_cast<double>(s.zeroHz), sampleRate);
        const double kp = 1.0 / std::clamp(static_cast<double>(s.poleQ), 0.5, 24.0);
        const double kz = 1.0 / std::clamp(static_cast<double>(s.zeroQ), 0.5, 24.0);
        const double r = gz / gp;
        const double norm = 1.0 / std::max(0.25, r);

        PoleZeroCoefs c;
        c.svf = SvfCoefs::fromGK(gp, kp);
        c.m0 = norm;
        c.m1 = norm * (r * kz - kp);
        c.m2 = norm * (r * r - 1.0);
        return c;
    }
};

class PoleZeroLattice {
public:
    void prepare(double osSampleRate) noexcept PF_NONBLOCKING {
        for (auto& sat : sat_) sat.prepare(osSampleRate);
        exitDc_.prepare(10.0, osSampleRate);
        reset();
    }

    void reset() noexcept PF_NONBLOCKING {
        for (auto& st : states_) st.reset();
        for (auto& sat : sat_) sat.reset();
        exitDc_.reset();
    }

    double process(double x, const std::array<PoleZeroCoefs, kNumStages>& coefs, int activeStages,
                   Topology topology, double makeup, double drive01, double asym01, FoldType foldType,
                   DrivePos drivePos) noexcept PF_NONBLOCKING {
        const int n = std::clamp(activeStages, 1, kNumStages);

        if (drivePos == DrivePos::pre)
            x = sat_[0].process(x, drive01, asym01, foldType);

        const auto tickStage = [&](std::size_t i, double in) noexcept PF_NONBLOCKING -> double {
            const auto& c = coefs[i];
            double v1, v2;
            states_[i].tick(c.svf, in, v1, v2);
            const double y = c.m0 * in + c.m1 * v1 + c.m2 * v2;
            // C^2-smooth internal headroom guard prevents 7 stacked high-Q resonant
            // peaks from blowing up numerically while remaining exact identity below ±2.5.
            return hba::dsp::softGuard(y, 2.5, 4.0);
        };

        double out = 0.0;
        if (topology == Topology::serial) {
            double sig = x;
            for (int i = 0; i < n; ++i) {
                sig = tickStage(static_cast<std::size_t>(i), sig);
                if (drivePos == DrivePos::inter && (i == 1 || i == 4)) {
                    const std::size_t satIdx = i == 1 ? 0 : 1;
                    sig = sat_[satIdx].process(sig, drive01 * 0.78, asym01, foldType);
                }
            }
            out = sig;
        } else if (topology == Topology::parallel) {
            double evenSum = 0.0;
            double oddSum = 0.0;
            const double invN = 1.0 / static_cast<double>(n);
            for (int i = 0; i < n; ++i) {
                const double band = tickStage(static_cast<std::size_t>(i), x);
                if ((i & 1) == 0) evenSum += band;
                else oddSum += band;
            }
            evenSum *= invN;
            oddSum *= invN;
            if (drivePos == DrivePos::inter) {
                evenSum = sat_[0].process(evenSum, drive01 * 0.85, asym01, foldType);
                oddSum = sat_[1].process(oddSum, drive01 * 0.85, asym01, foldType);
            }
            out = evenSum - oddSum;
        } else {
            // Hybrid: interleaved odd/even twin cascades summed in parallel.
            const int satStageA = n >= 4 ? 2 : 0;
            const int satStageB = n >= 4 ? 3 : 1;
            double branchA = x;
            double branchB = x;
            for (int i = 0; i < n; ++i) {
                if ((i & 1) == 0) {
                    branchA = tickStage(static_cast<std::size_t>(i), branchA);
                    if (drivePos == DrivePos::inter && i == satStageA)
                        branchA = sat_[0].process(branchA, drive01 * 0.8, asym01, foldType);
                } else {
                    branchB = tickStage(static_cast<std::size_t>(i), branchB);
                    if (drivePos == DrivePos::inter && i == satStageB)
                        branchB = sat_[1].process(branchB, drive01 * 0.8, asym01, foldType);
                }
            }
            out = 0.5 * (branchA + branchB);
        }

        out *= makeup;

        if (drivePos == DrivePos::post)
            out = sat_[0].process(out, drive01, asym01, foldType);

        return exitDc_.process(out);
    }

private:
    std::array<SvfState, kNumStages> states_ {};
    std::array<Saturator, 2> sat_ {};
    DcBlocker exitDc_;
};

}
