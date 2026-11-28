// SPDX-License-Identifier: AGPL-3.0-or-later
#include "inspector.h"

#include "timeline.h"

#include <visage/ui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace lg::ui {

namespace {
constexpr float kPadX = 14.0f;
constexpr float kTop = 12.0f;
constexpr float kContentW = 272.0f;
constexpr float kRight = kPadX + kContentW;
constexpr float kRowH = 26.0f;

// The palette constellation: the mock's 340-unit SVG drawn 262 px wide.
constexpr float kRingSize = 262.0f;
constexpr float kRingK = kRingSize / 340.0f;
constexpr float kRingX = kPadX + (kContentW - kRingSize) * 0.5f;
constexpr float kRingY = 51.0f;
constexpr float kRingCX = kRingX + 170.0f * kRingK;
constexpr float kRingCY = kRingY + 170.0f * kRingK;
constexpr float kChipW = 56.0f * kRingK;
constexpr float kChipH = 21.0f * kRingK;
constexpr std::array<float, 3> kRingRadius{64.0f, 104.0f, 148.0f};
// Fixed chip slots (x, y from the centre, in SVG units) laid out so no two chips can touch.
constexpr std::array<std::array<float, 2>, 6> kSlots0{{{0, -58}, {62, -30}, {62, 30}, {0, 58}, {-62, 30}, {-62, -30}}};
constexpr std::array<std::array<float, 2>, 6> kSlots1{
    {{0, -100}, {86, -62}, {86, 62}, {0, 100}, {-86, 62}, {-86, -62}}};
constexpr std::array<std::array<float, 2>, 8> kSlots2{
    {{66, -140}, {140, -30}, {140, 30}, {66, 140}, {-66, 140}, {-140, 30}, {-140, -30}, {-66, -140}}};

constexpr float kCaptionY = kRingY + kRingSize - 8.0f + 8.0f;
constexpr float kChordRowsY = kCaptionY + 30.0f + 8.0f;

constexpr unsigned kRingLine = 0xff17202a;
constexpr unsigned kChipFill0 = 0xff1b2430;
constexpr unsigned kChipFill1 = 0xff141b24;
constexpr unsigned kChipFill2 = 0xff10161d;
constexpr unsigned kChipEdge0 = 0xff2e3a48;
constexpr unsigned kChipEdge1 = 0xff3b4a5a;
constexpr unsigned kLegendDash = 0xff5a6b7c;
constexpr unsigned kFnChip = 0xff1c2530;
constexpr unsigned kRouteOn = 0xff1b2028;
constexpr unsigned kRouteHover = 0xff34414f;

// "Use Cm9", right of the caption line while a chord is held on the MIDI input.
float playedWidth(const std::string& label) {
    return std::min(96.0f, std::ceil(textWidth(label, 10.5f, Weight::semibold)) + 18.0f);
}

visage::Gradient film3() {
    return visage::Gradient(colour::oklch(0.8f, 0.13f, 200.0f), colour::oklch(0.8f, 0.13f, 300.0f),
                            colour::oklch(0.85f, 0.12f, 60.0f));
}

float slotX(int ring, int j) {
    return ring == 0   ? kSlots0[static_cast<std::size_t>(j)][0]
           : ring == 1 ? kSlots1[static_cast<std::size_t>(j)][0]
                       : kSlots2[static_cast<std::size_t>(j)][0];
}
float slotY(int ring, int j) {
    return ring == 0   ? kSlots0[static_cast<std::size_t>(j)][1]
           : ring == 1 ? kSlots1[static_cast<std::size_t>(j)][1]
                       : kSlots2[static_cast<std::size_t>(j)][1];
}
int slotCount(int ring) { return ring == 2 ? 8 : 6; }

std::string barPosition(float start) {
    const int step = static_cast<int>(std::lround(start));
    std::string text = "BAR " + std::to_string(step / kStepsPerBar + 1);
    if (step % kStepsPerBar != 0)
        text += "." + std::to_string((step % kStepsPerBar) / 4 + 1);
    return text;
}

std::string ringWord(Ring r) {
    switch (r) {
    case Ring::inKey:
        return "in key";
    case Ring::borrowed:
        return "borrowed";
    case Ring::outside:
        return "outside";
    }
    return {};
}

std::string plural(int n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

// A segmented control at the right edge of a row.
void placeRight(Segmented& seg, const Context& c, float y) {
    const float w = seg.designWidth();
    place(seg, c, kRight - w, y, w, kRowH);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Density: a slider that edits live, one undo step per drag.

// Keyboard: arrows nudge it. (The shared accessibility layer only mirrors host parameters and actions, and density
// is song state, so the slider is not in the screen-reader tree.)
class Inspector::Slider final : public visage::Frame {
  public:
    explicit Slider(Context& context) : context_(context) {
        setAcceptsKeystrokes(true);
        setName("Density");
    }

    void setRole(Role role) {
        role_ = role;
        redraw();
    }

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / p.s;
        const float h = height() / p.s;
        const float v = context_.song().part(role_).density;
        const unsigned accent = colour::role(role_);
        const float x0 = 6.0f;
        const float tw = w - 12.0f;
        p.rect(colour::track, x0, h * 0.5f - 2.0f, tw, 4.0f, 2.0f);
        p.rect(accent, x0, h * 0.5f - 2.0f, tw * v, 4.0f, 2.0f);
        p.circle(accent, x0 + tw * v, h * 0.5f, dragging_ || hovered_ ? 7.0f : 6.0f);
        p.circle(colour::ink, x0 + tw * v, h * 0.5f, 2.0f);
        hba::ui::drawKeyboardFocus(canvas, *this, context_.scale);
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }
    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }
    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        dragging_ = true;
        set(e.position.x, true);
    }
    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragging_)
            set(e.position.x, false);
    }
    void mouseUp(const visage::MouseEvent&) override {
        dragging_ = false;
        context_.changed();
        redraw();
    }
    bool keyPress(const visage::KeyEvent& key) override {
        using Key = visage::KeyCode;
        const auto code = key.keyCode();
        if (key.isMainModifier() || (code != Key::Left && code != Key::Right && code != Key::Up && code != Key::Down))
            return false;
        const float step =
            (code == Key::Left || code == Key::Down ? -1.0f : 1.0f) * (key.isShiftDown() ? 0.01f : 0.05f);
        apply(std::clamp(context_.song().part(role_).density + step, 0.0f, 1.0f), true);
        return true;
    }
    void focusChanged(bool, bool) override { redraw(); }

  private:
    void set(float x, bool gestureStart) {
        const float s = context_.scale;
        const float v = std::clamp((x / s - 6.0f) / std::max(1.0f, width() / s - 12.0f), 0.0f, 1.0f);
        apply(v, gestureStart);
    }
    void apply(float v, bool gestureStart) {
        const Role role = role_;
        context_.model.editLive([role, v, gestureStart](Document& d) { d.setDensity(role, v, gestureStart); });
        redraw();
    }

    Context& context_;
    Role role_ = Role::melody;
    bool dragging_ = false;
    bool hovered_ = false;
};

