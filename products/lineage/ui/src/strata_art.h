// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/engine.h"

#include <visage/graphics.h>

#include <array>

namespace ln::ui {

struct StrataMotion {
    float x = 0.5f;
    float y = 0.5f;
    float phase = 0.0f;
    float rms = 0.0f;
    float subPeak = 0.0f;
    float filt1Norm = 0.45f;
    float filt2Norm = 0.60f;
    float drive = 0.38f;
    float tone = 0.42f;
    float genMix = 0.0f;
    float disperse = 0.24f;
    float density = 0.36f;
    float width = 0.68f;
    float subWarmth = 0.26f;
    float playheadNorm = 0.0f;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    int generation = 0;
    bool hasPrint = false;
    bool sounding = false;
    std::array<float, TelemetrySnapshot::kScopePoints> scope{};
    std::array<float, 7> voicePhases{};
    std::array<float, PrintBuffer::kWaveformPoints> printPeaks{};
};

void drawStrataArt(visage::Canvas& canvas, int program, float x, float y, float w, float h, const StrataMotion& m,
                   float scale, float alpha);

} // namespace ln::ui
