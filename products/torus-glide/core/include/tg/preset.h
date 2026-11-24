// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/chord_matrix.h"
#include "tg/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace tg {

struct PresetData {
    std::string name;
    std::string category;
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    SceneMatrix matrix = matrixForPack(0);
};

std::optional<PresetData> parsePresetJson(std::string_view json);
std::string serialisePresetJson(const PresetData& preset);

std::optional<SceneMatrix> parseMatrixJson(std::string_view json);
std::string serialiseMatrixJson(const SceneMatrix& matrix);

} // namespace tg
