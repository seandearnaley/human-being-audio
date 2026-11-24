// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hba/dsp/nonblocking.h"

#include <cstddef>
#include <vector>

namespace livecut::engine
{

// Both effects choose a random start and end setting for each block and sweep
// between them across the block's cuts, as Livecut does.
inline double sweepSetting(double start, double end, std::size_t cut, std::size_t cuts) noexcept HBA_NONBLOCKING {
    return start + (end - start) * static_cast<double> (cut) / static_cast<double> (cuts);
}

struct CrusherParameters
{
    bool enabled = false;
    float minBits = 32.0f;
    float maxBits = 32.0f;
    double minHz = 192000.0; // sample-and-hold rate; at or above the sample rate it does nothing
    double maxHz = 192000.0;
};

// Reduces bit depth and sample rate, after BBCut's CutBit1.
class BitCrusher
{
public:
    void prepare (double sampleRate);
    void clear() noexcept HBA_NONBLOCKING;
    void setBlock(double startBits, double endBits, double startHz, double endHz) noexcept HBA_NONBLOCKING;
    void startCut(std::size_t cut, std::size_t cuts) noexcept HBA_NONBLOCKING;
    void process(float& left, float& right) noexcept HBA_NONBLOCKING;

    double bits() const noexcept HBA_NONBLOCKING { return currentBits; }
    double holdSamples() const noexcept HBA_NONBLOCKING { return hold; }

  private:
    double sampleRate = 48000.0;
    double startBits = 32.0, endBits = 32.0, startHz = 192000.0, endHz = 192000.0;
    double currentBits = 32.0;
    double step = 0.0;
    double hold = 1.0;
    double phase = 0.0;
    float heldLeft = 0.0f, heldRight = 0.0f;
};

struct CombParameters
{
    enum class Type
    {
        feedForward,
        feedBack
    };

    bool enabled = false;
    Type type = Type::feedForward;
    float feedback = 0.5f;
    float minDelayMs = 10.0f;
    float maxDelayMs = 10.0f;
};

// A comb filter whose delay follows the cuts.
class Comb
{
public:
    static constexpr double maxDelayMs = 50.0;

    void prepare (double sampleRate);
    void clear() noexcept HBA_NONBLOCKING;
    void setBlock(const CombParameters& params, double startMs, double endMs) noexcept HBA_NONBLOCKING;
    void startCut(std::size_t cut, std::size_t cuts) noexcept HBA_NONBLOCKING;
    void process(float& left, float& right) noexcept HBA_NONBLOCKING;

    double delaySamples() const noexcept HBA_NONBLOCKING { return targetDelay; }

  private:
    float read(const std::vector<float>& line, double delay) const noexcept HBA_NONBLOCKING;

    double sampleRate = 48000.0;
    CombParameters::Type type = CombParameters::Type::feedForward;
    float feedback = 0.5f;
    double startMs = 10.0, endMs = 10.0;
    double targetDelay = 1.0;
    double smoothedDelay = 1.0;
    double smoothing = 0.0;
    bool smootherPrimed = false;
    std::vector<float> lineLeft, lineRight;
    std::size_t write = 0;
};

} // namespace livecut::engine
