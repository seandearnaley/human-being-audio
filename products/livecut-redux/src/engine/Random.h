// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hba/dsp/nonblocking.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>

namespace livecut::engine
{

// Returns the index a draw in [0, 1) lands on when each index owns a slice of
// that range proportional to its weight. Weights must be positive.
std::size_t pickWeighted(std::span<const double> weights, double unitDraw) noexcept HBA_NONBLOCKING;

// Each cutter owns one of these, so cutting never touches the C library's
// process-wide rand() state. The standard distributions are not specified
// bit-for-bit, so draws are derived from the engine's output directly: a seed
// then cuts the same way on every platform.
class Random
{
public:
  explicit Random(std::uint32_t seed = 1) noexcept;

  void reseed(std::uint32_t seed) noexcept HBA_NONBLOCKING;

  // A value in [0, 1).
  double unit() noexcept HBA_NONBLOCKING;

  // A value between a and b that never reaches b.
  double uniform(double a, double b) noexcept HBA_NONBLOCKING;

  // Any integer between a and b inclusive, each equally likely. The bounds may come in either order.
  int integer(int a, int b) noexcept HBA_NONBLOCKING;

  bool chance(double probability) noexcept HBA_NONBLOCKING;

  std::size_t weighted(std::span<const double> weights) noexcept HBA_NONBLOCKING;

private:
    std::mt19937 engine;
};

} // namespace livecut::engine
