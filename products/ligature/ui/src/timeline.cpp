// SPDX-License-Identifier: AGPL-3.0-or-later
#include "timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lg::ui {

namespace {
constexpr float kTop = 8.0f;
constexpr float kHeadX = 8.0f;
constexpr float kHeadW = 128.0f;
constexpr float kGridX = 144.0f;
constexpr float kGridW = 760.0f;
constexpr float kRowGap = 5.0f;
constexpr float kRulerH = 20.0f;
constexpr float kHarmonyH = 94.0f;
constexpr float kStutterH = 28.0f;

constexpr std::array<Role, 4> kLaneRoles{Role::melody, Role::chords, Role::arp, Role::bass};
constexpr std::array<float, 4> kLaneH{76.0f, 100.0f, 56.0f, 64.0f};

constexpr float rowY(int row) {
    // 0 ruler, 1 harmony, 2..5 lanes, 6 stutter
    float y = kTop;
    const float heights[] = {kRulerH, kHarmonyH, kLaneH[0], kLaneH[1], kLaneH[2], kLaneH[3], kStutterH};
    for (int i = 0; i < row; ++i)
        y += heights[i] + kRowGap;
    return y;
}

constexpr unsigned kHeaderHover = 0xff141a22;
constexpr unsigned kHeaderSelected = 0xff18202a;
constexpr unsigned kSmallButton = 0xff1a222c;
constexpr unsigned kSmallButtonHover = 0xff24303c;
constexpr unsigned kDotBar = 0xff3b4a5a;
constexpr unsigned kDotBeat = 0xff26313e;

float shortestHue(float from, float to, float t) {
    const float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return from + delta * t;
}

visage::Gradient filmGradient() {
    return visage::Gradient(colour::oklch(0.8f, 0.13f, 200.0f), colour::oklch(0.8f, 0.13f, 300.0f),
                            colour::oklch(0.85f, 0.12f, 60.0f), colour::oklch(0.8f, 0.13f, 150.0f));
}

std::string styleText(const Song& song, Role role) {
    const auto& part = song.part(role);
    const auto names = styleNames(role);
    std::string text = part.style >= 0 && static_cast<std::size_t>(part.style) < names.size()
                           ? std::string(names[static_cast<std::size_t>(part.style)])
                           : std::string();
    if (part.rate != 1.0f && rateApplies(role, part.style))
        text += part.rate > 1.0f ? " · 2×" : " · ½×";
    return text;
}

int voiceMotion(const Pattern& pattern) {
    int motion = 0;
    const auto& spans = pattern.spans;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto& a = spans[i].voicing;
        const auto& b = spans[(i + 1) % spans.size()].voicing;
        if (a.size() == b.size()) {
            for (std::size_t k = 0; k < a.size(); ++k)
                motion += std::abs(a[k] - b[k]);
        } else {
            motion += 4;
        }
    }
    return motion;
}
} // namespace

