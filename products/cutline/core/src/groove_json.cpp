// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"

#include "groove_json_detail.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace cl {

namespace detail {

namespace {

constexpr std::array<std::string_view, 3> kLayerKeys{"core", "groove", "detail"};

bool fail(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
    return false;
}

int laneOfKey(std::string_view key) {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        if (kLaneKeys[static_cast<std::size_t>(lane)] == key)
            return lane;
    }
    return -1;
}

int velocityOfChar(char c) {
    switch (c) {
    case 'X':
        return kVelocityAccent;
    case 'x':
        return kVelocityNormal;
    case 'o':
        return kVelocitySoft;
    case '.':
        return 0;
    default:
        return -1;
    }
}

char charOfVelocity(int velocity) {
    const int dAccent = std::abs(velocity - kVelocityAccent);
    const int dNormal = std::abs(velocity - kVelocityNormal);
    const int dSoft = std::abs(velocity - kVelocitySoft);
    if (dNormal <= dAccent && dNormal <= dSoft)
        return 'x';
    return dAccent < dSoft ? 'X' : 'o';
}

bool exactChar(int velocity) {
    return velocity == kVelocityAccent || velocity == kVelocityNormal || velocity == kVelocitySoft;
}

// Reads a step string ('|' and spaces are separators) into `count` steps of one layer.
bool readSteps(const Json& value, Step* steps, int count, bool exactLength, bool setLayer, Layer layer,
               const std::string& where, std::string* error) {
    if (!value.is_string())
        return fail(error, where + " must be a string");
    std::string text;
    for (const char c : value.get<std::string>()) {
        if (c != '|' && c != ' ')
            text.push_back(c);
    }
    const int length = static_cast<int>(text.size());
    if (exactLength ? length != count : length > count)
        return fail(error, where + " has " + std::to_string(length) + " steps, expected " + std::to_string(count));
    for (int i = 0; i < length; ++i) {
        const int velocity = velocityOfChar(text[static_cast<std::size_t>(i)]);
        if (velocity < 0)
            return fail(error, where + " has an unknown step character");
        if (velocity == 0)
            continue;
        Step& s = steps[i];
        if (s.on())
            return fail(error, where + " sets step " + std::to_string(i) + " in more than one layer");
        s.velocity = static_cast<std::uint8_t>(velocity);
        if (setLayer)
            s.layer = layer;
    }
    return true;
}

bool intField(const Json& obj, const char* key, int lo, int hi, int& out) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer())
        return false;
    out = std::clamp(it->get<int>(), lo, hi);
    return true;
}

void applyEdit(const Json& edit, Step& s) {
    int v = 0;
    if (intField(edit, "velocity", 1, 127, v))
        s.velocity = static_cast<std::uint8_t>(v);
    if (intField(edit, "ratchet", 1, 4, v))
        s.ratchet = static_cast<std::uint8_t>(v);
    if (intField(edit, "nudge", -24, 24, v))
        s.nudge = static_cast<std::int8_t>(v);
    s.flam = hba::core::jsonBoolOr(edit, "flam", s.flam);
}

bool needsEdit(const Step& s) { return s.on() && (!exactChar(s.velocity) || s.ratchet != 1 || s.nudge != 0 || s.flam); }

Json editFor(const Step& s) {
    Json e = Json::object();
    if (!exactChar(s.velocity))
        e["velocity"] = static_cast<int>(s.velocity);
    if (s.ratchet != 1)
        e["ratchet"] = static_cast<int>(s.ratchet);
    if (s.nudge != 0)
        e["nudge"] = static_cast<int>(s.nudge);
    if (s.flam)
        e["flam"] = true;
    return e;
}

// Writes `count` steps of one layer (or of every layer when `anyLayer`), 16 per bar.
std::string stepString(const Step* steps, int count, bool anyLayer, Layer layer, bool& any) {
    std::string text;
    any = false;
    for (int i = 0; i < count; ++i) {
        if (i > 0 && (i % kStepsPerBar) == 0)
            text.push_back('|');
        const Step& s = steps[i];
        const bool show = s.on() && (anyLayer || s.layer == layer);
        text.push_back(show ? charOfVelocity(s.velocity) : '.');
        any = any || show;
    }
    return text;
}

} // namespace

