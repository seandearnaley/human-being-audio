// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/ui/fake_model.h"

#include <algorithm>
#include <cmath>

namespace lg::ui {

namespace {
struct FakePreset {
    const char* name;
    const char* category;
    Key a;
    Key b;
    RouteId route;
    double bpm;
};

constexpr FakePreset kPresets[] = {
    {"Neon Pivot", "Garage", {5, Mode::minor}, {0, Mode::major}, RouteId::backdoor, 122.0},
    {"Glass Lift", "House", {2, Mode::minor}, {5, Mode::major}, RouteId::pivot, 126.0},
    {"Low Orbit", "DnB", {4, Mode::minor}, {8, Mode::major}, RouteId::hexatonic, 172.0},
};
constexpr int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

Song presetSong(int index) {
    const auto& p = kPresets[index];
    Document scratch(defaultSong());
    scratch.setSectionKey(0, p.a);
    scratch.setSectionKey(1, p.b);
    scratch.setRoute(0, p.route);
    scratch.setBpm(p.bpm);
    scratch.setName(p.name);
    Song song = scratch.song();
    song.category = p.category;
    return song;
}
} // namespace

FakeModel::FakeModel() : doc_(defaultSong()) { showStopped(); }

void FakeModel::edit(const std::function<void(Document&)>& change) {
    change(doc_);
    ++edits_;
}

std::vector<PresetInfo> FakeModel::presets() const {
    std::vector<PresetInfo> out;
    for (const auto& p : kPresets)
        out.push_back({p.name, p.category});
    return out;
}

void FakeModel::loadPreset(int index) {
    if (index < 0 || index >= kPresetCount)
        return;
    preset_ = index;
    doc_.load(presetSong(index));
}

void FakeModel::stepPreset(int delta) { loadPreset(((preset_ + delta) % kPresetCount + kPresetCount) % kPresetCount); }

void FakeModel::light(ItemRef item, float step) {
    telemetry_.sounding = {};
    const auto& pattern = doc_.pattern(item);
    for (const auto& n : pattern.notes) {
        if (n.start > step || n.start + n.dur <= step || n.pitch < 0 || n.pitch > 127)
            continue;
        auto& mask = telemetry_.sounding[static_cast<std::size_t>(n.role)];
        mask[static_cast<std::size_t>(n.pitch / 64)] |= std::uint64_t{1} << static_cast<unsigned>(n.pitch % 64);
    }
}

void FakeModel::play(ItemRef item, int bars, float step, double beat) {
    telemetry_ = {};
    telemetry_.playing = true;
    telemetry_.previewPlaying = true;
    telemetry_.bpm = doc_.song().bpm;
    telemetry_.beat = beat;
    telemetry_.performance.item = item;
    telemetry_.performance.anchor = beat - static_cast<double>(step) / 4.0;
    telemetry_.playingBars = bars;
    telemetry_.stepInItem = step;
    telemetry_.peak = 0.62f;
    light(item, step);
}

void FakeModel::showStopped() {
    telemetry_ = {};
    telemetry_.bpm = doc_.song().bpm;
}

void FakeModel::showLaunchQueued() {
    play(ItemRef::section(0), itemBars(doc_.song(), ItemRef::section(0)), 41.0f, 32.0 + 41.0 / 4.0);
    auto& perf = telemetry_.performance;
    perf.planActive = true;
    perf.planBridge = 0;
    perf.planTarget = 1;
    perf.planFit = doc_.song().bridges.empty() ? 0 : doc_.song().bridges[0].bars;
    perf.planStart = 48.0;
    telemetry_.barsToStart = 1;
    telemetry_.barsToLand = 1 + perf.planFit;
}

void FakeModel::showInBridge() {
    const int bars = doc_.song().bridges.empty() ? 1 : doc_.song().bridges[0].bars;
    play(ItemRef::bridge(0), bars, 20.0f, 48.0 + 5.0);
    telemetry_.hostPlaying = true;
    telemetry_.previewPlaying = false;
    telemetry_.performance.target = 1;
    telemetry_.performance.fit = 0;
    telemetry_.barsToLand = 1;
}

void FakeModel::showHolding() {
    const int bars = doc_.song().bridges.empty() ? 1 : doc_.song().bridges[0].bars;
    play(ItemRef::bridge(0), bars, 6.0f, 48.0 + 1.5);
    telemetry_.performance.target = 1;
    telemetry_.performance.hold = true;
    telemetry_.barsToLand = -1;
}

void FakeModel::launch(int section, bool direct) {
    if (!telemetry_.playing) {
        play(ItemRef::section(section), itemBars(doc_.song(), ItemRef::section(section)), 0.0f, 0.0);
        return;
    }
    auto& perf = telemetry_.performance;
    perf.planActive = true;
    perf.planTarget = section;
    perf.planBridge = -1;
    if (!direct && !perf.item.isBridge())
        for (std::size_t i = 0; i < doc_.song().bridges.size(); ++i)
            if (doc_.song().bridges[i].from == perf.item.index && doc_.song().bridges[i].to == section)
                perf.planBridge = static_cast<int>(i);
    telemetry_.barsToStart = 1;
    telemetry_.barsToLand =
        perf.planBridge >= 0 ? 1 + doc_.song().bridges[static_cast<std::size_t>(perf.planBridge)].bars : 1;
}

void FakeModel::fire(int bridge) {
    if (bridge < 0 || bridge >= static_cast<int>(doc_.song().bridges.size()))
        return;
    launch(doc_.song().bridges[static_cast<std::size_t>(bridge)].to, false);
}

void FakeModel::hold(int bridge, bool down) {
    if (down)
        showHolding();
    else if (telemetry_.performance.hold)
        telemetry_.performance.hold = false;
    (void)bridge;
}

void FakeModel::togglePreview(ItemRef from) {
    if (telemetry_.playing)
        showStopped();
    else
        play(from, itemBars(doc_.song(), from), 0.0f, 0.0);
}

void FakeModel::auditionChord(ItemRef, int) {}

} // namespace lg::ui