RollRange rollRange(const Pattern& pattern, Role role, float height) {
    RollRange r;
    bool any = false;
    int lo = 127;
    int hi = 0;
    for (const auto& n : pattern.notes) {
        if (n.role != role)
            continue;
        any = true;
        lo = std::min(lo, n.pitch);
        hi = std::max(hi, n.pitch);
    }
    if (any) {
        r.lo = lo - 1;
        r.hi = hi + 1;
    }
    const int minSpan = role == Role::bass ? 16 : 14;
    if (r.hi - r.lo < minSpan) {
        const float c = static_cast<float>(r.hi + r.lo) * 0.5f;
        r.lo = static_cast<int>(std::floor(c - static_cast<float>(minSpan) * 0.5f));
        r.hi = r.lo + minSpan;
    }
    const float rows = static_cast<float>(r.hi - r.lo + 1);
    r.rowH = std::min(7.0f, (height - 8.0f) / rows);
    r.top = (height - r.rowH * rows) * 0.5f;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// Harmony lane: key band, tension curve, and one control per chord.

class Timeline::HarmonyView final : public visage::Frame {
  public:
    explicit HarmonyView(Context& context) : context_(context), blocks_(context, *this) {}

    void sync() {
        auto& pattern = context_.doc().pattern(context_.focus);
        const std::size_t n = pattern.spans.size();
        blocks_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const int index = static_cast<int>(i);
            auto& block = blocks_[i];
            block.setup(Control::Look::custom, chordName(pattern.spans[i].chord), {});
            block.setAccessibleName("Chord " + chordName(pattern.spans[i].chord) + ", " +
                                    pattern.spans[i].analysis.numeral);
            block.onClick = [this, index](const visage::MouseEvent*) { choose(index); };
            block.onWheel = [this, index](const visage::MouseEvent& e) { return wheel(index, e); };
            block.setPainter([this, index](const Paint& p, const Control& c) { paintBlock(p, c, index); });
        }
        layoutBlocks();
        redrawAll();
    }

    // A chord click: selects it (a bridge's chords open the bridge) and auditions it while stopped.
    void choose(int span) {
        auto& c = context_;
        if (c.focus.isBridge()) {
            c.sel = {SelKind::item, c.focus, 0, c.sel.role};
            c.changed();
            return;
        }
        c.selectChord(c.focus, span);
        c.follow = false;
        if (!c.model.telemetry().playing)
            c.model.auditionChord(c.focus, span);
    }

    void resized() override { layoutBlocks(); }

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / p.s;
        const float h = height() / p.s;
        p.rect(colour::cell, 0.0f, 0.0f, w, h, 8.0f);
        auto& pattern = context_.doc().pattern(context_.focus);
        const float ha = keyHue(pattern.key);
        const float hb = pattern.bridge ? keyHue(pattern.toKey) : ha;
        p.clipped(0.0f, 0.0f, w, 4.0f, [&] {
            p.hgrad(colour::oklch(0.72f, 0.15f, ha), colour::oklch(0.78f, 0.14f, hb), 0.0f, 0.0f, w, h, 8.0f);
        });
        if (!context_.lens || pattern.spans.empty())
            return;
        const float stepW = w / static_cast<float>(std::max(1, pattern.totalSteps()));
        std::vector<visage::Point> points;
        for (const auto& span : pattern.spans) {
            const float x = span.start * stepW + span.dur * stepW * 0.5f;
            points.push_back({x, 28.0f - std::clamp(span.tension, 0.0f, 1.0f) * 20.0f});
        }
        visage::Path curve;
        curve.moveTo(0.0f, points[0].y);
        curve.lineTo(points[0].x, points[0].y);
        for (std::size_t i = 1; i < points.size(); ++i) {
            const float mx = (points[i - 1].x + points[i].x) * 0.5f;
            curve.bezierTo(mx, points[i - 1].y, mx, points[i].y, points[i].x, points[i].y);
        }
        curve.lineTo(w, points.back().y);
        p.stroke(curve, colour::withAlpha(colour::focusRing, 0.55f), 1.3f);
        for (const auto& pt : points)
            p.circle(colour::focusRing, pt.x, pt.y, 2.6f);
        const float lw = spacedWidth("TENSION", 8.5f, Weight::semibold, 0.1f);
        p.spaced("TENSION", 8.5f, Weight::semibold, colour::textFaint, w - 4.0f - lw, 14.0f, 0.1f);
    }

  private:
    void layoutBlocks() {
        auto& pattern = context_.doc().pattern(context_.focus);
        const float w = width() / context_.scale;
        const float h = height() / context_.scale;
        const float stepW = w / static_cast<float>(std::max(1, pattern.totalSteps()));
        for (std::size_t i = 0; i < blocks_.size() && i < pattern.spans.size(); ++i) {
            const auto& span = pattern.spans[i];
            const float x = span.start * stepW;
            const float bw = span.dur * stepW;
            place(blocks_[i], context_, x + 2.0f, 32.0f, std::max(4.0f, bw - 4.0f), h - 36.0f);
        }
    }

    bool wheel(int span, const visage::MouseEvent& e) {
        auto& c = context_;
        if (c.focus.isBridge() || c.sel.kind != SelKind::chord || c.sel.item != c.focus || c.sel.chord != span ||
            e.wheel_delta_y == 0.0f)
            return false;
        wheelAccumulator_ += e.wheel_delta_y;
        if (std::abs(wheelAccumulator_) < 1.0f)
            return true;
        const int direction = wheelAccumulator_ > 0.0f ? 1 : -1;
        wheelAccumulator_ = 0.0f;
        const int section = c.focus.index;
        c.edit([section, span, direction](Document& d) { d.stepInversion(section, span, direction); });
        return true;
    }

    void paintBlock(const Paint& p, const Control& c, int index) const {
        auto& pattern = context_.doc().pattern(context_.focus);
        if (index >= static_cast<int>(pattern.spans.size()))
            return;
        const auto& span = pattern.spans[static_cast<std::size_t>(index)];
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        const float ha = keyHue(pattern.key);
        const float frac = (span.start + span.dur * 0.5f) / static_cast<float>(std::max(1, pattern.totalSteps()));
        const float hue = pattern.bridge ? shortestHue(ha, keyHue(pattern.toKey), frac) : ha;
        const auto& sel = context_.sel;
        const bool selected = sel.kind == SelKind::chord && sel.item == context_.focus && sel.chord == index;

        p.rect(colour::mix(colour::cell, colour::oklch(0.6f, 0.12f, hue),
                           selected ? 0.34f : (c.hovered() ? 0.24f : 0.17f)),
               0.0f, 0.0f, w, h, 7.0f);
        if (selected)
            p.border(colour::text, 0.0f, 0.0f, w, h, 7.0f, 1.5f);
        else
            p.border(colour::oklch(0.72f, 0.13f, hue, 0.45f), 0.0f, 0.0f, w, h, 7.0f, 1.0f);
        if (!span.analysis.diatonic && w > 18.0f)
            p.hgrad(filmGradient(), 7.0f, 0.0f, w - 14.0f, 2.0f, 1.0f);

        const float full = w + 4.0f;
        const float size = full < 70.0f ? 11.0f : full < 110.0f ? 13.0f : 15.0f;
        p.textAt(ellipsize(chordName(span.chord), size, Weight::semibold, w - 12.0f), size, Weight::semibold,
                 colour::text, 8.0f, 23.0f);
        if (!context_.lens)
            return;
        p.textAt(ellipsize(numeralText(pattern, span), 10.5f, Weight::medium, w - 12.0f), 10.5f, Weight::medium,
                 colour::textDim, 8.0f, 40.0f);
        if (full > 64.0f) {
            const std::string word = span.pivot ? "PIVOT" : std::string(functionWord(span.analysis.function));
            p.spaced(word, 8.5f, Weight::semibold, span.pivot ? colour::gold() : colour::textFaint, 8.0f, h - 6.0f,
                     0.1f);
        }
    }

    Context& context_;
    ControlPool blocks_;
    float wheelAccumulator_ = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------
