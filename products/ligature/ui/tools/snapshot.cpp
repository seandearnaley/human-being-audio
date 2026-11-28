// SPDX-License-Identifier: AGPL-3.0-or-later
// Renders the Ligature editor over the fake model to a PNG.
#include "lg/ui/editor.h"
#include "lg/ui/fake_model.h"

#include <visage/ui.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
// Synthetic input at window coordinates (design pixels at the default size), for the "interact" mode.
visage::MouseEvent eventAt(visage::Frame& root, float x, float y, visage::Frame*& target) {
    visage::MouseEvent e;
    e.window_position = {x, y};
    target = root.frameAtPoint(e.window_position);
    if (target != nullptr) {
        e.event_frame = target;
        e.position = e.window_position - target->positionInWindow();
    }
    e.button_id = visage::kMouseButtonLeft;
    return e;
}

void click(visage::Frame& root, float x, float y) {
    visage::Frame* target = nullptr;
    auto e = eventAt(root, x, y, target);
    if (target == nullptr)
        return;
    e.is_down = true;
    e.button_state = visage::kMouseButtonLeft;
    target->processMouseDown(e);
    e.is_down = false;
    e.button_state = 0;
    target->processMouseUp(e);
}

bool wheel(visage::Frame& root, float x, float y, float deltaY) {
    visage::Frame* target = nullptr;
    auto e = eventAt(root, x, y, target);
    e.wheel_delta_y = deltaY;
    for (visage::Frame* f = target; f != nullptr; f = f->parent()) {
        e.position = e.window_position - f->positionInWindow();
        if (f->processMouseWheel(e))
            return true;
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::fprintf(
            stderr,
            "usage: lg-ui-snapshot <out.png> [width height] "
            "[chord|bridge|part|section|live|hold|small|lensoff|four|fourlive|longname|sixteen|played|interact]\n");
        return 2;
    }
    int width = lg::ui::kDefaultWidth;
    int height = lg::ui::kDefaultHeight;
    std::string mode = "chord";
    if (argc == 3) {
        mode = argv[2];
    } else if (argc >= 4) {
        width = std::atoi(argv[2]);
        height = std::atoi(argv[3]);
        if (argc == 5)
            mode = argv[4];
    }
    if (mode == "small" && argc <= 3) {
        width = lg::ui::kMinWidth;
        height = lg::ui::kMinHeight;
    }

    using lg::ItemRef;
    using lg::Role;
    lg::ui::FakeModel model;
    lg::ui::Editor editor(model);

    if (mode == "played") { // a chord held on the MIDI input offers itself for the selected slot
        model.played = lg::makeChord(8, lg::Quality::maj9);
        mode = "chord";
    }
    if (mode == "chord" || mode == "small") {
        editor.selectChord(ItemRef::section(0), 1);
    } else if (mode == "bridge") {
        model.showInBridge();
        editor.setFollow(true); // the focus follows the playing bridge
    } else if (mode == "part") {
        // Pin two melody notes so the pinned outline shows.
        auto& doc = model.document();
        const auto notes = doc.pattern(ItemRef::section(0)).notes;
        int pinned = 0;
        for (const auto& n : notes) {
            if (n.role != Role::melody || (pinned == 0 ? n.start < 8.0f : n.start < 36.0f))
                continue;
            model.edit([n](lg::Document& d) { d.togglePin(ItemRef::section(0), n); });
            if (++pinned == 2)
                break;
        }
        editor.focusItem(ItemRef::section(0));
        editor.selectPart(Role::melody);
    } else if (mode == "section") {
        editor.focusItem(ItemRef::section(0));
    } else if (mode == "live") {
        model.showLaunchQueued();
        editor.selectChord(ItemRef::section(0), 1);
        editor.setFollow(true);
    } else if (mode == "hold") {
        model.showHolding();
        editor.setFollow(true);
    } else if (mode == "lensoff") {
        editor.selectChord(ItemRef::section(0), 1);
        editor.setLens(false);
    } else if (mode == "four") {
        // Four sections: the strip shrinks its tiles and knots to fit.
        model.edit([](lg::Document& d) {
            d.addSection();
            d.addSection();
        });
        editor.focusItem(ItemRef::bridge(2));
    } else if (mode == "fourlive") {
        // Four sections with a launch queued: the "LANDS IN" label moves to the key line of a narrow tile.
        model.edit([](lg::Document& d) {
            d.addSection();
            d.addSection();
        });
        model.showLaunchQueued();
        editor.setFollow(true);
    } else if (mode == "longname") {
        // A long section name while playing: the position line stops before Lens.
        model.edit([](lg::Document& d) { d.setSectionName(0, "Extended Instrumental Breakdown"); });
        model.showLaunchQueued();
        editor.setFollow(true);
    } else if (mode == "sixteen") {
        // A 16-bar section from saved state: bar numbers only on the ruler, no Length button lit.
        model.edit([](lg::Document& d) { d.setSectionBars(0, 16); });
        editor.focusItem(ItemRef::section(0));
    } else if (mode == "interact") {
        // Drives the editor with synthetic clicks and a wheel notch, then reports what changed in the song.
        editor.selectChord(ItemRef::section(0), 1);
        editor.renderToPng(argv[1], lg::ui::kDefaultWidth, lg::ui::kDefaultHeight);
        auto& root = editor.rootFrame();
        auto& doc = model.document();
        click(root, 576.0f, 239.0f); // the third chord, Bbm9
        editor.renderToPng(argv[1], lg::ui::kDefaultWidth, lg::ui::kDefaultHeight);
        const bool used = wheel(root, 576.0f, 239.0f, 1.0f);    // one notch up over the selected chord
        const bool passed = !wheel(root, 300.0f, 239.0f, 1.0f); // over another chord: the event goes on
        click(root, 154.0f + 5.5f * 760.0f / 64.0f, 616.0f);    // stutter cell 5
        click(root, 70.0f, 154.0f + 318.0f + 10.0f);            // the Arp lane header
        std::printf("inversion of chord 3: %d (wheel used %d, passed elsewhere %d)\n",
                    doc.song().sections[0].chords[2].inversion, used ? 1 : 0, passed ? 1 : 0);
        const auto& cells = doc.song().sections[0].stutter;
        const auto cell = cells.find(5);
        std::printf("stutter cell 5: %s\n",
                    cell == cells.end() ? "none" : std::string(lg::stutterString(cell->second)).c_str());
        std::printf("edits: %d\n", model.edits());
    } else {
        std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
        return 2;
    }

    if (width != lg::ui::kDefaultWidth) // lay out at the default size first, so the resize path is what gets drawn
        editor.renderToPng(argv[1]);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d (%s)\n", argv[1], width, height, mode.c_str());
    return 0;
}
