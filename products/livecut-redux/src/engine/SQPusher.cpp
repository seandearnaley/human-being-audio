// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from Livecut, Copyright 2003-2004 Remy Muller, which implements
// SQPusher1 from Nick Collins' BBCut library for SuperCollider.

#include "engine/CutProcedures.h"
#include "engine/CutShaping.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace livecut::engine
{

namespace
{
constexpr double epsilon = 1.0e-6;

// Squarepusher rhythms transcribed by Nick Collins. BBCut wrote the tuplets as
// rounded decimals (0.571429, 0.33, 0.167 and a fudged 1.01); exact fractions
// make every fill last exactly one bar of 4/4.
constexpr double third = 1.0 / 3.0;
constexpr double fifth = 1.0 / 5.0;
constexpr double sixth = 1.0 / 6.0;
constexpr double seventh = 1.0 / 7.0;

struct FillBlock {
    std::array<double, 6> beats{};
    std::size_t count = 0;
};

struct FillSpec {
    std::array<FillBlock, 5> blocks{};
    std::size_t count = 0;
};

constexpr std::array<FillSpec, 13> staticFills{{
    {{{{{0.75, 0.75, 0.75, 0.75}, 4}, {{1.0}, 1}}}, 2},
    {{{{{0.5, 1.0}, 2}, {{1.0}, 1}, {{1.0, 0.5}, 2}}}, 3},
    {{{{{0.5}, 1}, {{1.0, 1.0, 1.0}, 3}, {{0.5}, 1}}}, 3},
    {{{{{4 * seventh}, 1},
       {{4 * seventh, 4 * seventh}, 2},
       {{4 * seventh, 4 * seventh}, 2},
       {{4 * seventh}, 1},
       {{2 * seventh, 2 * seventh}, 2}}},
     5},
    {{{{{1.0, 0.5}, 2}, {{1.0, 0.5}, 2}, {{0.5, 0.5}, 2}}}, 3},
    {{{{{0.5, 0.5}, 2}, {{2 * third, 2 * third, 2 * third}, 3}, {{1.0}, 1}}}, 3},
    {{{{{third}, 1}, {{third, third}, 2}, {{third, third}, 2}, {{7 * third}, 1}}}, 4},
    {{{{{7 * fifth}, 1}, {{2 * fifth, 2 * fifth}, 2}, {{3 * fifth, fifth}, 2}, {{1.0}, 1}}}, 4},
    {{{{{sixth, sixth, sixth}, 3}, {{1.0, 1.0, 1.0}, 3}, {{0.5}, 1}}}, 3},
    {{{{{1.5, 0.5, 1.0}, 3}, {{0.25, 0.25, 0.25, 0.25}, 4}}}, 2},
    {{{{{fifth, fifth}, 2}, {{2 * fifth, 2 * fifth}, 2}, {{2 * fifth, 2 * fifth}, 2}, {{2.0}, 1}}}, 4},
    {{{{{0.75, 0.75, 1.0}, 3}, {{0.25, 0.25, 0.25, 0.25, 0.25, 0.25}, 6}}}, 2},
    {{{{{0.5, 1.0}, 2}, {{0.5}, 1}, {{0.125, 0.125, 0.125, 0.125}, 4}, {{1.0}, 1}, {{sixth, sixth, sixth}, 3}}}, 5},
}};

// The fills built only from even divisions of the beat, for Straight.
constexpr std::array straightFills { 0, 1, 2, 4, 9, 11 };

// The chance of semiquavers on each quaver of the bar, scaled by the activity setting.
constexpr std::array semiquaverWeights { 0.0, 0.3, 0.0, 0.5, 0.7, 0.8, 0.9, 0.6 };

// Shortens a block that would run past `unitsLeft`: trims the cut that
// straddles the limit and drops the cuts after it, as BBCut does.
void truncateToFit(CutList& cuts, double unitsLeft) noexcept HBA_NONBLOCKING {
    auto start = 0.0;

    for (std::size_t i = 0; i < cuts.size(); ++i)
    {
        if (start + cuts[i].units > unitsLeft + epsilon)
        {
            cuts[i].units = unitsLeft - start;
            cuts.resize (cuts[i].units > epsilon ? i + 1 : i);
            return;
        }

        start += cuts[i].units;
    }
}
} // namespace

const std::vector<SQPusher::Fill>& SQPusher::fills()
{
    static const std::vector<Fill> table = [] {
        std::vector<Fill> out;
        out.reserve(staticFills.size());
        for (const auto& f : staticFills) {
            Fill fill;
            fill.reserve(f.count);
            for (std::size_t b = 0; b < f.count; ++b)
                fill.emplace_back(f.blocks[b].beats.begin(),
                                  f.blocks[b].beats.begin() + static_cast<std::ptrdiff_t>(f.blocks[b].count));
            out.push_back(std::move(fill));
        }
        return out;
    }();
    return table;
}

void SQPusher::reset() noexcept HBA_NONBLOCKING {
    fill = -1;
    fillBlock = 0;
}

int SQPusher::choosePhraseBars(Random& random, const CutParameters& params) noexcept HBA_NONBLOCKING {
    reset();
    return CutProcedure::choosePhraseBars (random, params);
}

void SQPusher::chooseBlock(Random& random, const CutParameters& params, const BlockContext& context,
                           CutList& cuts) noexcept HBA_NONBLOCKING {
    // SQPusher works one bar at a time, in quarter-note beats.
    const auto unitsPerBar = static_cast<double> (context.unitsPerBar);
    const auto unitsPerBeat = unitsPerBar / context.beatsPerBar;
    const auto bar = std::floor ((context.unitsDone + epsilon) / unitsPerBar);
    const auto unitsIntoBar = std::max (0.0, context.unitsDone - bar * unitsPerBar);
    const auto beatsIntoBar = unitsIntoBar / unitsPerBeat;
    const auto startsLastBar = unitsIntoBar < epsilon
                            && std::abs (context.phraseUnits - context.unitsDone - unitsPerBar) < epsilon;

    if (startsLastBar)
    {
        fill =
            params.straight
                ? straightFills[static_cast<std::size_t>(random.integer(0, static_cast<int>(straightFills.size()) - 1))]
                : random.integer(0, static_cast<int>(staticFills.size()) - 1);
        fillBlock = 0;
    }

    // Straight turned on mid-fill: a tuplet fill gives way to quavers.
    auto isStraightFill = [](int id) noexcept HBA_NONBLOCKING {
        for (const int s : straightFills)
            if (s == id)
                return true;
        return false;
    };
    if (params.straight && fill >= 0 && !isStraightFill(fill)) {
        fill = -1;
        const auto sixtyFourths = beatsIntoBar * 64.0;

        if (std::abs (sixtyFourths - std::round (sixtyFourths)) > epsilon)
        {
            // Recover the beat grid with one cut to the next whole beat.
            const auto toBeat = (std::floor (beatsIntoBar) + 1.0) * unitsPerBeat - unitsIntoBar;
            Cut cut;
            cut.units = std::min(toBeat, std::min(unitsPerBar - unitsIntoBar, context.phraseUnits - context.unitsDone));
            cut.gate = params.dutyCycle;
            cut.amp = static_cast<float> (random.uniform (params.minAmp, params.maxAmp));
            cuts.assign (1, cut);
            return;
        }
    }

    cuts.clear();

    if (fill >= 0 && fillBlock < staticFills[static_cast<std::size_t>(fill)].count) {
        const auto& block = staticFills[static_cast<std::size_t>(fill)].blocks[fillBlock];
        for (std::size_t i = 0; i < block.count; ++i) {
            Cut cut;
            cut.units = block.beats[i] * unitsPerBeat;
            cut.gate = params.fillDutyCycle;
            cuts.push_back (cut);
        }

        ++fillBlock;
    } else {
        fill = -1;

        // One or two quavers, or as many pairs of semiquavers. A block starting
        // on the off-beat quaver is a single quaver, so blocks stay on the beat grid.
        const auto positionInBeat = std::fmod (std::round (beatsIntoBar * 4.0) / 4.0, 1.0);
        const auto quaver = static_cast<std::size_t> (std::lround (beatsIntoBar * 2.0)) % semiquaverWeights.size();
        auto quavers = random.integer (1, 2);

        if (std::abs (positionInBeat - 0.5) < epsilon)
            quavers = 1;

        const auto semiquavers = random.chance (semiquaverWeights[quaver] * params.activity);

        Cut cut;
        cut.units = (semiquavers ? 0.25 : 0.5) * unitsPerBeat;
        cut.gate = params.dutyCycle;
        cuts.assign (static_cast<std::size_t> (semiquavers ? 2 * quavers : quavers), cut);
    }

    scatter (cuts, random, params);
    truncateToFit (cuts, unitsPerBar - unitsIntoBar);
}

} // namespace livecut::engine
