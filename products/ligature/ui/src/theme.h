// SPDX-License-Identifier: AGPL-3.0-or-later
// Ligature 2 editor state, colours and drawing helpers. Everything is laid out in design pixels (1240 x 760) and
// multiplied by Context::scale when drawn.
#pragma once

#include "lg/ui/model.h"

#include <hba/ui/theme.h>
#include <visage/graphics.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lg::ui {

using hba::ui::Live;
using hba::ui::Weight;

enum class SelKind : std::uint8_t { item, chord, part };

struct Selection {
    SelKind kind = SelKind::item;
    ItemRef item;             // item and chord selections: the item they belong to
    int chord = 0;            // chord selections: the span index
    Role role = Role::melody; // part selections
};

// What the editor shows, beside the song: focus, selection and the view toggles. Not saved with the plugin.
struct Context {
    explicit Context(Model& m) : model(m), loadsSeen(m.document().loadCount()) {}

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;

    ItemRef focus = ItemRef::section(0);
    Selection sel{SelKind::chord, ItemRef::section(0), 1, Role::melody};
    bool follow = true;
    bool lens = true;
    Mutation mutation = Mutation::twist;
    DragScope dragScope = DragScope::item;
    std::uint64_t revision = 1;  // bumped by every change to the state above
    std::uint64_t loadsSeen = 0; // Document::loadCount() when the view last reset for a new song

    float px(float design) const { return design * scale; }
    Document& doc() const { return model.document(); }
    const Song& song() const { return model.document().song(); }
    void changed() { ++revision; }

    // Manual focus: Follow turns off and the item itself is selected.
    void focusOn(ItemRef item);
    void selectChord(ItemRef item, int span);
    void selectPart(Role role);
    void edit(const std::function<void(Document&)>& change);
    // Keeps focus and selection inside the song after it changes shape.
    void validate();
    // After section `removed` of `oldCount` is deleted: later items move down, the removed one gives way to its
    // neighbour, so the view keeps showing what the user was looking at.
    void sectionRemoved(int removed, int oldCount);
    // A new song arrived (preset or host state): back to section A, its second chord, Follow on.
    void songLoaded();
    // Follow: the focus moves to the playing item; a selected part stays selected.
    void followTo(ItemRef playing);
};

namespace colour {
using hba::ui::colour::edge;
using hba::ui::colour::panel;
using hba::ui::colour::raised;
using hba::ui::colour::text;
using hba::ui::colour::textDim;
using hba::ui::colour::textFaint;
using hba::ui::colour::track;
using hba::ui::colour::window;
using hba::ui::colour::withAlpha;

inline constexpr unsigned white = 0xffffffff;
inline constexpr unsigned cell = 0xff0d1218;     // lane wells
inline constexpr unsigned rule = 0xff131a22;     // the line under the top bar
inline constexpr unsigned ink = 0xff0a0d12;      // dark text on bright fills
inline constexpr unsigned buttonOn = 0xff1c2530; // a toggle that is on
inline constexpr unsigned buttonOnEdge = 0xff34414f;
inline constexpr unsigned segmentOn = 0xff26313e;
inline constexpr unsigned hoverEdge = 0xff2c3846;
inline constexpr unsigned focusRing = 0xffdfe6ee;
inline constexpr unsigned gridBar = 0xff202a36;
inline constexpr unsigned gridBeat = 0xff151c25;
inline constexpr unsigned quiet = 0xff3b4a5a; // voice lines that move, faint ruler beats
inline constexpr unsigned knob = 0xff222c38;  // launch and fire buttons
inline constexpr unsigned knobHover = 0xff2e3a48;
inline constexpr unsigned whiteKey = 0xffcfd6de;
inline constexpr unsigned blackKey = 0xff151b22;
inline constexpr unsigned meterTrack = 0xff1a222c;
inline constexpr unsigned hot = 0xffff6b5b;

// oklch(L C h / alpha) as ARGB.
unsigned oklch(float lightness, float chroma, float hueDegrees, float alpha = 1.0f);
unsigned role(Role r);
unsigned keyColour(Key k, float lightness = 0.72f, float chroma = 0.15f, float alpha = 1.0f);
unsigned mix(unsigned a, unsigned b, float t); // t = 0: a

unsigned gold();
unsigned stutter();
} // namespace colour