// ---------------------------------------------------------------------------------------------------------------
// The progression library, grouped by pack, scrolling.

class Inspector::Library final : public visage::ScrollableFrame {
  public:
    explicit Library(Context& context) : context_(context), rows_(context, content_) {
        addScrolledChild(&content_);
        content_.setIgnoresMouseEvents(true, true);
        content_.onDraw() += [this](visage::Canvas& canvas) { drawHeaders(canvas); };
    }

    void sync(int section) {
        const auto& sec = context_.song().sections[static_cast<std::size_t>(section)];
        const auto list = progressionsFor(sec.key.mode);
        std::vector<std::string_view> packs;
        for (const auto* p : list)
            if (std::find(packs.begin(), packs.end(), p->pack) == packs.end())
                packs.push_back(p->pack);
        headers_.clear();
        std::vector<const Progression*> ordered;
        std::vector<float> ys;
        float y = 0.0f;
        for (const auto pack : packs) {
            headers_.push_back({std::string(pack), y});
            y += 20.0f;
            for (const auto* p : list) {
                if (p->pack != pack)
                    continue;
                ordered.push_back(p);
                ys.push_back(y);
                y += 30.0f;
            }
            y += 4.0f;
        }
        rows_.resize(ordered.size());
        rowY_ = ys;
        height_ = y;
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const Progression* prog = ordered[i];
            std::string numerals;
            for (const auto& chord : realise(*prog, sec.key, sec.bars)) {
                if (!numerals.empty())
                    numerals += " ";
                numerals += analyze(chord, sec.key).numeral;
            }
            auto& row = rows_[i];
            row.setup(Control::Look::custom, std::string(prog->name), [this, section, prog] {
                context_.edit([section, prog](Document& d) { d.loadProgression(section, *prog); });
            });
            row.setAccessibleName("Progression " + std::string(prog->name) + ", " + numerals);
            row.setPainter([numerals](const Paint& p, const Control& c) {
                const float w = c.width() / p.s;
                const float h = c.height() / p.s;
                p.rect(colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
                p.border(c.hovered() ? kRouteHover : colour::edge, 0.0f, 0.0f, w, h, 7.0f);
                const float nameW = textWidth(c.label(), 11.5f, Weight::semibold);
                p.text(c.label(), 11.5f, Weight::semibold, colour::text, 9.0f, 0.0f, nameW + 2.0f, h);
                const float avail = w - 18.0f - nameW - 8.0f;
                p.text(ellipsize(numerals, 10.5f, Weight::medium, avail), 10.5f, Weight::medium, colour::textDim,
                       w - 9.0f - avail, 0.0f, avail, h, visage::Font::kRight);
            });
        }
        layoutRows();
    }

    void resized() override {
        ScrollableFrame::resized();
        layoutRows();
    }

  private:
    void layoutRows() {
        const float s = context_.scale;
        const float w = width() / s - 8.0f;
        content_.setBounds(0.0f, 0.0f, w * s, height_ * s);
        for (std::size_t i = 0; i < rows_.size() && i < rowY_.size(); ++i)
            place(rows_[i], context_, 0.0f, rowY_[i], w, 28.0f);
        setScrollableHeight(height_ * s);
    }

    void drawHeaders(visage::Canvas& canvas) {
        const Paint p{canvas, context_.scale};
        for (const auto& [name, y] : headers_)
            p.spaced(upper(name), 9.5f, Weight::semibold, colour::textFaint, 1.0f, y + 14.0f, 0.09f);
    }

