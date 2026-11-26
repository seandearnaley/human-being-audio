// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/ImpactEffects.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <numbers>
#include <optional>

namespace livecut::engine
{
namespace
{
using namespace detail;
constexpr double pi = std::numbers::pi;
constexpr double log1000 = 6.907755278982137;
constexpr double semitone = 1.0594630943592953; // 2^(1/12)
constexpr double makeupCeiling = 2.0; // +6 dB
constexpr double makeupHighestHz = 18000.0;
// A sweep's makeup never lifts, and a tone on its resonant peak gains at most
// +6 dB: lifting a dark sweep to pink noise's level raised bass-heavy
// material 5 dB, and a bass note on a resonant cutoff 8 dB.
constexpr double sweepMakeupCeiling = 1.0, sweepPeakCeiling = 2.0;
constexpr double tailDuckFloor = 0.25; // -12 dB
constexpr double tailSilence = 1.0e-6; // -120 dBFS

// The band's pink-noise power, tabulated over its width w (octaves) and log2 q.
// The widest band the clamps allow is log2 (0.45 * 384 kHz / 30 Hz) = 12.49
// octaves; the narrowest is the semitone floor. Bilinear interpolation of
// ln P on 1/16 octave by 1/8 octave of q is within 0.05 dB of the integral
// across the whole table (worst near w 0.1, q 7); the gain halves that.
constexpr double tableWidthMin = 1.0 / 12.0, tableWidthStep = 1.0 / 16.0;
constexpr std::size_t tableWidths = 200; // 1/12 to 12.52 octaves
constexpr double tableLogQMin = -1.0, tableLogQStep = 1.0 / 8.0;
constexpr std::size_t tableQs = 33; // q 0.5 to 8

// The analogue prototypes the SVFs approximate: a high-pass at 1 and a
// low-pass at 2^width, both with this q. Each edge's tail falls 12 dB an
// octave, so 6 octaves beyond the edges leave out under 1e-6 dB; 1/16-octave
// trapezoids resolve q 8's resonance (about 0.18 octaves wide) to 0.002 dB.
double integratePinkPower (double width, double lowQ, double highQ, double step, double margin)
{
    const auto lowDamping = 1.0 / (lowQ * lowQ), highDamping = 1.0 / (highQ * highQ);
    const auto scale = std::exp2 (-2.0 * width);
    const auto count = static_cast<int> (std::ceil ((width + 2.0 * margin) / step));
    auto sum = 0.0;
    for (int i = 0; i <= count; ++i)
    {
        const auto x = std::exp2 (2.0 * (i * step - margin)); // squared frequency at the low edge
        const auto y = x * scale; // and at the high edge
        const auto highPass = x * x / ((1.0 - x) * (1.0 - x) + lowDamping * x);
        const auto lowPass = 1.0 / ((1.0 - y) * (1.0 - y) + highDamping * y);
        sum += (i == 0 || i == count ? 0.5 : 1.0) * highPass * lowPass;
    }
    return sum * step;
}
} // namespace

namespace detail {
using namespace detail;
// The low edge's q is the high edge's for a band, and fixed for a sweep.
struct PinkPowerTable
{
    explicit PinkPowerTable (std::optional<double> fixedLowQ)
    {
        for (std::size_t i = 0; i < tableWidths; ++i)
            for (std::size_t j = 0; j < tableQs; ++j)
            {
                const auto q = std::exp2 (tableLogQMin + static_cast<double> (j) * tableLogQStep);
                logPower[i * tableQs + j] = std::log (integratePinkPower (tableWidthMin + static_cast<double> (i) * tableWidthStep,
                                                                          fixedLowQ.value_or (q), q, bandPowerStep, bandPowerMargin));
            }
    }

