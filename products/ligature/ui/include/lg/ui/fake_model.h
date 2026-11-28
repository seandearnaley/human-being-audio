// SPDX-License-Identifier: AGPL-3.0-or-later
// A Model over an in-memory Document with canned telemetry, for snapshots and UI tests.
#pragma once

#include "lg/ui/model.h"

namespace lg::ui {

class FakeModel final : public Model {
  public:
    FakeModel();

    Document& document() override { return doc_; }
    void edit(const std::function<void(Document&)>& change) override;

    void launch(int section, bool direct) override;
    void fire(int bridge) override;
    void hold(int bridge, bool down) override;
    void togglePreview(ItemRef from) override;
    int land() const override { return land_; }
    void setLand(int bars) override { land_ = bars; }
    bool beat() const override { return beat_; }
    void setBeat(bool on) override { beat_ = on; }
    void auditionChord(ItemRef item, int span) override;

    const EngineTelemetry& telemetry() override { return telemetry_; }
    std::optional<Chord> playedChord() const override { return played; }
    bool studioVoices() const override { return studio_; }
    void setStudioVoices(bool on) override { studio_ = on; }
    std::optional<Chord> played; // what the snapshot pretends is held on the MIDI input

    void dragMidi(DragScope, ItemRef, std::optional<Role>) override {}

    std::string presetName() const override { return doc_.song().name; }
    std::vector<PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;

    bool canUndo() const override { return doc_.canUndo(); }
    bool canRedo() const override { return doc_.canRedo(); }
    void undo() override { doc_.undo(); }
    void redo() override { doc_.redo(); }
    bool isSlotB() const override { return doc_.isSlotB(); }
    void toggleAB() override { doc_.toggleAB(); }
    void copyAB() override { doc_.copyAB(); }

    // Canned performances for screenshots.
    void showStopped();
    void showLaunchQueued(); // A plays; B was launched, so the A -> B bridge starts in a bar
    void showInBridge();     // the host plays; inside the A -> B bridge, a bar from landing on B
    void showHolding();      // the A -> B bridge held: it loops its first bar until release

    int edits() const { return edits_; }

  private:
    bool studio_ = true;
    void play(ItemRef item, int bars, float step, double beat);
    void light(ItemRef item, float step); // sounding masks from the notes that play at `step`

    Document doc_;
    EngineTelemetry telemetry_;
    int land_ = 4;
    bool beat_ = true;
    int preset_ = 0;
    int edits_ = 0;
};

} // namespace lg::ui
