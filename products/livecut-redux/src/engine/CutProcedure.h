// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "engine/Cut.h"
#include "engine/Random.h"

#include <vector>

namespace livecut::engine
{

// A strategy that decides how long each phrase lasts and how each block in it is cut.
class CutProcedure
{
public:
    virtual ~CutProcedure() = default;

    // Forgets any state carried between blocks, for when the cutter switches
    // to this procedure part way through a phrase.
    virtual void reset() noexcept HBA_NONBLOCKING {}

    // Called at the start of every phrase.
    virtual int choosePhraseBars(Random& random, const CutParameters& params) noexcept HBA_NONBLOCKING {
        return random.integer (params.minPhraseBars, params.maxPhraseBars);
    }

    // Replaces `cuts` with the next block. The block lasts totalUnits (cuts),
    // which is never zero and never runs past the end of the phrase.
    virtual void chooseBlock(Random& random, const CutParameters& params, const BlockContext& context,
                             CutList& cuts) noexcept HBA_NONBLOCKING = 0;

    // Convenience overload for non-real-time callers and unit tests.
    void chooseBlock(Random& random, const CutParameters& params, const BlockContext& context, std::vector<Cut>& cuts) {
        CutList list;
        chooseBlock(random, params, context, list);
        cuts.assign(list.begin(), list.end());
    }
};

} // namespace livecut::engine
