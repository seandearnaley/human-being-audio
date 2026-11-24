// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from Livecut, Copyright 2003-2004 Remy Muller.

#include "engine/Effects.h"

#include <hba/dsp/limiter.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace livecut::engine
{

void BitCrusher::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    clear();
    startCut(0, 1);
}

void BitCrusher::clear() noexcept HBA_NONBLOCKING {
    phase = 0.0;
    heldLeft = heldRight = 0.0f;
}

void BitCrusher::setBlock(double newStartBits, double newEndBits, double newStartHz,
                          double newEndHz) noexcept HBA_NONBLOCKING {
    startBits = newStartBits;
    endBits = newEndBits;
    startHz = newStartHz;
    endHz = newEndHz;
}

void BitCrusher::startCut(std::size_t cut, std::size_t cuts) noexcept HBA_NONBLOCKING {
    currentBits = sweepSetting (startBits, endBits, cut, cuts);

    // As BBCut's CutBit1: n bits divide full scale (-1 to 1) into steps of 2^(1 - n).
    // A float sample only carries 24 bits, so finer steps would just add rounding error.
    step = currentBits < 24.0 ? std::pow (0.5, currentBits - 1.0) : 0.0;

    hold = std::max (sampleRate / sweepSetting (startHz, endHz, cut, cuts), 1.0);
}

void BitCrusher::process(float& left, float& right) noexcept HBA_NONBLOCKING {
    // Sample and hold: take a new sample every `hold` samples, carrying the fraction over.
    if (phase <= 0.0)
    {
        const auto quantise = [this] (float x)
        {
            return step > 0.0 ? static_cast<float> (std::round (x / step) * step) : x;
        };

        heldLeft = quantise (left);
        heldRight = quantise (right);
        phase += hold;
    }

    phase -= 1.0;
    left = heldLeft;
    right = heldRight;
}

void Comb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const auto length = static_cast<std::size_t>(std::ceil(maxDelayMs / 1000.0 * sampleRate)) + 4;
    lineLeft.assign (length, 0.0f);
    lineRight.assign (length, 0.0f);
    write = 0;

    // Smooth comb delay (feedforward and feedback) so cut transitions never click.
    smoothing = std::exp (std::log (1.0 - 0.66) / (0.040 * sampleRate));
    smootherPrimed = false;
}

void Comb::clear() noexcept HBA_NONBLOCKING {
    std::fill(lineLeft.begin(), lineLeft.end(), 0.0f);
    std::fill(lineRight.begin(), lineRight.end(), 0.0f);
    write = 0;
    smootherPrimed = false;
}

void Comb::setBlock(const CombParameters& params, double newStartMs, double newEndMs) noexcept HBA_NONBLOCKING {
    type = params.type;
    feedback = params.feedback;
    startMs = newStartMs;
    endMs = newEndMs;
}

void Comb::startCut(std::size_t cut, std::size_t cuts) noexcept HBA_NONBLOCKING {
    // The parameter range keeps delays within the line; clamping keeps reads inside it regardless.
    const auto maxDelay = static_cast<double>(lineLeft.size() - 4);
    const auto delay = sweepSetting (startMs, endMs, cut, cuts) / 1000.0 * sampleRate;
    assert (delay >= 1.0 && delay <= maxDelay);
    targetDelay = std::clamp (delay, 1.0, maxDelay);

    if (! smootherPrimed)
    {
        smoothedDelay = targetDelay;
        smootherPrimed = true;
    }
}

float Comb::read(const std::vector<float>& line, double delay) const noexcept HBA_NONBLOCKING {
    // 4-point Catmull-Rom cubic fractional delay interpolation for clean swept combs.
    const auto n = line.size();
    const auto size = static_cast<double>(n);
    auto position = static_cast<double> (write) - delay;

    if (position < 0.0)
        position += size;

    const auto i1 = static_cast<std::size_t>(position) % n;
    const auto fraction = static_cast<float>(position - std::floor(position));
    if (fraction == 0.0f)
        return line[i1];

    const auto i0 = i1 == 0 ? n - 1 : i1 - 1;
    const auto i2 = (i1 + 1) % n;
    const auto i3 = (i2 + 1) % n;
    const float p0 = line[i0];
    const float p1 = line[i1];
    const float p2 = line[i2];
    const float p3 = line[i3];
    const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float a1 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float a2 = -0.5f * p0 + 0.5f * p2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + p1;
}

void Comb::process(float& left, float& right) noexcept HBA_NONBLOCKING {
    smoothedDelay = smoothing * smoothedDelay + (1.0 - smoothing) * targetDelay;
    if (type == CombParameters::Type::feedForward)
    {
        const auto delayedLeft = read(lineLeft, smoothedDelay);
        const auto delayedRight = read(lineRight, smoothedDelay);
        lineLeft[write] = left;
        lineRight[write] = right;
        left = 0.5f * (left + delayedLeft);
        right = 0.5f * (right + delayedRight);
    } else {
        const auto delayedLeft = read (lineLeft, smoothedDelay);
        const auto delayedRight = read (lineRight, smoothedDelay);
        // C^2-smooth soft saturation (identity <= 0.80, asymptotes strictly to 1.0)
        // eliminates legacy hard-clamp metallic buzz while staying bounded <= full scale.
        left = static_cast<float>(
            hba::dsp::softClip(static_cast<double>((0.99f - feedback) * left + feedback * delayedLeft), 0.80, 1.0));
        right = static_cast<float>(
            hba::dsp::softClip(static_cast<double>((0.99f - feedback) * right + feedback * delayedRight), 0.80, 1.0));
        lineLeft[write] = left;
        lineRight[write] = right;
    }

    write = (write + 1) % lineLeft.size();
}

} // namespace livecut::engine