// A part's roll: notes coloured by role, velocity as opacity, pins outlined; the chords lane draws voice-leading.

class Timeline::LaneView final : public visage::Frame {
  public:
    LaneView(Context& context, Role role, HarmonyView& harmony) : context_(context), role_(role), harmony_(harmony) {}

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / p.s;
        const float h = height() / p.s;
        p.rect(colour::cell, 0.0f, 0.0f, w, h, 8.0f);
        auto& pattern = context_.doc().pattern(context_.focus);
        const int total = std::max(1, pattern.totalSteps());
        const float stepW = w / static_cast<float>(total);
        for (int st = 4; st < total; st += 4)
            p.rect(st % kStepsPerBar == 0 ? colour::gridBar : colour::gridBeat, static_cast<float>(st) * stepW - 0.5f,
                   0.0f, 1.0f, h);
        const RollRange range = rollRange(pattern, role_, h);
        const float half = range.rowH * 0.5f;

        if (role_ == Role::chords && pattern.spans.size() > 1) {
            const auto& spans = pattern.spans;
            for (std::size_t i = 0; i < spans.size(); ++i) {
                const auto& a = spans[i];
                const auto& b = spans[(i + 1) % spans.size()];
                if (a.voicing.size() != b.voicing.size())
                    continue;
                const bool wrap = i + 1 == spans.size();
                const float x1 = (a.start + a.dur) * stepW - 6.0f;
                const float x2 = wrap ? w : b.start * stepW + 6.0f;
                const float hx = a.start * stepW;
                for (std::size_t k = 0; k < a.voicing.size(); ++k) {
                    const bool same = a.voicing[k] == b.voicing[k];
                    const float y1 = range.y(a.voicing[k]) + half;
                    const float y2 = range.y(b.voicing[k]) + half;
                    visage::Path path;
                    path.moveTo(hx, y1);
                    path.lineTo(x1, y1);
                    path.bezierTo(x1 + 10.0f, y1, x2 - 10.0f, y2, x2, y2);
                    std::vector<float> dash;
                    if (wrap)
                        dash = {3.0f, 3.0f};
                    p.stroke(path,
                             same ? colour::withAlpha(colour::gold(), 0.75f) : colour::withAlpha(colour::quiet, 0.7f),
                             same ? 1.6f : 1.0f, dash);
                }
            }
        }