    Context& context_;
    visage::Frame content_;
    ControlPool rows_;
    std::vector<std::pair<std::string, float>> headers_;
    std::vector<float> rowY_;
    float height_ = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------

Inspector::Inspector(Context& context)
    : Panel(context), chips_(context, *this), voicing_(context), inversionDown_(context), inversionUp_(context),
      level_(context), voiceLead_(context), played_(context), styles_(context, *this), register_(context),
      rate_(context), amount_(context), clearPins_(context), mutate_(context), takes_(context, *this),
      routes_(context, *this), length_(context), keys_(context, *this), keyMode_(context), bars_(context) {
    density_ = std::make_unique<Slider>(context);
    library_ = std::make_unique<Library>(context);
    for (visage::Frame* f : std::initializer_list<visage::Frame*>{
             &voicing_, &inversionDown_, &inversionUp_, &level_, &voiceLead_, &played_, density_.get(), &register_,
             &rate_, &amount_, &clearPins_, &mutate_, &length_, &keyMode_, &bars_, library_.get()})
        addChild(f, false);

    const auto stepPainter = [](const Paint& p, const Control& c) {
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        if (c.hovered())
            p.rect(colour::segmentOn, 0.0f, 0.0f, w, h, 5.0f);
        p.text(c.label(), 13.0f, Weight::semibold, c.hovered() ? colour::text : colour::textDim, 0.0f, -1.0f, w, h,
               visage::Font::kCenter);
    };
    inversionDown_.setup(Control::Look::custom, "−", [this] {
        const auto sel = context_.sel;
        context_.edit([sel](Document& d) { d.stepInversion(sel.item.index, sel.chord, -1); });
    });
    inversionDown_.setAccessibleName("Lower inversion");
    inversionDown_.setPainter(stepPainter);
    inversionUp_.setup(Control::Look::custom, "+", [this] {
        const auto sel = context_.sel;
        context_.edit([sel](Document& d) { d.stepInversion(sel.item.index, sel.chord, 1); });
    });
    inversionUp_.setAccessibleName("Raise inversion");
    inversionUp_.setPainter(stepPainter);
    voiceLead_.setup(Control::Look::button, "On", [this] {
        const bool on = context_.song().voiceLead;
        context_.edit([on](Document& d) { d.setVoiceLead(!on); });
    });

    played_.setup(Control::Look::button, "Use", [this] {
        const auto sel = context_.sel;
        if (sel.kind != SelKind::chord || sel.item.isBridge() || playedName_.empty())
            return;
        const Chord chord = playedChord_;
        context_.edit([sel, chord](Document& d) { d.replaceChord(sel.item.index, sel.chord, chord); });
        if (!context_.model.telemetry().playing)
            context_.model.auditionChord(sel.item, sel.chord);
    });
    played_.setTextSize(10.5f);
    played_.setVisible(false);

    clearPins_.setup(Control::Look::button, "Clear", [this] {
        const ItemRef item = context_.focus;
        const Role role = context_.sel.role;
        context_.edit([item, role](Document& d) { d.clearPins(item, role); });
    });
    clearPins_.setTextSize(10.5f);
    mutate_.setup(Control::Look::custom, "Mutate", [this] {
        const Role role = context_.sel.role;
        const Mutation m = context_.mutation;
        context_.edit([role, m](Document& d) { d.mutate(role, m); });
    });
    mutate_.setPainter([this](const Paint& p, const Control& c) {
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        const unsigned accent = colour::role(context_.sel.role);
        p.rect(c.pressed() ? colour::mix(accent, colour::white, 0.2f) : accent, 0.0f, 0.0f, w, h, 7.0f);
        p.diceIcon(colour::ink, 18.0f, h * 0.5f, 12.0f);
        p.text("Mutate", 11.5f, Weight::semibold, colour::ink, 30.0f, 0.0f, w - 30.0f, h);
    });
}

Inspector::~Inspector() = default;

void Inspector::sync() {
    context_.validate();
    const auto& sel = context_.sel;
    if (sel.kind == SelKind::part)
        view_ = View::part;
    else if (sel.kind == SelKind::chord && !sel.item.isBridge())
        view_ = View::chord;
    else if (sel.item.isBridge())
        view_ = View::bridge;
    else
        view_ = View::section;

    // Only what changes visibility is touched: hiding a frame drops its keyboard focus.
    const bool chord = view_ == View::chord;
    const bool part = view_ == View::part;
    const bool bridge = view_ == View::bridge;
    const bool section = view_ == View::section;
    if (!chord) {
        chips_.hide();
        level_.setVisible(false);
        played_.setVisible(false);
        hover_ = -1;
    } else {
        played_.setVisible(!playedName_.empty());
    }
    if (!part) {
        styles_.hide();
        takes_.hide();
        rate_.setVisible(false);
        clearPins_.setVisible(false);
    }
    if (!bridge)
        routes_.hide();
    if (!section)
        keys_.hide();
    for (visage::Frame* f :
         std::initializer_list<visage::Frame*>{&voicing_, &inversionDown_, &inversionUp_, &voiceLead_})
        f->setVisible(chord);
    for (visage::Frame* f : std::initializer_list<visage::Frame*>{density_.get(), &register_, &amount_, &mutate_})
        f->setVisible(part);
    length_.setVisible(bridge);
    for (visage::Frame* f : std::initializer_list<visage::Frame*>{&keyMode_, &bars_, library_.get()})
        f->setVisible(section);

    switch (view_) {
    case View::chord:
        syncChord();
        break;
    case View::part:
        syncPart();
        break;
    case View::bridge:
        syncBridge();
        break;
    case View::section:
        syncSection();
        break;
    }
}

// ----- chord

// A chord held on the MIDI input offers itself for the selected slot.
void Inspector::live() {
    const auto held = context_.model.playedChord();
    const std::string name = held ? chordName(*held) : std::string();
    if (name == playedName_)
        return;
    playedName_ = name;
    if (held)
        playedChord_ = *held;
    played_.setLabel("Use " + name);
    played_.setAccessibleName("Use the played chord " + name + " here");
    played_.setVisible(view_ == View::chord && !name.empty());
    layoutChildren();
    redraw();
}

void Inspector::syncChord() {
    const int section = context_.sel.item.index;
    const int index = context_.sel.chord;
    auto& pattern = context_.doc().pattern(ItemRef::section(section));
    const auto& span = pattern.spans[static_cast<std::size_t>(index)];
    const Chord& chord = span.chord;

    palette_ = context_.doc().palette(section, index);
    chipEntry_.clear();
    chipSlot_.clear();
    for (int ring = 0; ring < 3; ++ring) {
        int used = 0;
        for (std::size_t e = 0; e < palette_.size() && used < slotCount(ring); ++e) {
            if (static_cast<int>(palette_[e].ring) != ring)
                continue;
            chipEntry_.push_back(static_cast<int>(e));
            chipSlot_.push_back({ring, used});
            ++used;
        }
    }
    if (hover_ >= static_cast<int>(chipEntry_.size()))
        hover_ = -1;
    chips_.resize(chipEntry_.size());
    for (std::size_t i = 0; i < chipEntry_.size(); ++i) {
        const int chip = static_cast<int>(i);
        const PaletteEntry entry = palette_[static_cast<std::size_t>(chipEntry_[i])];
        auto& c = chips_[i];
        const std::string name = chordName(entry.chord);
        c.setup(Control::Look::custom, name, [this, section, index, entry] {
            const Chord replacement = entry.chord;
            context_.edit([section, index, replacement](Document& d) { d.replaceChord(section, index, replacement); });
            if (!context_.model.telemetry().playing)
                context_.model.auditionChord(ItemRef::section(section), index);
        });
        c.setAccessibleName("Swap to " + name + ", " + ringWord(entry.ring));
        c.onHover = [this, chip](bool on) {
            if (on)
                hover_ = chip;
            else if (hover_ == chip)
                hover_ = -1;
            redraw();
        };
        c.setPainter([entry, name](const Paint& p, const Control& ctl) {
            const float w = ctl.width() / p.s;
            const float h = ctl.height() / p.s;
            const int ring = static_cast<int>(entry.ring);
            const float r = h * 0.5f;
            p.rect(ring == 0 ? kChipFill0 : ring == 1 ? kChipFill1 : kChipFill2, 0.5f, 0.5f, w - 1.0f, h - 1.0f, r);
            if (ctl.hovered())
                p.border(colour::focusRing, 0.0f, 0.0f, w, h, r, 1.1f);
            else if (ring == 0)
                p.border(kChipEdge0, 0.0f, 0.0f, w, h, r, 1.0f);
            else if (ring == 1)
                p.dashedRoundRect(kChipEdge1, 0.5f, 0.5f, w - 1.0f, h - 1.0f, r, 1.0f, 2.5f * kRingK * 1.2f,
                                  2.0f * kRingK * 1.2f);
            else {
                p.canvas.setColor(visage::Brush::linear(film3(), {0.0f, 0.0f}, {p.px(w), 0.0f}));
                p.canvas.roundedRectangleBorder(0.0f, 0.0f, p.px(w), p.px(h), p.px(r), std::max(1.0f, p.px(1.0f)));
            }
            const float size = name.size() > 7 ? 7.9f : 8.6f;
            p.text(ellipsize(name, size, Weight::semibold, w - 4.0f), size, Weight::semibold,
                   ring == 2 ? 0xffcfd8e2u : colour::text, 0.0f, 0.0f, w, h, visage::Font::kCenter);
        });
    }

    voicing_.set(
        {"Close", "Drop 2", "Spread"}, static_cast<int>(chord.style),
        [this, section, index](int v) {
            context_.edit(
                [section, index, v](Document& d) { d.setVoicingStyle(section, index, static_cast<VoicingStyle>(v)); });
        },
        "Voicing");
    if (hasLadder(chord.family))
        level_.set(
            {"Triad", "7", "9", "11+"}, levelOf(chord),
            [this, section, index](int level) {
                context_.edit([section, index, level](Document& d) { d.setLevel(section, index, level); });
            },
            "Colour");
    voiceLead_.setLabel(context_.song().voiceLead ? "On" : "Off");
    voiceLead_.setOn(context_.song().voiceLead);
    voiceLead_.setAccessibleName(std::string("Voice-leading ") + (context_.song().voiceLead ? "on" : "off"));
    inversionDown_.setEnabled(chord.inversion > -2);
    inversionUp_.setEnabled(chord.inversion < 2);

    level_.setVisible(hasLadder(chord.family));
}

void Inspector::drawChord(const Paint& p) {
    const int section = context_.sel.item.index;
    const int index = context_.sel.chord;
    const auto& song = context_.song();
    auto& pattern = context_.doc().pattern(ItemRef::section(section));
    if (index < 0 || index >= static_cast<int>(pattern.spans.size()))
        return;
    const auto& span = pattern.spans[static_cast<std::size_t>(index)];
    const auto& sec = song.sections[static_cast<std::size_t>(section)];
    const float hue = keyHue(sec.key);

    drawHeader(p, sectionLetter(section) + " · " + upper(sec.name) + " · " + barPosition(span.start),
               chordName(span.chord), context_.lens ? span.analysis.numeral : std::string(), 0, 64.0f);
    // function word chip (the theory lens)
    if (context_.lens) {
        const std::string word(functionWord(span.analysis.function));
        const float ww = spacedWidth(word, 9.0f, Weight::semibold, 0.1f);
        p.rect(kFnChip, kRight - ww - 12.0f, 31.0f, ww + 12.0f, 16.0f, 4.0f);
        p.spaced(word, 9.0f, Weight::semibold, colour::textDim, kRight - ww - 6.0f, 42.5f, 0.1f);
    }

    // rings
    for (float r : kRingRadius)
        p.ring(kRingLine, kRingCX, kRingCY, r * kRingK, 1.0f);
    const float cr = 28.0f * kRingK;
    p.circle(colour::mix(colour::panel, colour::oklch(0.6f, 0.12f, hue), 0.3f), kRingCX, kRingCY, cr);
    p.ring(colour::oklch(0.75f, 0.13f, hue), kRingCX, kRingCY, cr, 1.3f);
    const std::string name = chordName(span.chord);
    const float cs = (name.size() > 6 ? 11.0f : 13.0f) * kRingK * 1.08f;
    p.text(ellipsize(name, cs, Weight::semibold, 2.0f * cr), cs, Weight::semibold, colour::white, kRingCX - 30.0f,
           kRingCY - 10.0f, 60.0f, 20.0f, visage::Font::kCenter);

    // caption: the hovered chip, or the legend
    if (hover_ >= 0 && hover_ < static_cast<int>(chipEntry_.size())) {
        // The caption stops short of the "Use ..." button when one shows.
        const float captionW = playedName_.empty() ? kContentW : kContentW - playedWidth(played_.label()) - 8.0f;
        const auto& entry = palette_[static_cast<std::size_t>(chipEntry_[static_cast<std::size_t>(hover_)])];
        const std::string cn = chordName(entry.chord);
        p.textAt(ellipsize(cn, 11.0f, Weight::semibold, captionW), 11.0f, Weight::semibold, colour::text, kPadX,
                 kCaptionY + 11.0f);
        const float cw = std::min(captionW, textWidth(cn, 11.0f, Weight::semibold));
        const std::string numeral = context_.lens ? " · " + analyze(entry.chord, sec.key).numeral : std::string();
        p.textAt(ellipsize(numeral + " · " + ringWord(entry.ring), 11.0f, Weight::medium, captionW - cw), 11.0f,
                 Weight::medium, colour::textDim, kPadX + cw, kCaptionY + 11.0f);
        p.textAt(ellipsize("Keeps " + plural(entry.sharedBefore, "note") + " from the chord before, " +
                               std::to_string(entry.sharedAfter) + " after.",
                           11.0f, Weight::medium, captionW),
                 11.0f, Weight::medium, colour::textDim, kPadX, kCaptionY + 26.0f);
    } else {
        float x = kPadX;
        const float y = kCaptionY + 6.0f;
        p.rect(kChipEdge0, x, y, 9.0f, 9.0f, 3.0f);
        p.textAt("In key", 10.0f, Weight::medium, colour::textDim, x + 13.0f, y + 8.5f);
        x += 13.0f + textWidth("In key", 10.0f) + 10.0f;
        p.dashedRoundRect(kLegendDash, x + 0.5f, y + 0.5f, 8.0f, 8.0f, 2.5f, 1.0f, 2.0f, 1.5f);
        p.textAt("Borrowed", 10.0f, Weight::medium, colour::textDim, x + 13.0f, y + 8.5f);
        x += 13.0f + textWidth("Borrowed", 10.0f) + 10.0f;
        p.hgrad(film3(), x, y, 9.0f, 9.0f, 3.0f);
        p.textAt("Outside", 10.0f, Weight::medium, colour::textDim, x + 13.0f, y + 8.5f);
    }

    rowLabel(p, "Voicing", kChordRowsY);
    rowLabel(p, "Inversion", kChordRowsY + 34.0f);
    // stepper track and value
    const float sx = kRight - 74.0f;
    const float sy = kChordRowsY + 34.0f;
    p.rect(colour::raised, sx + 0.5f, sy + 0.5f, 73.0f, kRowH - 1.0f, 7.0f);
    p.border(colour::edge, sx, sy, 74.0f, kRowH, 7.0f);
    const int inv = span.chord.inversion;
    p.text((inv > 0 ? "+" : "") + std::to_string(inv), 11.5f, Weight::semibold, colour::text, sx + 26.0f, sy, 22.0f,
           kRowH, visage::Font::kCenter);
    rowLabel(p, "Colour", kChordRowsY + 68.0f);
    if (!hasLadder(span.chord.family))
        p.text("fixed for this chord", 11.5f, Weight::semibold, colour::textFaint, kPadX, kChordRowsY + 68.0f,
               kContentW, kRowH, visage::Font::kRight);
    rowLabel(p, "Voice-leading", kChordRowsY + 102.0f);
}

// ----- part

void Inspector::syncPart() {
    const Role role = context_.sel.role;
    const auto& part = context_.song().part(role);
    const unsigned accent = colour::role(role);
    const auto names = styleNames(role);

    styles_.resize(names.size());
    styleRects_.clear();
    float x = kPadX;
    float y = 57.0f;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const int style = static_cast<int>(i);
        auto& chip = styles_[i];
        chip.setup(Control::Look::chip, std::string(names[i]),
                   [this, role, style] { context_.edit([role, style](Document& d) { d.setStyle(role, style); }); });
        chip.setAccessibleName(std::string(roleName(role)) + " style " + std::string(names[i]));
        chip.setAccent(accent);
        chip.setAnnounce(Control::Announce::choice);
        chip.setOn(part.style == style);
        const float w = std::ceil(textWidth(names[i], 11.0f, Weight::semibold)) + 20.0f;
        if (x + w > kRight + 0.5f) {
            x = kPadX;
            y += 29.0f;
        }
        styleRects_.push_back({x, y, w});
        x += w + 5.0f;
    }
    stylesBottom_ = y + 24.0f;