    double interpolate(double width, double q) const noexcept HBA_NONBLOCKING {
        const auto u = std::clamp ((width - tableWidthMin) / tableWidthStep, 0.0, static_cast<double> (tableWidths - 1));
        const auto v = std::clamp ((std::log2 (q) - tableLogQMin) / tableLogQStep, 0.0, static_cast<double> (tableQs - 1));
        const auto i = std::min (static_cast<std::size_t> (u), tableWidths - 2);
        const auto j = std::min (static_cast<std::size_t> (v), tableQs - 2);
        const auto a = u - static_cast<double> (i), b = v - static_cast<double> (j);
        const auto at = [this](std::size_t wi, std::size_t qi) noexcept { return logPower[wi * tableQs + qi]; };
        return std::lerp (std::lerp (at (i, j), at (i, j + 1), b), std::lerp (at (i + 1, j), at (i + 1, j + 1), b), a);
    }

    std::array<double, tableWidths * tableQs> logPower {};
    double logOpen = std::log (integratePinkPower (bandOpenOctaves, bandOpenQ, bandOpenQ, bandPowerStep, bandPowerMargin));
};
} // namespace detail

namespace {
// Built once, by the first prepare(), never on the audio thread.
const detail::PinkPowerTable& pinkPowerTable() {
    static const detail::PinkPowerTable table{std::nullopt};
    return table;
}

const detail::PinkPowerTable& sweepPowerTable() {
    static const detail::PinkPowerTable table{sweepLowQ};
    return table;
}

// SplitMix64's finaliser, used as a stateless hash rather than an RNG stream.
std::uint64_t hash(std::uint64_t x) noexcept HBA_NONBLOCKING {
    x = (x ^ (x >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C (0x94d049bb133111eb);
    return x ^ (x >> 31);
}

std::uint64_t identityHash(const CutIdentity& id) noexcept HBA_NONBLOCKING {
    auto result = hash (id.seed + UINT64_C (0x9e3779b97f4a7c15));
    const auto add = [&result](std::uint64_t value) noexcept { result = hash(result ^ hash(value)); };
    add (static_cast<std::uint64_t> (id.phraseBar));
    add (std::bit_cast<std::uint64_t> (id.blockStartUnits == 0.0 ? 0.0 : id.blockStartUnits));
    add (std::bit_cast<std::uint64_t> (id.cutStartUnits == 0.0 ? 0.0 : id.cutStartUnits));
    add (id.cutIndex);
    add (id.cutCount);
    return result;
}

bool choose(std::uint64_t identity, std::uint64_t tag, bool enabled, float chance) noexcept HBA_NONBLOCKING {
    if (! enabled || chance <= 0.0f)
        return false;
    if (chance >= 1.0f)
        return true;
    const auto draw = static_cast<double> (hash (identity ^ tag) >> 11) * 0x1.0p-53;
    return draw < chance;
}

double gain(double db) noexcept HBA_NONBLOCKING { return hba::dsp::dbToGain(db); }

double cleanState(double x) noexcept HBA_NONBLOCKING { return std::abs(x) < 1.0e-30 ? 0.0 : x; }

// The analogue two-pole low-pass's resonant peak, which it has above q 1/sqrt 2.
double lowPassPeak(double q) noexcept HBA_NONBLOCKING {
    return q > 1.0 / std::numbers::sqrt2 ? q / std::sqrt (1.0 - 1.0 / (4.0 * q * q)) : 1.0;
}

// The engine-only types: smoothed, level-held and held while they fade.
bool smoothed(ImpactParameters::Filter::Type type) noexcept HBA_NONBLOCKING {
    return type == ImpactParameters::Filter::Type::band || type == ImpactParameters::Filter::Type::sweep;
}
} // namespace

namespace detail
{
double bandPinkPower (double widthOctaves, double q)
{
    return std::exp (pinkPowerTable().interpolate (widthOctaves, q));
}

double integrateBandPinkPower (double widthOctaves, double q, double stepOctaves)
{
    return integratePinkPower (widthOctaves, q, q, stepOctaves, bandPowerMargin);
}

double sweepPinkPower (double widthOctaves, double q)
{
    return std::exp (sweepPowerTable().interpolate (widthOctaves, q));
}

double integrateSweepPinkPower (double widthOctaves, double q, double stepOctaves)
{
    return integratePinkPower (widthOctaves, sweepLowQ, q, stepOctaves, bandPowerMargin);
}

double tailNoteHz(double cutoffHz) noexcept HBA_NONBLOCKING {
    auto semitones = std::lround (12.0 * std::log2 (cutoffHz / 440.0));
    while (semitones < -9) // C4
        semitones += 12;
    while (semitones >= 15) // C6
        semitones -= 12;
    return 440.0 * std::exp2 (static_cast<double> (semitones) / 12.0);
}
} // namespace detail

void ImpactEffects::Ramp::setTarget(double value, std::uint32_t samples) noexcept HBA_NONBLOCKING {
    if (value == target)
        return;
    target = value;
    remaining = samples;
    increment = (target - current) / samples;
}

double ImpactEffects::Ramp::next() noexcept HBA_NONBLOCKING {
    if (remaining > 0)
        current = --remaining == 0 ? target : current + increment;
    return current;
}

void ImpactEffects::prepare (double newSampleRate)
{
    assert (std::isfinite (newSampleRate) && newSampleRate >= 1000.0 && newSampleRate <= 384000.0);
    sampleRate = newSampleRate;
    rampSamples = static_cast<std::uint32_t> (std::ceil (0.002 * sampleRate));
    dcPole = std::exp (-2.0 * pi * 5.0 / sampleRate);
    smoothingPole = std::exp (-1.0 / (0.020 * sampleRate));
    tailRecoveryPole = std::exp (-1.0 / (0.040 * sampleRate));
    bandPowerTable = &pinkPowerTable(); // Builds and caches the makeup tables off the audio thread.
    sweepPowerTableData = &sweepPowerTable();
    reset();
    setParameters (parameters);
}

void ImpactEffects::reset() noexcept HBA_NONBLOCKING {
    state = {};
    wet = {};
    adoptFilter();
    filterState = {};
    driveState = {};
    resetBand();
    fastEnvelope = slowEnvelope = adEnvelope = 0.0;
    attackPosition = 0;
}

// Clears everything the band runs on, including the high-pass stage it shares
// with the single-SVF types.
void ImpactEffects::resetBand() noexcept HBA_NONBLOCKING {
    filterState = {};
    bandState = {};
    bandPrimed = false;
    tail = {};
    state.bandLowHz = state.bandHighHz = 0.0;
    state.levelHoldGain = 1.0;
    state.tailHz = state.tailAmplitude = 0.0;
    state.tailDuck = 1.0;
}

void ImpactEffects::adoptFilter() noexcept HBA_NONBLOCKING {
    // A band or sweep switched on never inherits classic or another type's state.
    if (smoothed (requestedFilter.type) && requestedFilter.type != parameters.filter.type)
        resetBand();
    parameters.filter = requestedFilter;
    filterHeld = filterDetached = false;
    if (requestedFilter.type == ImpactParameters::Filter::Type::sweep)
    {
        bandLowTarget = std::log2 (sweepLowHz);
        bandHighTarget = std::log2 (static_cast<double> (requestedFilter.cutoffHz));
        return;
    }
    bandLowTarget = std::log2 (static_cast<double> (requestedFilter.bandLowHz));
    bandHighTarget = std::log2 (static_cast<double> (requestedFilter.bandHighHz));
}

void ImpactEffects::setParameters(const ImpactParameters& next) noexcept HBA_NONBLOCKING {
    if (next.drive.mode != parameters.drive.mode)
        for (auto& channel : driveState)
            channel.primed = false;
    requestedFilter = next.filter;
    auto& f = requestedFilter;
    f.chance = std::clamp (f.chance, 0.0f, 1.0f);
    f.cutoffHz = std::clamp (f.cutoffHz, 30.0f, 18000.0f);
    f.q = std::clamp (f.q, 0.5f, 8.0f);
    f.envelopeOctaves = std::clamp (f.envelopeOctaves, -6.0f, 6.0f);
    f.repeatOctaves = std::clamp (f.repeatOctaves, -4.0f, 4.0f);
    f.bandLowHz = std::clamp (f.bandLowHz, 30.0f, 18000.0f);
    f.bandHighHz = std::clamp (f.bandHighHz, 30.0f, 18000.0f);
    f.tailLevel = std::clamp (f.tailLevel, 0.0f, 1.0f);
    f.tailDecayMs = std::clamp (f.tailDecayMs, 50.0f, 2000.0f);
    f.mix = std::clamp (f.mix, 0.0f, 1.0f);
    const auto filter = parameters.filter;
    parameters = next;
    parameters.filter = filter;
    // A band or sweep switched off, or switched to or from another type, while
    // it still sounds keeps its last shape, its breathing included, until its
    // wet ramps out at the next cut; it never changes shape while audible. The
    // new filter comes in on a later cut. Classic types, which never involve a
    // band or sweep, adopt the new parameters at once, as they always have.
    const auto sounding = parameters.filter.type;
    const auto audible = wet[0].current != 0.0 || wet[0].target != 0.0;
    if (audible && (smoothed (sounding) || smoothed (f.type)) && (! f.enabled || f.type != sounding))
    {
        if (! filterHeld)
            heldDecayMultiplier = decayMultiplier;
        filterHeld = true;
        parameters.filter.enabled = false;
    }
    else
        adoptFilter();
    auto& d = parameters.drive;
    d.chance = std::clamp (d.chance, 0.0f, 1.0f);
    d.driveDb = std::clamp (d.driveDb, 0.0f, 30.0f);
    d.envelopeDb = std::clamp (d.envelopeDb, -24.0f, 24.0f);
    d.repeatDb = std::clamp (d.repeatDb, -24.0f, 24.0f);
    d.mix = std::clamp (d.mix, 0.0f, 1.0f);
    auto& t = parameters.transient;
    t.chance = std::clamp (t.chance, 0.0f, 1.0f);
    t.attackDb = std::clamp (t.attackDb, -18.0f, 18.0f);
    t.sustainDb = std::clamp (t.sustainDb, -18.0f, 18.0f);
    t.timeMs = std::clamp (t.timeMs, 10.0f, 200.0f);
    t.mix = std::clamp (t.mix, 0.0f, 1.0f);
    parameters.attackMs = std::clamp (parameters.attackMs, 0.0f, 25.0f);
    parameters.decayMs = std::clamp (parameters.decayMs, 5.0f, 1000.0f);
    attackSamples = static_cast<std::uint64_t> (std::llround (0.001 * parameters.attackMs * sampleRate));
    decayMultiplier = std::exp (-log1000 / (0.001 * parameters.decayMs * sampleRate));

    // A peak follower pair with four time constants. Both channels share the
    // larger input magnitude, so an off-centre transient cannot move the image.
    const auto time = 0.001 * t.timeMs * sampleRate;
    transientAttack = std::exp (-1.0 / (0.025 * time));
    transientRelease = std::exp (-1.0 / (0.25 * time));
    transientSlowAttack = std::exp (-1.0 / (0.5 * time));
    transientSlowRelease = std::exp (-1.0 / time);
}

void ImpactEffects::startCut(const CutIdentity& identity) noexcept HBA_NONBLOCKING {
    ++state.cutsStarted;
    if (filterHeld && ! filterDetached)
    {
        // A band fading out shouldn't breathe: this cut would open a held band
        // 1.5 octaves while it fades, an audible change of shape. So it fades on
        // its own envelope, decaying on from where it was.
        filterDetached = true;
        filterEnvelope = adEnvelope;
        filterRepeatProgress = state.repeatProgress;
    }
    state.repeatProgress = identity.cutCount > 1
        ? static_cast<double> (std::min (identity.cutIndex, identity.cutCount - 1))
            / static_cast<double> (identity.cutCount - 1) : 0.0;
    const auto cut = identityHash (identity);
    // Fixed effect tags keep draws independent of enable order and the legacy RNG.
    state.filterActive = choose (cut, UINT64_C (0x46494c5445523036), parameters.filter.enabled, parameters.filter.chance);
    state.driveActive = choose (cut, UINT64_C (0x4452495645303036), parameters.drive.enabled, parameters.drive.chance);
    state.transientActive = choose (cut, UINT64_C (0x5452414e53493036), parameters.transient.enabled, parameters.transient.chance);
    wet[0].setTarget (state.filterActive ? parameters.filter.mix : 0.0, rampSamples);
    wet[1].setTarget (state.driveActive ? parameters.drive.mix : 0.0, rampSamples);
    wet[2].setTarget (state.transientActive ? parameters.transient.mix : 0.0, rampSamples);
    attackPosition = 0;
    adEnvelope = attackSamples == 0 ? 1.0 : 0.0;
    state.envelope = adEnvelope;
    // A held sweep runs on as if this cut never came: its tail neither strikes nor ducks.
    if (! filterHeld && parameters.filter.type == ImpactParameters::Filter::Type::sweep)
        startTail (identity);
}

void ImpactEffects::startTail(const CutIdentity& identity) noexcept HBA_NONBLOCKING {
    const auto& p = parameters.filter;
    // The cut after a roll's last strikes the tail, at tailLevel times the
    // roll's RMS, so a roll of silence strikes nothing. The RMS is the
    // low-pass's before the makeup, but never above the roll's as cut, so
    // neither the makeup nor the resonance can make a tail louder than its roll.
    if (tail.rollEnding && state.filterActive && p.tailLevel > 0.0f && tail.rollSamples > 0)
    {
        const auto energy = std::min (tail.rollEnergy, tail.rollInputEnergy);
        const auto peak = p.tailLevel * std::numbers::sqrt2 * std::sqrt (energy / static_cast<double> (tail.rollSamples));
        if (peak > 0.0)
        {
            // A silent tail starts at phase 0, so a strike sounds the same
            // every time; a ringing one changes note with its phase intact.
            if (tail.amplitude == 0.0)
                tail.phase = 0.0;
            tail.hz = detail::tailNoteHz (p.cutoffHz);
            tail.increment = 2.0 * pi * tail.hz / sampleRate;
            tail.decay = std::exp (-log1000 / (0.001 * p.tailDecayMs * sampleRate));
            tail.peak = peak;
            tail.attackRemaining = rampSamples;
            tail.attackStep = (peak - tail.amplitude) / rampSamples;
        }
    }
    tail.rollEnding = identity.cutCount >= 2 && identity.cutIndex + 1 == identity.cutCount;
    if (identity.cutIndex == 0)
    {
        tail.rollEnergy = tail.rollInputEnergy = 0.0;
        tail.rollSamples = 0;
    }
    // Every cut ducks the tail, so the hits stay clean and it sings between them.
    tail.duckRemaining = rampSamples;
    tail.duckStep = (tail.duck - tailDuckFloor) / rampSamples;
}

double ImpactEffects::nextTail() noexcept HBA_NONBLOCKING {
    if (tail.duckRemaining > 0)
        tail.duck = --tail.duckRemaining == 0 ? tailDuckFloor : tail.duck - tail.duckStep;
    else
        tail.duck = 1.0 - tailRecoveryPole * (1.0 - tail.duck);
    if (tail.attackRemaining > 0)
        tail.amplitude = --tail.attackRemaining == 0 ? tail.peak : tail.amplitude + tail.attackStep;
    else if (tail.amplitude * tail.decay < tailSilence)
    {
        tail.amplitude = tail.phase = tail.hz = 0.0;
    }
    else
        tail.amplitude *= tail.decay;
    state.tailHz = tail.hz;
    state.tailAmplitude = tail.amplitude;
    state.tailDuck = tail.duck;
    if (tail.amplitude == 0.0)
        return 0.0;
    const auto sample = tail.amplitude * tail.duck * std::sin (tail.phase);
    tail.phase += tail.increment;
    if (tail.phase >= 2.0 * pi)
        tail.phase -= 2.0 * pi;
    return sample;
}

double ImpactEffects::nextEnvelope() noexcept HBA_NONBLOCKING {
    if (attackPosition < attackSamples)
        adEnvelope = static_cast<double> (++attackPosition) / static_cast<double> (attackSamples);
    else if (attackSamples == 0 && attackPosition == 0)
        ++attackPosition; // An instantaneous attack emits exactly 1 on the onset sample.
    else
        adEnvelope = cleanState (adEnvelope * decayMultiplier);
    state.envelope = adEnvelope;
    return adEnvelope;
}

void ImpactEffects::processFilter(double& left, double& right, double envelope,
                                  double repeatProgress) noexcept HBA_NONBLOCKING {
    const auto mix = wet[0].next();
    if (mix == 0.0)
    {
        resetBand();
        if (filterHeld) // Only a held filter differs from the requested one.
            adoptFilter();
        return;
    }
    if (smoothed (parameters.filter.type))
    {
        processBand (left, right, envelope, repeatProgress, mix);
        return;
    }
    const auto& p = parameters.filter;
    // Octave modulation operates in log Hz; the physical coefficient is bounded
    // before tan() so extreme sweeps remain below Nyquist at every supported rate.
    const auto octaves = p.envelopeOctaves * envelope + p.repeatOctaves * repeatProgress;
    const auto cutoff = std::clamp (p.cutoffHz * std::exp2 (octaves), 30.0, 0.45 * sampleRate);
    const auto coefs = hba::dsp::SvfCoefs::make(cutoff, p.q, sampleRate);
    const auto channel = [&](double input, FilterState& s) noexcept HBA_NONBLOCKING {
        double v1 = 0.0, v2 = 0.0;
        s.tick(coefs, input, v1, v2);
        s.ic1 = cleanState(s.ic1);
        s.ic2 = cleanState(s.ic2);
        const auto output = p.type == ImpactParameters::Filter::Type::lowPass    ? v2
                            : p.type == ImpactParameters::Filter::Type::bandPass ? coefs.k * v1
                                                                                 : input - coefs.k * v1 - v2;
        return input + mix * (output - input);
    };
    left = channel (left, filterState[0]);
    right = channel (right, filterState[1]);
}

void ImpactEffects::processBand(double& left, double& right, double envelope, double repeatProgress,
                                double mix) noexcept HBA_NONBLOCKING {
    const auto& p = parameters.filter;
    if (! bandPrimed)
    {
        bandLow = bandLowTarget;
        bandHigh = bandHighTarget;
        bandQ = p.q;
        bandPrimed = true;
    }
    else
    {
        // 20 ms one-pole glides, so block-rate parameter steps cannot zipper.
        bandLow = bandLowTarget + smoothingPole * (bandLow - bandLowTarget);
        bandHigh = bandHighTarget + smoothingPole * (bandHigh - bandHighTarget);
        bandQ = p.q + smoothingPole * (bandQ - p.q);
    }
    // A band's envelope opens its edges outward and repeat progress shifts
    // both. The high edge stays at least a semitone above the low edge, so
    // crossed edges hold a semitone apart, and both stay inside
    // [30, 0.45 * rate]. A sweep's high-pass stays at 30 Hz, and both move
    // only its low-pass edge, which stays a semitone above it.
    const auto sweep = p.type == ImpactParameters::Filter::Type::sweep;
    const auto opening = p.envelopeOctaves * envelope;
    const auto shift = p.repeatOctaves * repeatProgress;
    const auto limit = 0.45 * sampleRate;
    double low, high;
    if (sweep)
    {
        low = sweepLowHz;
        high = std::min (std::max (std::exp2 (bandHigh + opening + shift), low * semitone), limit);
    }
    else
    {
        low = std::clamp (std::exp2 (bandLow - opening + shift), 30.0, limit);
        high = std::clamp (std::exp2 (bandHigh + opening + shift), 30.0, limit);
        high = std::min (std::max (high, low * semitone), limit);
        low = std::min (low, high / semitone);
    }
    state.bandLowHz = low;
    state.bandHighHz = high;

    const auto lowQ = sweep ? sweepLowQ : bandQ;
    const auto highPass = hba::dsp::SvfCoefs::make(low, lowQ, sampleRate);
    const auto lowPass = hba::dsp::SvfCoefs::make(high, bandQ, sampleRate);
    const auto tick = [](const hba::dsp::SvfCoefs& c, double input, FilterState& s) noexcept HBA_NONBLOCKING {
        double v1 = 0.0, v2 = 0.0;
        s.tick(c, input, v1, v2);
        s.ic1 = cleanState(s.ic1);
        s.ic2 = cleanState(s.ic2);
        return std::array<double, 2> { v1, v2 };
    };
    const auto channel = [&](double input, FilterState& highState, FilterState& lowState) noexcept HBA_NONBLOCKING {
        const auto [v1, v2] = tick (highPass, input, highState);
        return tick(lowPass, input - highPass.k * v1 - v2, lowState)[1];
    };
    const auto bandLeft = channel (left, filterState[0], bandState[0]);
    const auto bandRight = channel (right, filterState[1], bandState[1]);

    // Level hold: makeup from the band's shape this sample, so pink material
    // keeps the open band's power, including where the resonant edges overlap.
    // It is at most +6 dB. It follows the breathing edges exactly, so it never
    // lags or overshoots.
    // Octaves above 18 kHz don't count: at high rates the breathing pushes the
    // high edge there, where material has little power, and counting them
    // would dip the level at every cut. The table clamps a band that sits
    // wholly above 18 kHz to its narrowest width. A sweep has its own table,
    // for its fixed Butterworth high-pass, and its makeup only ever takes back
    // what its resonance adds: never above 1, and never letting a tone on the
    // resonant peak gain more than 6 dB. So a dark sweep is quieter than the
    // open filter on broadband material, as any low-pass is.
    const auto* table = sweep ? sweepPowerTableData : bandPowerTable;
    const auto width = std::log2 (std::min (high, makeupHighestHz) / low);
    const auto pink = table != nullptr ? std::exp(0.5 * (table->logOpen - table->interpolate(width, bandQ))) : 1.0;
    const auto hold = sweep ? std::min(sweepMakeupCeiling, std::min(pink, sweepPeakCeiling / lowPassPeak(bandQ)))
                            : std::min(makeupCeiling, pink);
    state.levelHoldGain = hold;
    if (sweep)
    {
        // The tail joins the wet, and measures the roll.
        const auto heardLeft = hold * bandLeft, heardRight = hold * bandRight;
        tail.rollEnergy += 0.5 * (bandLeft * bandLeft + bandRight * bandRight);
        tail.rollInputEnergy += 0.5 * (left * left + right * right);
        ++tail.rollSamples;
        const auto ring = nextTail();
        left += mix * (heardLeft + ring - left);
        right += mix * (heardRight + ring - right);
        return;
    }
    left += mix * (hold * bandLeft - left);
    right += mix * (hold * bandRight - right);
}

void ImpactEffects::processDrive(double& left, double& right, double envelope) noexcept HBA_NONBLOCKING {
    const auto mix = wet[1].next();
    if (mix == 0.0)
    {
        driveState = {};
        return;
    }
    const auto& p = parameters.drive;
    const auto db = std::clamp (p.driveDb + p.envelopeDb * envelope + p.repeatDb * state.repeatProgress, 0.0, 30.0);
    const auto inputGain = gain (db);
    const auto channel = [&] (double input, DriveState& s)
    {
        const auto x = input * inputGain;
        double shaped;
        if (p.mode == ImpactParameters::Drive::Mode::wavefold)
        {
            // F(x)=-cos(x). Its divided difference is evaluated with the exact
            // trig identity sin(midpoint)*sinc(halfDifference), avoiding cancellation.
            const auto previous = s.primed ? s.previous : x;
            const auto halfDifference = 0.5 * (x - previous);
            const auto sinc = std::abs (halfDifference) < 1.0e-4
                ? 1.0 - halfDifference * halfDifference / 6.0 : std::sin (halfDifference) / halfDifference;
            shaped = std::sin (0.5 * (x + previous)) * sinc;
        }
        else
        {
            // First-order ADAA: [F(x[n])-F(x[n-1])]/[x[n]-x[n-1]].
            // Uses hba::dsp::TanhShaper::ad1 (Maclaurin-stabilized log(cosh(x))).
            const auto primitive = hba::dsp::TanhShaper::ad1(x);
            const auto difference = x - s.previous;
            shaped = ! s.primed || std::abs (difference) < 1.0e-4
                ? std::tanh (s.primed ? 0.5 * (x + s.previous) : x)
                : (primitive - s.previousPrimitive) / difference;
            s.previousPrimitive = primitive;
        }
        s.previous = x;
        s.primed = true;
        const auto blocked = cleanState (shaped - s.dcInput + dcPole * s.dcOutput);
        s.dcInput = shaped;
        s.dcOutput = blocked;
        return input + mix * (blocked - input);
    };
    left = channel (left, driveState[0]);
    right = channel (right, driveState[1]);
}

void ImpactEffects::processTransient(double& left, double& right) noexcept HBA_NONBLOCKING {
    const auto mix = wet[2].next();
    if (mix == 0.0)
    {
        fastEnvelope = slowEnvelope = 0.0;
        return;
    }
    const auto detector = std::max (std::abs (left), std::abs (right));
    const auto fast = detector > fastEnvelope ? transientAttack : transientRelease;
    const auto slow = detector > slowEnvelope ? transientSlowAttack : transientSlowRelease;
    fastEnvelope = cleanState (detector + fast * (fastEnvelope - detector));
    slowEnvelope = cleanState (detector + slow * (slowEnvelope - detector));
    const auto& p = parameters.transient;
    if ((p.attackDb == 0.0f && p.sustainDb == 0.0f) || std::max (fastEnvelope, slowEnvelope) < 1.0e-15)
        return;
    const auto attack = std::clamp ((fastEnvelope - slowEnvelope) / std::max (fastEnvelope, slowEnvelope), 0.0, 1.0);
    // A convex dB blend bounds gain by the two controls (at most +/-18 dB).
    const auto db = p.attackDb * attack + p.sustainDb * (1.0 - attack);
    const auto amount = 1.0 + mix * (gain (db) - 1.0);
    left *= amount;
    right *= amount;
}

void ImpactEffects::process(float& left, float& right) noexcept HBA_NONBLOCKING {
    const auto envelope = nextEnvelope();
    if (wet[0].current == 0.0 && wet[0].target == 0.0
        && wet[1].current == 0.0 && wet[1].target == 0.0
        && wet[2].current == 0.0 && wet[2].target == 0.0)
        return;
    auto l = static_cast<double> (left), r = static_cast<double> (right);
    if (filterDetached)
    {
        filterEnvelope = cleanState (filterEnvelope * heldDecayMultiplier);
        processFilter (l, r, filterEnvelope, filterRepeatProgress);
    }
    else
        processFilter (l, r, envelope, state.repeatProgress);
    processDrive (l, r, envelope);
    processTransient (l, r);
    left = static_cast<float> (l);
    right = static_cast<float> (r);
}
} // namespace livecut::engine