        const unsigned ink = colour::role(role_);
        const float noteH = std::max(2.0f, range.rowH - 1.0f);
        for (std::size_t i = 0; i < pattern.notes.size(); ++i) {
            const auto& n = pattern.notes[i];
            if (n.role != role_)
                continue;
            const float x = n.start * stepW;
            const float nw = std::max(2.0f, n.dur * stepW - 1.0f);
            const float y = range.y(n.pitch);
            p.rect(colour::withAlpha(ink, 0.45f + std::clamp(n.velocity, 0.0f, 1.0f) * 0.55f), x, y, nw, noteH, 1.5f);
            if ((n.flags & kNotePinned) != 0)
                p.border(colour::white, x - 0.6f, y - 0.6f, nw + 1.2f, noteH + 1.2f, 2.0f, 1.2f);
            else if (static_cast<int>(i) == hover_)
                p.border(colour::withAlpha(colour::white, 0.55f), x - 0.6f, y - 0.6f, nw + 1.2f, noteH + 1.2f, 2.0f,
                         1.0f);
        }
    }

    // The pattern changed: the hovered note index no longer means the same note.
    void clearHover() { hover_ = -1; }

    void mouseMove(const visage::MouseEvent& e) override {
        const int note = noteAt(e.position);
        if (note != hover_) {
            hover_ = note;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        if (hover_ >= 0) {
            hover_ = -1;
            redraw();
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        const int index = noteAt(e.position);
        if (index < 0)
            return;
        auto& c = context_;
        const Note note = c.doc().pattern(c.focus).notes[static_cast<std::size_t>(index)];
        if (role_ == Role::chords) {
            if (note.span >= 0)
                harmony_.choose(note.span);
            return;
        }
        const ItemRef item = c.focus;
        c.edit([item, note](Document& d) { d.togglePin(item, note); });
        c.selectPart(role_);
    }

  private:
    int noteAt(visage::Point position) const {
        const float s = context_.scale;
        const float mx = position.x / s;
        const float my = position.y / s;
        const float w = width() / s;
        const float h = height() / s;
        auto& pattern = context_.doc().pattern(context_.focus);
        const float stepW = w / static_cast<float>(std::max(1, pattern.totalSteps()));
        const RollRange range = rollRange(pattern, role_, h);
        const float noteH = std::max(2.0f, range.rowH - 1.0f);
        int best = -1;
        float bestDistance = 1.0e9f;
        for (std::size_t i = 0; i < pattern.notes.size(); ++i) {
            const auto& n = pattern.notes[i];
            if (n.role != role_)
                continue;
            const float x = n.start * stepW;
            const float nw = std::max(2.0f, n.dur * stepW - 1.0f);
            const float y = range.y(n.pitch);
            constexpr float slop = 2.5f;
            if (mx < x - slop || mx > x + nw + slop || my < y - slop || my > y + noteH + slop)
                continue;
            const float dy = std::abs(my - (y + noteH * 0.5f));
            const float dx = mx < x ? x - mx : (mx > x + nw ? mx - x - nw : 0.0f);
            const float distance = dx + dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    Context& context_;
    Role role_;
    HarmonyView& harmony_;
    int hover_ = -1;
};

// ---------------------------------------------------------------------------------------------------------------
// Stutter: one cell per 16th. Click cycles none -> 1/32 -> 1/64 -> gate. Route-owned cells draw dimmer.

class Timeline::StutterView final : public visage::Frame {
  public:
    explicit StutterView(Context& context) : context_(context) {}

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / p.s;
        const float h = height() / p.s;
        p.rect(colour::cell, 0.0f, 0.0f, w, h, 8.0f);
        auto& pattern = context_.doc().pattern(context_.focus);
        const auto& user = itemStutter(context_.song(), context_.focus);
        const int total = std::max(1, pattern.totalSteps());
        const float stepW = w / static_cast<float>(total);
        const unsigned ink = colour::stutter();
        for (int st = 0; st < total; ++st) {
            const float x = static_cast<float>(st) * stepW;
            if (st == hover_)
                p.rect(colour::withAlpha(colour::white, 0.05f), x, 2.0f, stepW, h - 4.0f, 2.0f);
            Stutter cell = Stutter::none;
            bool own = false;
            if (auto it = user.find(st); it != user.end() && it->second != Stutter::none) {
                cell = it->second;
                own = true;
            } else if (auto jt = pattern.cells.find(st); jt != pattern.cells.end()) {
                cell = jt->second;
            }
            const float cx = x + stepW * 0.5f;
            const float inset = std::min(2.0f, stepW * 0.2f);
            switch (cell) {
            case Stutter::none:
                p.circle(st % kStepsPerBar == 0 ? kDotBar : kDotBeat, cx, h * 0.5f, st % 4 == 0 ? 1.8f : 1.0f);
                break;
            case Stutter::gate:
                p.border(colour::withAlpha(ink, own ? 1.0f : 0.55f), x + inset, 7.0f,
                         std::max(1.0f, stepW - 2.0f * inset), h - 14.0f, 2.0f, 1.2f);
                break;
            case Stutter::r32:
            case Stutter::r64: {
                // Narrow cells (16-bar sections) draw fewer, thinner strokes rather than negative widths.
                const int k = cell == Stutter::r32 ? 2 : 4;
                const float sub = std::max(0.0f, stepW - 2.0f * inset) / static_cast<float>(k);
                for (int j = 0; j < k; ++j)
                    p.rect(colour::withAlpha(ink, own ? 1.0f : 0.55f), x + inset + static_cast<float>(j) * sub, 6.0f,
                           std::max(0.6f, sub - std::min(1.2f, sub * 0.4f)), h - 12.0f, 1.0f);
                break;
            }
            }
        }
    }

    void mouseMove(const visage::MouseEvent& e) override {
        const int st = stepAt(e.position.x);
        if (st != hover_) {
            hover_ = st;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        hover_ = -1;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        const int st = stepAt(e.position.x);
        if (st < 0)
            return;
        const ItemRef item = context_.focus;
        context_.edit([item, st](Document& d) { d.cycleStutter(item, st); });
    }

  private:
    int stepAt(float x) const {
        auto& pattern = context_.doc().pattern(context_.focus);
        const int total = std::max(1, pattern.totalSteps());
        const int st = static_cast<int>(std::floor(x / std::max(1.0f, width()) * static_cast<float>(total)));
        return st >= 0 && st < total ? st : -1;
    }

    Context& context_;
    int hover_ = -1;
};

// ---------------------------------------------------------------------------------------------------------------
// The playhead, above everything, when the focused item is the one playing.

class Timeline::Playhead final : public visage::Frame {
  public:
    explicit Playhead(Context& context) : context_(context) { setIgnoresMouseEvents(true, false); }

    // -1 hides it
    int position() const {
        const auto& t = context_.model.telemetry();
        if (!t.playing || t.performance.item != context_.focus)
            return -1;
        const float total = static_cast<float>(std::max(1, t.playingBars) * kStepsPerBar);
        return static_cast<int>(std::lround(std::clamp(t.stepInItem / total, 0.0f, 1.0f) * 4000.0f));
    }

    void draw(visage::Canvas& canvas) override {
        const int pos = position();
        if (pos < 0)
            return;
        const Paint p{canvas, context_.scale};
        const float h = height() / p.s;
        const float x = 6.0f + static_cast<float>(pos) / 4000.0f * kGridW;
        p.rect(colour::withAlpha(colour::white, 0.05f), x - 5.0f, 0.0f, 11.5f, h, 5.0f);
        p.rect(colour::withAlpha(colour::white, 0.09f), x - 2.0f, 0.0f, 5.5f, h, 2.5f);
        p.rect(0xfff2f5f8u, x, 0.0f, 1.5f, h);
    }

  private:
    Context& context_;
};

// ---------------------------------------------------------------------------------------------------------------

Timeline::Timeline(Context& context) : Panel(context) {
    harmony_ = std::make_unique<HarmonyView>(context);
    addChild(harmony_.get());
    for (std::size_t i = 0; i < kLaneRoles.size(); ++i) {
        lanes_[i] = std::make_unique<LaneView>(context, kLaneRoles[i], *harmony_);
        addChild(lanes_[i].get());
    }
    stutter_ = std::make_unique<StutterView>(context);
    addChild(stutter_.get());

    for (std::size_t i = 0; i < kLaneRoles.size(); ++i) {
        const Role role = kLaneRoles[i];
        auto& h = headers_[i];
        h.select = std::make_unique<Control>(context);
        h.mutate = std::make_unique<Control>(context);
        h.mute = std::make_unique<Control>(context);
        h.solo = std::make_unique<Control>(context);
        addChild(h.select.get());
        for (auto* b : {h.mutate.get(), h.mute.get(), h.solo.get()}) {
            addChild(b);
            b->setOnTop(true);
        }
        const std::string title(roleName(role));
        h.select->setup(Control::Look::custom, title, [this, role] { context_.selectPart(role); });
        h.select->setAccessibleName(title + " part, MIDI channel " + std::to_string(midiChannel(role)));
        // Drag a lane header to a track: that part of the focused item as MIDI.
        h.select->onDragOut = [this, role] { context_.model.dragMidi(DragScope::item, context_.focus, role); };
        h.select->setPainter([this, role, title](const Paint& p, const Control& c) {
            const float w = c.width() / p.s;
            const float hh = c.height() / p.s;
            const bool selected = context_.sel.kind == SelKind::part && context_.sel.role == role;
            if (selected)
                p.rect(kHeaderSelected, 0.0f, 0.0f, w, hh, 8.0f);
            else if (c.hovered())
                p.rect(kHeaderHover, 0.0f, 0.0f, w, hh, 8.0f);
            p.rect(colour::role(role), 0.0f, 6.0f, 2.0f, hh - 12.0f, 1.0f);
            const float top = (hh - 53.0f) * 0.5f;
            p.textAt(title, 12.0f, Weight::semibold, colour::text, 9.0f, top + 12.0f);
            const float tw = textWidth(title, 12.0f, Weight::semibold);
            p.textAt("CH " + std::to_string(midiChannel(role)), 9.5f, Weight::semibold, colour::textFaint,
                     9.0f + tw + 6.0f, top + 12.0f);
            p.textAt(ellipsize(styleText(context_.song(), role), 10.5f, Weight::medium, w - 16.0f), 10.5f,
                     Weight::medium, colour::textDim, 9.0f, top + 28.0f);
        });

        const auto smallPainter = [role](bool dice) {
            return [role, dice](const Paint& p, const Control& c) {
                const float w = c.width() / p.s;
                const float hh = c.height() / p.s;
                const bool on = c.on();
                p.rect(on ? colour::role(role) : (c.hovered() ? kSmallButtonHover : kSmallButton), 0.0f, 0.0f, w, hh,
                       4.0f);
                const unsigned ink = on ? colour::ink : (c.hovered() ? colour::text : colour::textDim);
                if (dice)
                    p.diceIcon(ink, w * 0.5f, hh * 0.5f, 11.0f);
                else
                    p.text(c.label(), 9.5f, Weight::semibold, ink, 0.0f, 0.0f, w, hh, visage::Font::kCenter);
            };
        };
        h.mutate->setup(Control::Look::custom, "Mutate", [this, role] {
            const Mutation m = context_.mutation;
            context_.edit([role, m](Document& d) { d.mutate(role, m); });
        });
        h.mutate->setPainter(smallPainter(true));
        h.mutate->setAccessibleName("Mutate " + title);
        h.mute->setup(Control::Look::custom, "M",
                      [this, role] { context_.edit([role](Document& d) { d.toggleMute(role); }); });
        h.mute->setPainter(smallPainter(false));
        h.mute->setAccessibleName("Mute " + title);
        h.mute->setAnnounce(Control::Announce::toggle);
        h.solo->setup(Control::Look::custom, "S",
                      [this, role] { context_.edit([role](Document& d) { d.toggleSolo(role); }); });
        h.solo->setPainter(smallPainter(false));
        h.solo->setAccessibleName("Solo " + title);
        h.solo->setAnnounce(Control::Announce::toggle);
    }

    playhead_ = std::make_unique<Playhead>(context);
    addChild(playhead_.get());
    playhead_->setOnTop(true);
}

Timeline::~Timeline() = default;

void Timeline::sync() {
    harmony_->sync();
    const auto& song = context_.song();
    for (std::size_t i = 0; i < kLaneRoles.size(); ++i) {
        const Role role = kLaneRoles[i];
        auto& h = headers_[i];
        h.mute->setOn(song.part(role).mute);
        h.solo->setOn(song.part(role).solo);
        h.mutate->setAccessibleName("Mutate " + h.select->label() + " (" +
                                    std::string(mutationName(context_.mutation)) + ")");
        h.select->redraw();
        lanes_[i]->clearHover();
        lanes_[i]->redraw();
    }
    stutter_->redraw();
    playhead_->redraw();
}

void Timeline::layoutChildren() {
    const auto& c = context_;
    place(*harmony_, c, kGridX, rowY(1), kGridW, kHarmonyH);
    for (std::size_t i = 0; i < kLaneRoles.size(); ++i) {
        const float y = rowY(static_cast<int>(i) + 2);
        const float h = kLaneH[i];
        place(*lanes_[i], c, kGridX, y, kGridW, h);
        auto& head = headers_[i];
        place(*head.select, c, kHeadX, y, kHeadW, h);
        const float top = y + (h - 53.0f) * 0.5f + 35.0f;
        place(*head.mutate, c, kHeadX + 9.0f, top, 20.0f, 18.0f);
        place(*head.mute, c, kHeadX + 32.0f, top, 20.0f, 18.0f);
        place(*head.solo, c, kHeadX + 55.0f, top, 20.0f, 18.0f);
    }
    place(*stutter_, c, kGridX, rowY(6), kGridW, kStutterH);
    place(*playhead_, c, kGridX - 6.0f, kTop, kGridW + 12.0f, designHeight() - 2.0f * kTop);
}

void Timeline::live() {
    const int pos = playhead_->position();
    if (pos != playheadShown_) {
        playheadShown_ = pos;
        playhead_->redraw();
    }
}

void Timeline::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    chrome(p);
    const auto& song = context_.song();
    auto& pattern = context_.doc().pattern(context_.focus);
    const ItemRef focus = context_.focus;

    // Ruler header and ruler
    std::string label;
    if (focus.isBridge())
        label = itemLetters(song, focus) + " · " + itemName(song, focus);
    else
        label = sectionLetter(focus.index) + " · " + itemName(song, focus) + " · " + keyName(pattern.key);
    p.text(ellipsize(label, 10.5f, Weight::semibold, kHeadW - 4.0f), 10.5f, Weight::semibold, colour::textDim,
           kHeadX + 2.0f, kTop, kHeadW - 2.0f, kRulerH);
    const int total = std::max(1, pattern.totalSteps());
    const float stepW = kGridW / static_cast<float>(total);
    for (int bar = 0; bar < total / kStepsPerBar; ++bar) {
        const float x = kGridX + static_cast<float>(bar * kStepsPerBar) * stepW;
        p.textAt(std::to_string(bar + 1), 10.5f, Weight::semibold, colour::textDim, x + 4.0f, kTop + 13.0f);
        if (4.0f * stepW < 18.0f)
            continue; // long sections: bar numbers only, the beat labels would collide
        for (int q = 1; q < 4; ++q)
            p.textAt("." + std::to_string(q + 1), 9.0f, Weight::medium, colour::quiet,
                     x + static_cast<float>(q * 4) * stepW + 3.0f, kTop + 13.0f);
    }

    // Harmony header
    const float hy = rowY(1);
    p.textAt("Harmony", 12.0f, Weight::semibold, colour::text, kHeadX + 2.0f, hy + kHarmonyH * 0.5f - 2.0f);
    std::string sub;
    if (focus.isBridge())
        sub = "Bridge · " + keyShort(pattern.key) + " " + std::string(kArrow) + " " + keyShort(pattern.toKey);
    else
        sub = "Voice motion " + std::to_string(voiceMotion(pattern)) + " st";
    p.textAt(sub, 10.5f, Weight::medium, colour::textDim, kHeadX + 2.0f, hy + kHarmonyH * 0.5f + 13.0f);

    // Stutter header
    const float sy = rowY(6);
    p.rect(colour::stutter(), kHeadX, sy + 6.0f, 2.0f, kStutterH - 12.0f, 1.0f);
    p.textAt("Stutter", 12.0f, Weight::semibold, colour::text, kHeadX + 9.0f, sy + kStutterH * 0.5f + 4.5f);
    p.textAt("ALL", 9.5f, Weight::semibold, colour::textFaint,
             kHeadX + 9.0f + textWidth("Stutter", 12.0f, Weight::semibold) + 6.0f, sy + kStutterH * 0.5f + 4.5f);
}

} // namespace lg::ui
