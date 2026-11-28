// SPDX-License-Identifier: AGPL-3.0-or-later
// What the Ligature editor needs from its host: the document to edit, the performance controls, and telemetry.
// The plugin implements this over the processor; FakeModel implements it in memory for snapshots and tests.
#pragma once

#include <hba/ui/accessible_controls.h>

#include "lg/document.h"
#include "lg/engine.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lg::ui {

struct PresetInfo {
    std::string name;
    std::string category;
};

enum class DragScope : std::uint8_t {
    item,       // the focused section or bridge
    song,       // every section and bridge in strip order
    performance // everything played since Play, bridges included
};

struct Model : public hba::ui::PresetMenuHost {
    virtual ~Model() = default;

    // The song. Read freely; change it only through edit(), which takes the undo step, republishes the song to the
    // engine and saves it into the plugin state. Undo, redo and A/B also go through the model.
    virtual Document& document() = 0;
    virtual void edit(const std::function<void(Document&)>& change) = 0;
    // Continuous gestures (the density slider): `change` runs for every move; the model publishes at most once a frame.
    virtual void editLive(const std::function<void(Document&)>& change) { edit(change); }

    // Performance. All of these work while the host plays; while it is stopped they drive the preview clock.
    virtual void launch(int section, bool direct) = 0;
    virtual void fire(int bridge) = 0;
    virtual void hold(int bridge, bool down) = 0;
    virtual void togglePreview(ItemRef from) = 0;
    virtual int land() const = 0; // 1, 2 or 4 bars
    virtual void setLand(int bars) = 0;
    virtual bool beat() const = 0; // audition drums
    virtual void setBeat(bool on) = 0;
    // The built-in sound: the studio's own instruments (true) or the quick sketch synth.
    virtual bool studioVoices() const { return false; }
    virtual void setStudioVoices(bool) {}
    // Plays one chord through the sketch synth (used while stopped, when a chord is clicked or swapped).
    virtual void auditionChord(ItemRef item, int span) = 0;

    // Followers: -1 leads (all parts, channels 1-4); 0..3 follows the leader and outputs that part on channel 1.
    virtual int outputRole() const { return -1; }
    virtual void setOutputRole(int) {}
    virtual bool leaderFound() const { return true; }

    // The chord last held on the MIDI input (three or more notes outside the one-key range), for "Use played chord".
    virtual std::optional<Chord> playedChord() const { return std::nullopt; }

    // Telemetry, refreshed once per UI frame.
    virtual void refreshVisuals() {}
    virtual const EngineTelemetry& telemetry() = 0;

    // MIDI drag-out. `role` empty: one track per part.
    virtual void dragMidi(DragScope scope, ItemRef item, std::optional<Role> role) = 0;
    virtual std::string exportStatus() const { return {}; }

    virtual std::string presetName() const = 0;
    virtual std::vector<PresetInfo> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() {}

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool isSlotB() const = 0;
    virtual void toggleAB() = 0;
    virtual void copyAB() = 0;
};

} // namespace lg::ui
