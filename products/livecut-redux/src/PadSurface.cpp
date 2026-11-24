// SPDX-License-Identifier: GPL-3.0-or-later

#include "PadSurface.h"

namespace livecut
{
Settings padSettings (const Settings& user, const engine::PadMapping& mapping)
{
    Settings settings{};
    settings.procedure = mapping.procedure;
    settings.unitsPerBar = mapping.unitsPerBar;
    // Default seed (1) preserves exact PadMapping seeds; rolling Seed on the Pad
    // shifts the mapping seed cleanly for instant fresh rhythmic variations.
    settings.seed = mapping.seed + (user.seed > 1U ? (user.seed - 1U) : 0U);
    settings.cut = mapping.cut;
    settings.cut.straight = user.padStraight;
    settings.crusher = mapping.crusher;
    settings.impact = mapping.impact;
    settings.fracture = mapping.fracture;
    settings.wet = user.wet;
    settings.outputTrimDb = user.outputTrimDb;
    settings.subAnchorHz = user.subAnchorHz;
    settings.limiter = user.limiter;
    // Preserve stream-level mode/spectral configuration so switching Pad <-> Classic
    // never drops audio, while zeroing Classic's Spectral Freeze/Smear/Mix so they
    // never silently hijack or freeze the Pad surface.
    settings.requestedMode = user.requestedMode;
    settings.requestedSpectral = user.requestedSpectral;
    settings.spectralFreeze = false;
    settings.spectralSmearMs = 0.0f;
    settings.spectralMix = 0.0f;
    settings.surface = user.surface;
    settings.padProgram = user.padProgram;
    settings.padX = user.padX;
    settings.padY = user.padY;
    settings.padTouch = user.padTouch;
    settings.padHold = user.padHold;
    settings.padStraight = user.padStraight;
    settings.padQuantize = user.padQuantize;
    return settings;
}
} // namespace livecut