std::optional<Pattern> readPattern(const Json& value, std::string* error) {
    if (!value.is_object()) {
        fail(error, "pattern is not a JSON object");
        return std::nullopt;
    }
    Pattern p{};
    int bars = 1;
    if (value.contains("bars")) {
        if (!value["bars"].is_number_integer() ||
            (value["bars"].get<int>() != 1 && value["bars"].get<int>() != 2 && value["bars"].get<int>() != 4)) {
            fail(error, "\"bars\" must be 1, 2 or 4");
            return std::nullopt;
        }
        bars = value["bars"].get<int>();
    }
    p.bars = static_cast<std::uint8_t>(bars);
    const int steps = bars * kStepsPerBar;

    if (value.contains("lanes")) {
        if (!value["lanes"].is_object()) {
            fail(error, "\"lanes\" must be an object");
            return std::nullopt;
        }
        for (const auto& [key, lo] : value["lanes"].items()) {
            const int lane = laneOfKey(key);
            if (lane < 0 || !lo.is_object()) {
                fail(error, "unknown lane \"" + key + "\"");
                return std::nullopt;
            }
            DrumLane& dl = p.lanes[static_cast<std::size_t>(lane)];
            for (std::size_t layer = 0; layer < kLayerKeys.size(); ++layer) {
                const std::string layerKey(kLayerKeys[layer]);
                if (lo.contains(layerKey) && !readSteps(lo[layerKey], dl.steps.data(), steps, true, true,
                                                        static_cast<Layer>(layer), key + "." + layerKey, error))
                    return std::nullopt;
            }
            int v = 0;
            if (intField(lo, "length", 0, kMaxPatternSteps, v))
                dl.length = static_cast<std::uint8_t>(v);
            if (intField(lo, "midiNote", 0, 127, v))
                dl.midiNote = static_cast<std::uint8_t>(v);
            if (intField(lo, "offsetMs", -20, 20, v))
                dl.offsetMs = static_cast<std::int8_t>(v);
            dl.muted = hba::core::jsonBoolOr(lo, "muted");
            dl.solo = hba::core::jsonBoolOr(lo, "solo");
        }
    }

    if (value.contains("fill")) {
        const Json& fo = value["fill"];
        if (!fo.is_object()) {
            fail(error, "\"fill\" must be an object");
            return std::nullopt;
        }
        int beats = 2;
        if (fo.contains("beats")) {
            if (!fo["beats"].is_number_integer() ||
                (fo["beats"].get<int>() != 1 && fo["beats"].get<int>() != 2 && fo["beats"].get<int>() != 4)) {
                fail(error, "fill \"beats\" must be 1, 2 or 4");
                return std::nullopt;
            }
            beats = fo["beats"].get<int>();
        }
        p.fill.beats = static_cast<std::uint8_t>(beats);
        if (fo.contains("lanes") && fo["lanes"].is_object()) {
            for (const auto& [key, text] : fo["lanes"].items()) {
                const int lane = laneOfKey(key);
                if (lane < 0) {
                    fail(error, "unknown fill lane \"" + key + "\"");
                    return std::nullopt;
                }
                if (!readSteps(text, p.fill.steps[static_cast<std::size_t>(lane)].data(), kStepsPerBar, true, false,
                               Layer::groove, "fill." + key, error))
                    return std::nullopt;
            }
        }
    }

    if (value.contains("edits") && value["edits"].is_array()) {
        for (const auto& edit : value["edits"]) {
            if (!edit.is_object())
                continue;
            const int lane = laneOfKey(hba::core::jsonStringOr(edit, "lane"));
            const bool inFill = hba::core::jsonBoolOr(edit, "fill");
            int step = 0;
            if (lane < 0 || !intField(edit, "step", 0, (inFill ? kStepsPerBar : steps) - 1, step)) {
                fail(error, "edit needs a known \"lane\" and a \"step\"");
                return std::nullopt;
            }
            const auto li = static_cast<std::size_t>(lane);
            const auto si = static_cast<std::size_t>(step);
            applyEdit(edit, inFill ? p.fill.steps[li][si] : p.lanes[li].steps[si]);
        }
    }
    // Only the fill window is part of the fill: characters and edits before it are ignored.
    const auto fillFrom = static_cast<std::size_t>(fillWindowStart(p.fill));
    for (auto& row : p.fill.steps) {
        for (std::size_t step = 0; step < fillFrom; ++step)
            row[step] = {};
    }
    return p;
}

Json writePattern(const Pattern& pattern) {
    Json obj = Json::object();
    obj["bars"] = patternBars(pattern);
    const int steps = patternSteps(pattern);
    Json edits = Json::array();

    Json lanes = Json::object();
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const DrumLane& dl = pattern.lanes[static_cast<std::size_t>(lane)];
        const std::string key(kLaneKeys[static_cast<std::size_t>(lane)]);
        Json lo = Json::object();
        for (std::size_t layer = 0; layer < kLayerKeys.size(); ++layer) {
            bool any = false;
            std::string text = stepString(dl.steps.data(), steps, false, static_cast<Layer>(layer), any);
            if (any)
                lo[std::string(kLayerKeys[layer])] = std::move(text);
        }
        if (dl.length != 0)
            lo["length"] = static_cast<int>(dl.length);
        if (dl.midiNote != 0)
            lo["midiNote"] = static_cast<int>(dl.midiNote);
        if (dl.offsetMs != 0)
            lo["offsetMs"] = static_cast<int>(dl.offsetMs);
        if (dl.muted)
            lo["muted"] = true;
        if (dl.solo)
            lo["solo"] = true;
        if (!lo.empty())
            lanes[key] = std::move(lo);
        for (int step = 0; step < steps; ++step) {
            const Step& s = dl.steps[static_cast<std::size_t>(step)];
            if (!needsEdit(s))
                continue;
            Json e = editFor(s);
            e["lane"] = key;
            e["step"] = step;
            edits.push_back(std::move(e));
        }
    }
    obj["lanes"] = std::move(lanes);

    Json fill = Json::object();
    fill["beats"] = kStepsPerBar / 4 - fillWindowStart(pattern.fill) / 4;
    Json fillLanes = Json::object();
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        auto row = pattern.fill.steps[static_cast<std::size_t>(lane)];
        for (int step = 0; step < fillWindowStart(pattern.fill); ++step)
            row[static_cast<std::size_t>(step)] = {};
        const std::string key(kLaneKeys[static_cast<std::size_t>(lane)]);
        bool any = false;
        std::string text = stepString(row.data(), kStepsPerBar, true, Layer::groove, any);
        if (any)
            fillLanes[key] = std::move(text);
        for (int step = 0; step < kStepsPerBar; ++step) {
            const Step& s = row[static_cast<std::size_t>(step)];
            if (!needsEdit(s))
                continue;
            Json e = editFor(s);
            e["lane"] = key;
            e["step"] = step;
            e["fill"] = true;
            edits.push_back(std::move(e));
        }
    }
    fill["lanes"] = std::move(fillLanes);
    obj["fill"] = std::move(fill);

    if (!edits.empty())
        obj["edits"] = std::move(edits);
    return obj;
}

