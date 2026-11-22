// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/phrase.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cl::ui {

struct PresetInfo {
    std::string name;
    std::string category;
};

struct Visuals {
    PhrasePlan plan{};
    double playheadBeats = 0.0;
    double ppq = 0.0;
    std::array<float, kLaneCount> lanePhase{};
    std::array<float, kLaneCount> laneValue{};
    std::uint64_t activeNotesLow = 0;
    std::uint64_t activeNotesHigh = 0;
    int lastBlockEvents = 0;
    bool playing = false;
};

struct Model {
    virtual ~Model() = default;

    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual const Visuals& visuals() = 0;

    virtual Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const Curve& curve) = 0;

    virtual void rollSeed() = 0;
    virtual void dragMidiPhrase() = 0;
    virtual std::string exportStatus() const = 0;

    virtual std::string presetName() const = 0;
    virtual std::vector<PresetInfo> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;
};

} // namespace cl::ui
