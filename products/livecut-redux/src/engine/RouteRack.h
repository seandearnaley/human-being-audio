// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/Resonator.h"
#include "engine/FrequencyEffects.h"
#include "engine/ImpactEffects.h"

#include <array>
#include <cstdint>

namespace livecut::engine
{
enum class EffectRoute { impactResonatorFrequency, resonatorImpactFrequency, impactFrequencyResonator };

struct RouteParameters
{
    ImpactParameters impact;
    FrequencyParameters frequency;
    ResonatorParameters resonator;
};

struct RouteCutContext
{
    CutIdentity identity;
    bool frequencySelected = false; // Frozen once by Cutter, never drawn by the rack.
};

// Crusher/Comb prefix and one Spectral suffix remain outside this fixed rack.
class RouteRack
{
public:
    struct Diagnostics
    {
        EffectRoute active = EffectRoute::impactResonatorFrequency;
        EffectRoute requested = EffectRoute::impactResonatorFrequency;
        bool transitioning = false;
        std::uint32_t framesRemaining = 0;
        std::uint64_t cutsStarted = 0;
    };
    bool prepare (double sampleRate);
    void reset() noexcept HBA_NONBLOCKING;
    void setParameters(const RouteParameters&) noexcept HBA_NONBLOCKING; // Existing next-block contract.
    void requestRoute(EffectRoute) noexcept HBA_NONBLOCKING;             // Structural; no switch until an eligible bar.
    void barStarted() noexcept HBA_NONBLOCKING;
    void startCut(const RouteCutContext&) noexcept HBA_NONBLOCKING;
    void process(float& left, float& right) noexcept HBA_NONBLOCKING;
    const Diagnostics& diagnostics() const noexcept HBA_NONBLOCKING { return status; }
    const ImpactEffects& impact() const noexcept HBA_NONBLOCKING { return lanes[active].impact; }
    const Resonator& resonator() const noexcept HBA_NONBLOCKING { return lanes[active].resonator; }

  private:
    struct Lane
    {
        ImpactEffects impact;
        FrequencyEffects frequency;
        Resonator resonator;
        EffectRoute route = EffectRoute::impactResonatorFrequency;
        void process(float& left, float& right, bool frequencySelected) noexcept HBA_NONBLOCKING;
    };
    std::array<Lane, 2> lanes;
    RouteCutContext cut;
    Diagnostics status;
    std::size_t active = 0;
    std::uint32_t fadeFrames = 240, fadePosition = 0;
    bool fading = false;
};
} // namespace livecut::engine