std::optional<GrooveBank> readGrooveBank(const Json& value, std::string* error) {
    if (!value.is_object()) {
        fail(error, "bank is not a JSON object");
        return std::nullopt;
    }
    GrooveBank bank{};
    bank.scenes = {};
    if (value.contains("scenes")) {
        if (!value["scenes"].is_array()) {
            fail(error, "\"scenes\" must be an array");
            return std::nullopt;
        }
        const auto& arr = value["scenes"];
        for (std::size_t i = 0; i < arr.size() && i < bank.scenes.size(); ++i) {
            const auto pattern = readPattern(arr[i], error);
            if (!pattern)
                return std::nullopt;
            bank.scenes[i] = *pattern;
        }
    }
    if (value.contains("slicer") && value["slicer"].is_object()) {
        const Json& so = value["slicer"];
        int v = 0;
        if (intField(so, "length", 1, kMaxSlicerSteps, v))
            bank.slicerLength = static_cast<std::uint8_t>(v);
        if (so.contains("steps") && !readSteps(so["steps"], bank.slicerSteps.data(), kMaxSlicerSteps, false, false,
                                               Layer::groove, "slicer.steps", error))
            return std::nullopt;
        if (so.contains("edits") && so["edits"].is_array()) {
            for (const auto& edit : so["edits"]) {
                int step = 0;
                if (edit.is_object() && intField(edit, "step", 0, kMaxSlicerSteps - 1, step))
                    applyEdit(edit, bank.slicerSteps[static_cast<std::size_t>(step)]);
            }
        }
    }
    return bank;
}

Json writeGrooveBank(const GrooveBank& bank) {
    Json obj = Json::object();
    Json scenes = Json::array();
    for (const auto& pattern : bank.scenes)
        scenes.push_back(writePattern(pattern));
    obj["scenes"] = std::move(scenes);

    Json slicer = Json::object();
    const int length = std::clamp(static_cast<int>(bank.slicerLength), 1, kMaxSlicerSteps);
    slicer["length"] = length;
    bool any = false;
    slicer["steps"] = stepString(bank.slicerSteps.data(), length, true, Layer::groove, any);
    Json edits = Json::array();
    for (int step = 0; step < length; ++step) {
        const Step& s = bank.slicerSteps[static_cast<std::size_t>(step)];
        if (!needsEdit(s))
            continue;
        Json e = editFor(s);
        e["step"] = step;
        edits.push_back(std::move(e));
    }
    if (!edits.empty())
        slicer["edits"] = std::move(edits);
    obj["slicer"] = std::move(slicer);
    return obj;
}

} // namespace detail

std::optional<Pattern> parsePattern(std::string_view json, std::string* error, std::string* name) {
    const detail::Json root = detail::Json::parse(json, nullptr, false);
    if (!root.is_object()) {
        if (error != nullptr)
            *error = "pattern is not a JSON object";
        return std::nullopt;
    }
    const auto format = root.find("format");
    if (format == root.end() || !format->is_number_integer() || format->get<int>() < kGrooveFormat) {
        if (error != nullptr)
            *error = std::string(kOldFormatError);
        return std::nullopt;
    }
    if (name != nullptr)
        *name = hba::core::jsonStringOr(root, "name");
    return detail::readPattern(root, error);
}

std::string serialisePattern(const Pattern& pattern, std::string_view name) {
    detail::Json root = detail::Json::object();
    root["format"] = kGrooveFormat;
    if (!name.empty())
        root["name"] = std::string(name);
    root.update(detail::writePattern(pattern));
    return root.dump(2) + "\n";
}

std::optional<GrooveBank> parseGrooveBank(std::string_view json, std::string* error) {
    return detail::readGrooveBank(detail::Json::parse(json, nullptr, false), error);
}

std::string serialiseGrooveBank(const GrooveBank& bank) { return detail::writeGrooveBank(bank).dump(); }

} // namespace cl