// Text in design pixels. CSS 600 and 700 both map to Inter SemiBold, the heaviest cut the studio embeds.
float textWidth(std::string_view text, float size, Weight weight = Weight::medium);
std::string ellipsize(std::string_view text, float size, Weight weight, float width);
std::vector<std::string> wrapText(std::string_view text, float size, Weight weight, float width);
std::string upper(std::string_view text);

// Drawing in design pixels.
struct Paint {
    visage::Canvas& canvas;
    float s = 1.0f;

    float px(float v) const { return v * s; }
    void rect(unsigned argb, float x, float y, float w, float h, float radius = 0.0f) const;
    void border(unsigned argb, float x, float y, float w, float h, float radius, float thickness = 1.0f) const;
    void hgrad(unsigned left, unsigned right, float x, float y, float w, float h, float radius = 0.0f) const;
    void hgrad(const visage::Gradient& gradient, float x, float y, float w, float h, float radius = 0.0f) const;
    void vgrad(unsigned top, unsigned bottom, float x, float y, float w, float h, float radius = 0.0f) const;
    void circle(unsigned argb, float cx, float cy, float radius) const;
    void ring(unsigned argb, float cx, float cy, float radius, float thickness) const;
    void line(unsigned argb, float x1, float y1, float x2, float y2, float thickness = 1.0f) const;
    // A path built in design pixels, stroked.
    void stroke(const visage::Path& designPath, unsigned argb, float thickness, std::vector<float> dash = {}) const;
    void text(std::string_view str, float size, Weight weight, unsigned argb, float x, float y, float w, float h,
              visage::Font::Justification justification = visage::Font::kLeft) const;
    // Text whose baseline sits at `baseline` (CSS SVG <text y=...>).
    void textAt(std::string_view str, float size, Weight weight, unsigned argb, float x, float baseline,
                visage::Font::Justification justification = visage::Font::kLeft, float width = 600.0f) const;
    // Uppercase labels with CSS letter-spacing (em); returns the drawn width.
    float spaced(std::string_view str, float size, Weight weight, unsigned argb, float x, float baseline,
                 float spacingEm) const;
    // Draws `fn` clipped to a rectangle (CSS overflow: hidden on a straight edge).
    template <typename Fn> void clipped(float x, float y, float w, float h, Fn&& fn) const {
        canvas.saveState();
        canvas.trimClampBounds(px(x), px(y), px(w), px(h));
        fn();
        canvas.restoreState();
    }
    void dashedRoundRect(unsigned argb, float x, float y, float w, float h, float radius, float thickness, float dash,
                         float gap) const;
    void playIcon(unsigned argb, float cx, float cy, float size) const;
    void stopIcon(unsigned argb, float cx, float cy, float size) const;
    void diceIcon(unsigned argb, float cx, float cy, float size) const;
};

float spacedWidth(std::string_view str, float size, Weight weight, float spacingEm);

// Words the editor shows about the music.
std::string itemName(const Song& song, ItemRef item);    // "Verse", or a bridge's route name
std::string itemLetters(const Song& song, ItemRef item); // "A", "A -> B" (with an arrow)
Route bridgeRoute(const Song& song, int bridge);
std::string numeralText(const Pattern& pattern, const Span& span); // "VImaj9", or "bVI -> IV" on a bridge
std::string functionText(Function f);                              // "Home. Settled; ..." for the lens
std::string formatBpm(double bpm);
inline constexpr std::string_view kArrow = "\xe2\x86\x92"; // ->

// The chord before a span (wrapping), for "keeps ... from ..."; a bridge's first chord looks back into its section.
const Span* previousSpan(Document& doc, const Pattern& pattern, int span);

} // namespace lg::ui