    density_->setRole(role);
    density_->setName(std::string(roleName(role)) + " density");
    register_.set(
        {"Low", "Mid", "High"}, part.reg + 1,
        [this, role](int i) { context_.edit([role, i](Document& d) { d.setRegister(role, i - 1); }); }, "Register");
    const bool rate = rateApplies(role, part.style);
    if (rate)
        rate_.set(
            {"½×", "1×", "2×"},
            part.rate < 0.75f  ? 0
            : part.rate > 1.5f ? 2
                               : 1,
            [this, role](int i) {
                const float r = i == 0 ? 0.5f : i == 1 ? 1.0f : 2.0f;
                context_.edit([role, r](Document& d) { d.setRate(role, r); });
            },
            "Rate");
    amount_.set(
        {"Nudge", "Twist", "Rewrite"}, static_cast<int>(context_.mutation),
        [this](int i) {
            context_.mutation = static_cast<Mutation>(i);
            context_.changed();
        },
        "Mutate amount");
    const int pins = context_.doc().pinCount(context_.focus, role);
    clearPins_.setAccessibleName("Clear " + plural(pins, "pin"));
    mutate_.setAccessibleName("Mutate " + std::string(roleName(role)) + " (" +
                              std::string(mutationName(context_.mutation)) + ")");

