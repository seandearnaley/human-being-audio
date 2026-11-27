// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Library.h"

#include "CutlineLibraryData.h"
#include "cl/groove_json.h"

#include <algorithm>
#include <string_view>
#include <tuple>

namespace cl {
namespace {

FactoryLibrary load() {
    FactoryLibrary library;
    for (int i = 0; i < CutlineLibraryData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = CutlineLibraryData::getNamedResource(CutlineLibraryData::namedResourceList[i], size);
        if (data == nullptr || size <= 0)
            continue;
        const std::string_view json(data, static_cast<std::size_t>(size));
        const std::string category = libraryCategory(json);
        std::string name;
        // A fill file's lanes are step strings and a pattern's are objects, so each file parses as only one of them.
        if (auto fill = parseFill(json, nullptr, &name); fill && !name.empty()) {
            library.fills.push_back({name, category.empty() ? "Fills" : category, *fill});
            continue;
        }
        if (auto pattern = parsePattern(json, nullptr, &name); pattern && !name.empty())
            library.patterns.push_back({name, category.empty() ? "Patterns" : category, *pattern});
    }
    const auto byCategoryThenName = [](const auto& a, const auto& b) {
        return std::tie(a.category, a.name) < std::tie(b.category, b.name);
    };
    std::sort(library.patterns.begin(), library.patterns.end(), byCategoryThenName);
    std::sort(library.fills.begin(), library.fills.end(), byCategoryThenName);
    return library;
}

} // namespace

const FactoryLibrary& factoryLibrary() {
    static const FactoryLibrary library = load();
    return library;
}

} // namespace cl
