// SPDX-License-Identifier: AGPL-3.0-or-later
// What the engine plays from: a sanitised bank and, for each scene, the Density ranking of its steps.
// Building one ranks every scene, so build it on the message thread and hand it to the audio thread whole.
#pragma once

#include "cl/groove.h"
#include "cl/resolve.h"

#include <array>

namespace cl {

struct GrooveState {
    GrooveBank bank{};
    std::array<DensityMap, kSceneCount> maps{};

    GrooveState() noexcept; // the default bank
    explicit GrooveState(const GrooveBank& source) noexcept;
};

} // namespace cl