    takePatterns_.clear();
    takes_.resize(4);
    for (int k = 1; k <= 4; ++k) {
        takePatterns_.push_back(context_.doc().previewTake(context_.focus, role, k));
        auto& take = takes_[static_cast<std::size_t>(k - 1)];
        take.setup(Control::Look::custom, "Take " + std::to_string(k),
                   [this, role, k] { context_.edit([role, k](Document& d) { d.applyTake(role, k); }); });
        take.setAccessibleName("Use take " + std::to_string(k) + " for " + std::string(roleName(role)));
        take.setPainter([this, k, accent, role](const Paint& p, const Control& c) {
            const float w = c.width() / p.s;
            const float h = c.height() / p.s;
            p.rect(colour::cell, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
            p.border(c.hovered() ? accent : colour::edge, 0.0f, 0.0f, w, h, 7.0f);
            if (static_cast<std::size_t>(k - 1) >= takePatterns_.size())
                return;
            const auto& pat = takePatterns_[static_cast<std::size_t>(k - 1)];
            int lo = 127;
            int hi = 0;
            for (const auto& n : pat.notes) {
                if (n.role != role)
                    continue;
                lo = std::min(lo, n.pitch);
                hi = std::max(hi, n.pitch + 1);
            }
            if (hi < lo)
                return;
            const float total = static_cast<float>(std::max(1, pat.totalSteps()));
            const float iw = w - 4.0f;
            const float ih = h - 4.0f;
            for (const auto& n : pat.notes) {
                if (n.role != role)
                    continue;
                const float nx = 2.0f + n.start / total * iw;
                const float ny =
                    2.0f + 4.0f + static_cast<float>(hi - n.pitch) / static_cast<float>(hi - lo + 1) * (ih - 8.0f);
                p.rect(accent, nx, ny, std::max(1.2f, n.dur / total * iw - 0.4f), 2.4f);
            }
        });
    }

    rate_.setVisible(rate);
    clearPins_.setVisible(role != Role::chords && pins > 0);
    mutate_.redraw();
}

void Inspector::drawPart(const Paint& p) {
    const Role role = context_.sel.role;
    const auto& part = context_.song().part(role);
    drawHeader(p, "PART · MIDI CHANNEL " + std::to_string(midiChannel(role)), std::string(roleName(role)), "",
               colour::role(role));
    float y = stylesBottom_ + 10.0f;
    rowLabel(p, "Density", y);
    y += kRowH + 10.0f;
    rowLabel(p, "Register", y);
    y += kRowH + 10.0f;
    rowLabel(p, "Rate", y);
    if (!rateApplies(role, part.style)) {
        const auto names = styleNames(role);
        const std::string style = part.style >= 0 && static_cast<std::size_t>(part.style) < names.size()
                                      ? std::string(names[static_cast<std::size_t>(part.style)])
                                      : std::string();
        p.text(role == Role::melody ? "follows the phrase" : "set by " + style, 11.5f, Weight::semibold,
               colour::textFaint, kPadX, y, kContentW, kRowH, visage::Font::kRight);
    }
    y += kRowH + 10.0f;
    rowLabel(p, "Mutate", y);
    y += kRowH + 10.0f;
    const int pins = context_.doc().pinCount(context_.focus, role);
    std::string note = role == Role::chords ? "Chord notes follow the harmony lane"
                       : pins > 0           ? std::to_string(pins) + " pinned here"
                                            : "Click a note to pin it";
    p.text(ellipsize(note, 11.0f, Weight::medium, kContentW - 110.0f), 11.0f, Weight::medium, colour::textFaint, kPadX,
           y, kContentW - 100.0f, kRowH);
    y += kRowH + 12.0f;
    p.spaced("OTHER TAKES", 9.5f, Weight::semibold, colour::textFaint, kPadX, y + 9.0f, 0.09f);
}

// ----- bridge

void Inspector::syncBridge() {
    const int bridge = context_.sel.item.index;
    const auto& song = context_.song();
    const auto& b = song.bridges[static_cast<std::size_t>(bridge)];
    routeList_ = context_.doc().routes(bridge);
    const Route current = bridgeRoute(song, bridge);
    why_ = wrapText(current.why, 11.0f, Weight::medium, kContentW);

    const std::string target = song.sections[static_cast<std::size_t>(b.to)].chords.empty()
                                   ? std::string()
                                   : chordName(song.sections[static_cast<std::size_t>(b.to)].chords.front());
    routes_.resize(routeList_.size());
    for (std::size_t i = 0; i < routeList_.size(); ++i) {
        const Route r = routeList_[i];
        auto& row = routes_[i];
        row.setup(Control::Look::custom, std::string(r.name),
                  [this, bridge, id = r.id] { context_.edit([bridge, id](Document& d) { d.setRoute(bridge, id); }); });
        row.setAccessibleName("Route " + std::string(r.name) + ", boldness " + std::to_string(r.boldness));
        row.setAnnounce(Control::Announce::choice);
        row.setOn(r.id == current.id);
        std::vector<std::string> chords;
        for (const auto& c : r.chords)
            chords.push_back(chordName(c));
        row.setPainter([chords, target, boldness = r.boldness](const Paint& p, const Control& c) {
            const float w = c.width() / p.s;
            const float h = c.height() / p.s;
            p.rect(c.on() ? kRouteOn : colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
            p.border(c.on() ? colour::gold() : (c.hovered() ? kRouteHover : colour::edge), 0.0f, 0.0f, w, h, 7.0f);
            p.text(ellipsize(c.label(), 11.5f, Weight::semibold, 94.0f), 11.5f, Weight::semibold, colour::text, 8.0f,
                   0.0f, 96.0f, h);
            // boldness: five dots
            const float dotsRight = w - 8.0f;
            for (int d = 0; d < 5; ++d)
                p.circle(colour::withAlpha(colour::gold(), d < boldness ? 1.0f : 0.25f),
                         dotsRight - 2.6f - static_cast<float>(4 - d) * 6.2f, h * 0.5f, 2.4f);
            // chords: names bright, joins dim, cut to fit
            float x = 112.0f;
            const float limit = dotsRight - 5.0f * 6.2f - 8.0f;
            const float base = h * 0.5f + 3.8f;
            const auto piece = [&](const std::string& t, bool bright) {
                if (x >= limit)
                    return;
                const Weight wt = bright ? Weight::semibold : Weight::medium;
                const float tw = textWidth(t, 10.5f, wt);
                if (x + tw > limit) {
                    p.textAt(ellipsize(t, 10.5f, wt, limit - x), 10.5f, wt, bright ? colour::text : colour::textDim, x,
                             base);
                    x = limit;
                    return;
                }
                p.textAt(t, 10.5f, wt, bright ? colour::text : colour::textDim, x, base);
                x += tw;
            };
            for (std::size_t k = 0; k < chords.size(); ++k) {
                if (k > 0)
                    piece(" · ", false);
                piece(chords[k], true);
            }
            if (!target.empty()) {
                piece(" " + std::string(kArrow) + " ", false);
                piece(target, false);
            }
        });
    }
    length_.set(
        {"1 bar", "2 bars", "4 bars"},
        b.bars == 1   ? 0
        : b.bars == 2 ? 1
        : b.bars == 4 ? 2
                      : -1, // a length from saved state that the buttons do not offer lights none
        [this, bridge](int i) {
            const int bars = i == 0 ? 1 : i == 1 ? 2 : 4;
            context_.edit([bridge, bars](Document& d) { d.setBridgeBars(bridge, bars); });
        },
        "Bridge length");
}

void Inspector::drawBridge(const Paint& p) {
    const int bridge = context_.sel.item.index;
    const auto& song = context_.song();
    const auto& b = song.bridges[static_cast<std::size_t>(bridge)];
    const Key from = song.sections[static_cast<std::size_t>(b.from)].key;
    const Key to = song.sections[static_cast<std::size_t>(b.to)].key;
    drawHeader(p,
               "BRIDGE · " + upper(song.sections[static_cast<std::size_t>(b.from)].name) + " " + std::string(kArrow) +
                   " " + upper(song.sections[static_cast<std::size_t>(b.to)].name),
               std::string(bridgeRoute(song, bridge).name),
               keyName(from) + " " + std::string(kArrow) + " " + keyName(to));

    // morph bar: key A to key B, pivots marked
    const float my = 57.0f;
    p.hgrad(colour::oklch(0.68f, 0.15f, keyHue(from)), colour::oklch(0.8f, 0.14f, keyHue(to)), kPadX, my, kContentW,
            26.0f, 7.0f);
    auto& pattern = context_.doc().pattern(ItemRef::bridge(bridge));
    const float total = static_cast<float>(std::max(1, pattern.totalSteps()));
    for (const auto& span : pattern.spans) {
        const float x = kPadX + span.start / total * kContentW;
        const float w = span.dur / total * kContentW;
        if (span.start > 0.0f)
            p.rect(colour::withAlpha(colour::ink, 0.35f), x - 0.5f, my, 1.0f, 26.0f);
        if (span.pivot) {
            p.border(colour::gold(), x + 0.75f, my + 0.75f, w - 1.5f, 24.5f, 6.0f, 1.5f);
            p.text("PIVOT", 10.0f, Weight::semibold, colour::ink, x, my, w, 26.0f, visage::Font::kCenter);
        } else if (w > 40.0f) {
            p.text(ellipsize(chordName(span.chord), 10.0f, Weight::semibold, w - 6.0f), 10.0f, Weight::semibold,
                   colour::withAlpha(colour::ink, 0.75f), x, my, w, 26.0f, visage::Font::kCenter);
        }
    }

    float y = my + 26.0f + 8.0f;
    for (const auto& line : why_) {
        p.textAt(line, 11.0f, Weight::medium, colour::textDim, kPadX, y + 11.0f);
        y += 14.5f;
    }
    // Length row label sits under the routes
    const float rowsY = y + 8.0f;
    const float lengthY = rowsY + static_cast<float>(routes_.size()) * 34.0f + 4.0f;
    rowLabel(p, "Length", lengthY);
}

// ----- section

void Inspector::syncSection() {
    const int section = context_.sel.item.index;
    const auto& sec = context_.song().sections[static_cast<std::size_t>(section)];
    const float hue = keyHue(sec.key);
    keys_.resize(12);
    for (int pc = 0; pc < 12; ++pc) {
        auto& key = keys_[static_cast<std::size_t>(pc)];
        key.setup(Control::Look::custom, pcName(pc), [this, section, pc, mode = sec.key.mode] {
            context_.edit([section, pc, mode](Document& d) { d.setSectionKey(section, Key{pc, mode}); });
        });
        key.setAccessibleName("Key " + pcName(pc));
        key.setAnnounce(Control::Announce::choice);
        key.setOn(sec.key.tonic == pc);
        key.setPainter([hue](const Paint& p, const Control& c) {
            const float w = c.width() / p.s;
            const float h = c.height() / p.s;
            const unsigned accent = colour::oklch(0.72f, 0.15f, hue);
            p.rect(c.on() ? colour::mix(colour::raised, accent, 0.3f) : colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f,
                   6.0f);
            p.border(c.on() ? accent : (c.hovered() ? colour::hoverEdge : colour::edge), 0.0f, 0.0f, w, h, 6.0f);
            p.text(c.label(), 11.0f, Weight::semibold, c.on() || c.hovered() ? colour::text : colour::textDim, 0.0f,
                   0.0f, w, h, visage::Font::kCenter);
        });
    }
    keyMode_.set(
        {"Major", "Minor"}, sec.key.mode == Mode::major ? 0 : 1,
        [this, section, tonic = sec.key.tonic](int i) {
            const Mode mode = i == 0 ? Mode::major : Mode::minor;
            context_.edit([section, tonic, mode](Document& d) { d.setSectionKey(section, Key{tonic, mode}); });
        },
        "Mode");
    bars_.set(
        {"2", "4", "8"},
        sec.bars == 2   ? 0
        : sec.bars == 4 ? 1
        : sec.bars == 8 ? 2
                        : -1, // 1, 16 and other lengths from saved state light none
        [this, section](int i) {
            const int bars = i == 0 ? 2 : i == 1 ? 4 : 8;
            context_.edit([section, bars](Document& d) { d.setSectionBars(section, bars); });
        },
        "Section length", 26.0f);
    library_->sync(section);
}

void Inspector::drawSection(const Paint& p) {
    const int section = context_.sel.item.index;
    const auto& sec = context_.song().sections[static_cast<std::size_t>(section)];
    drawHeader(p, "SECTION " + sectionLetter(section), sec.name,
               keyName(sec.key) + " · " + std::to_string(sec.bars) + " bars");
    rowLabel(p, "Mode", 117.0f);
    rowLabel(p, "Length", 151.0f);
    p.spaced(upper("Progressions in " + keyName(sec.key)), 9.5f, Weight::semibold, colour::textFaint, kPadX, 197.0f,
             0.09f);
}

// ---------------------------------------------------------------------------------------------------------------

void Inspector::layoutChildren() {
    const auto& c = context_;
    switch (view_) {
    case View::chord: {
        for (std::size_t i = 0; i < chips_.size() && i < chipSlot_.size(); ++i) {
            const auto [ring, j] = chipSlot_[i];
            const float cx = kRingCX + slotX(ring, j) * kRingK;
            const float cy = kRingCY + slotY(ring, j) * kRingK;
            place(chips_[i], c, cx - kChipW * 0.5f, cy - kChipH * 0.5f, kChipW, kChipH);
        }
        placeRight(voicing_, c, kChordRowsY);
        const float sx = kRight - 74.0f;
        place(inversionDown_, c, sx + 2.0f, kChordRowsY + 34.0f + 3.0f, 22.0f, 20.0f);
        place(inversionUp_, c, sx + 50.0f, kChordRowsY + 34.0f + 3.0f, 22.0f, 20.0f);
        placeRight(level_, c, kChordRowsY + 68.0f);
        const float vw = std::ceil(textWidth("Off", 11.5f, Weight::semibold)) + 24.0f;
        place(voiceLead_, c, kRight - vw, kChordRowsY + 102.0f, vw, kRowH);
        if (!playedName_.empty()) {
            const float pw = playedWidth(played_.label());
            place(played_, c, kRight - pw, kCaptionY + 3.0f, pw, 20.0f);
        }
        break;
    }
    case View::part: {
        for (std::size_t i = 0; i < styles_.size() && i < styleRects_.size(); ++i)
            place(styles_[i], c, styleRects_[i][0], styleRects_[i][1], styleRects_[i][2], 24.0f);
        float y = stylesBottom_ + 10.0f;
        place(*density_, c, kRight - 156.0f, y, 156.0f, kRowH);
        y += kRowH + 10.0f;
        placeRight(register_, c, y);
        y += kRowH + 10.0f;
        placeRight(rate_, c, y);
        y += kRowH + 10.0f;
        placeRight(amount_, c, y);
        y += kRowH + 10.0f;
        const float mw = 30.0f + textWidth("Mutate", 11.5f, Weight::semibold) + 12.0f;
        place(mutate_, c, kRight - mw, y, mw, kRowH);
        const int pins = context_.doc().pinCount(context_.focus, context_.sel.role);
        const float pinTextW = textWidth(std::to_string(pins) + " pinned here", 11.0f, Weight::medium);
        place(clearPins_, c, kPadX + pinTextW + 6.0f, y + 3.0f, 44.0f, 20.0f);
        y += kRowH + 12.0f + 14.0f;
        const float tw = (kContentW - 18.0f) / 4.0f;
        for (std::size_t i = 0; i < takes_.size(); ++i)
            place(takes_[i], c, kPadX + static_cast<float>(i) * (tw + 6.0f), y, tw, 44.0f);
        break;
    }
    case View::bridge: {
        float y = 57.0f + 26.0f + 8.0f + static_cast<float>(why_.size()) * 14.5f + 8.0f;
        for (std::size_t i = 0; i < routes_.size(); ++i) {
            place(routes_[i], c, kPadX, y, kContentW, 30.0f);
            y += 34.0f;
        }
        placeRight(length_, c, y + 4.0f);
        break;
    }
    case View::section: {
        const float kw = (kContentW - 5.0f * 4.0f) / 6.0f;
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            const float col = static_cast<float>(i % 6);
            const float row = static_cast<float>(i / 6);
            place(keys_[i], c, kPadX + col * (kw + 4.0f), 57.0f + row * 28.0f, kw, 24.0f);
        }
        placeRight(keyMode_, c, 117.0f);
        placeRight(bars_, c, 151.0f);
        place(*library_, c, kPadX, 205.0f, kContentW + 6.0f, designHeight() - 205.0f - 10.0f);
        break;
    }
    }
}

void Inspector::drawHeader(const Paint& p, const std::string& eyebrow, const std::string& title,
                           const std::string& note, unsigned swatch, float reserveRight) const {
    p.spaced(ellipsize(eyebrow, 9.5f, Weight::semibold, kContentW - 30.0f), 9.5f, Weight::semibold, colour::textFaint,
             kPadX, kTop + 9.0f, 0.09f);
    float x = kPadX;
    if (swatch != 0) {
        p.rect(swatch, x, 33.0f, 10.0f, 10.0f, 3.0f);
        x += 18.0f;
    }
    const float tw = textWidth(title, 18.0f, Weight::semibold);
    p.textAt(title, 18.0f, Weight::semibold, colour::text, x, 44.5f);
    if (!note.empty())
        p.textAt(ellipsize(note, 12.0f, Weight::semibold, kRight - (x + tw + 8.0f) - reserveRight), 12.0f,
                 Weight::semibold, colour::textDim, x + tw + 8.0f, 44.5f);
}

void Inspector::rowLabel(const Paint& p, const std::string& label, float y) const {
    p.text(label, 11.5f, Weight::semibold, colour::textDim, kPadX, y, 120.0f, kRowH);
}

void Inspector::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    chrome(p);
    switch (view_) {
    case View::chord:
        drawChord(p);
        break;
    case View::part:
        drawPart(p);
        break;
    case View::bridge:
        drawBridge(p);
        break;
    case View::section:
        drawSection(p);
        break;
    }
}

} // namespace lg::ui
