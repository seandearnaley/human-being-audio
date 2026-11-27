// SPDX-License-Identifier: AGPL-3.0-or-later
// The factory pattern and fill library, embedded from patterns/ and fills/ and parsed once.
#pragma once

#include "cl/ui/model.h"

#include <vector>

namespace cl {

struct FactoryLibrary {
    std::vector<ui::LibraryPattern> patterns; // sorted by category, then name
    std::vector<ui::LibraryFill> fills;       // sorted by category, then name
};

const FactoryLibrary& factoryLibrary();

} // namespace cl
