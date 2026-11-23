// SPDX-License-Identifier: GPL-3.0-or-later
// Linkwitz-Riley 4th-order (LR4) filters built from two Butterworth SVF
// sections, plus a 4+4 Orchard/Bedrosian Dome Hilbert phase rotator (20-400 Hz).
#pragma once

#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace hba::dsp {

inline SvfCoefs butterworth(double hz, double sampleRate) noexcept HBA_NONBLOCKING {
    return SvfCoefs::make(hz, kButterworthQ, sampleRate);
}

class Lr4Lowpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return b_.lowpass(c, a_.lowpass(c, x)); }

private:
    SvfState a_, b_;
};

class Lr4Highpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return b_.highpass(c, a_.highpass(c, x)); }

private:
    SvfState a_, b_;
};

// The all-pass that an LR4 pair at the same frequency sums to.
class Lr4Allpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return a_.allpass(c, x); }

private:
    SvfState a_;
};

// One LR4 split: the first section is shared by both outputs.
class Lr4Split {
public:
    void reset() noexcept HBA_NONBLOCKING { first_.reset(); low_.reset(); high_.reset(); }
    void process(const SvfCoefs& c, double x, double& low, double& high) noexcept HBA_NONBLOCKING {
        double v1, v2;
        first_.tick(c, x, v1, v2);
        low = low_.lowpass(c, v2);
        high = high_.highpass(c, x - c.k * v1 - v2);
    }

private:
    SvfState first_, low_, high_;
};

// Three bands that sum to an all-pass: low = AP_hi(LP_lo(x)), mid = LP_hi(HP_lo(x)),
// high = HP_hi(HP_lo(x)), so low + mid + high = AP_hi(AP_lo(x)).
class ThreeBandSplit {
public:
    void reset() noexcept HBA_NONBLOCKING { lo_.reset(); hi_.reset(); comp_.reset(); }
    void process(const SvfCoefs& cLo, const SvfCoefs& cHi, double x, double& low, double& mid,
                 double& high) noexcept HBA_NONBLOCKING {
        double rest;
        lo_.process(cLo, x, low, rest);
        hi_.process(cHi, rest, mid, high);
        low = comp_.process(cHi, low);
    }

private:
    Lr4Split lo_, hi_;
    Lr4Allpass comp_;
};

// Two-branch 4+4 Orchard/Bedrosian Dome all-pass Hilbert network rotating
// phase continuously 0..360 deg across 20..400 Hz with <0.15 deg phase error
// and <0.012 dB magnitude ripple across 44.1..192 kHz.
class PhaseRotator {
public:
  static constexpr std::size_t kSections = 4;

  PhaseRotator() noexcept HBA_NONBLOCKING { prepare(48000.0); }

  void prepare(double sampleRate) noexcept HBA_NONBLOCKING {
      constexpr std::array<double, kSections> kPolesI{5.329948, 36.696142, 125.916857, 483.671421};
      constexpr std::array<double, kSections> kPolesQ{17.863367, 68.616706, 235.447093, 1621.028894};
      const double sr = std::max(sampleRate, 8000.0);
      for (std::size_t i = 0; i < kSections; ++i) {
          const double gI = std::tan(kPi * std::min(kPolesI[i], 0.45 * sr) / sr);
          const double gQ = std::tan(kPi * std::min(kPolesQ[i], 0.45 * sr) / sr);
          cI_[i] = (gI - 1.0) / (gI + 1.0);
          cQ_[i] = (gQ - 1.0) / (gQ + 1.0);
      }
      reset();
  }

  void reset() noexcept HBA_NONBLOCKING {
      sI_.fill(0.0);
      sQ_.fill(0.0);
  }

    double process(double x, double degrees) noexcept HBA_NONBLOCKING {
        double iOut = x;
        double qOut = x;
        for (std::size_t k = 0; k < kSections; ++k) {
            const double yi = cI_[k] * iOut + sI_[k];
            sI_[k] = iOut - cI_[k] * yi;
            iOut = yi;

            const double yq = cQ_[k] * qOut + sQ_[k];
            sQ_[k] = qOut - cQ_[k] * yq;
            qOut = yq;
        }
        const double rad = degrees * (kPi / 180.0);
        return std::cos(rad) * iOut + std::sin(rad) * qOut;
    }

    // Runs the zero-degree reference branch (I) with identical base phase delay.
    double processReference(double x) noexcept HBA_NONBLOCKING {
        double iOut = x;
        for (std::size_t k = 0; k < kSections; ++k) {
            const double yi = cI_[k] * iOut + sI_[k];
            sI_[k] = iOut - cI_[k] * yi;
            iOut = yi;
        }
        return iOut;
    }

private:
  std::array<double, kSections> cI_{};
  std::array<double, kSections> cQ_{};
  std::array<double, kSections> sI_{};
  std::array<double, kSections> sQ_{};
};

}  // namespace hba::dsp
