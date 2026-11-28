// SPDX-License-Identifier: AGPL-3.0-or-later
#include "song_strip.h"

#include <algorithm>
#include <cmath>

namespace lg::ui {

namespace {
constexpr float kStripH = 90.0f;
constexpr float kTileH = 70.0f;
constexpr float kTileY = (kStripH - kTileH) * 0.5f;
constexpr float kKnotH = 50.0f;
constexpr float kKnotY = (kStripH - kKnotH) * 0.5f;
constexpr float kKnotMargin = 14.0f;
constexpr float kRing = 2.0f; // controls reach 2 px past what they draw, for the focus and queued rings
constexpr float kMid = kStripH * 0.5f;
constexpr unsigned kKnotEdge = 0xff2a3440;
constexpr unsigned kKnotHover = 0xff3a4756;
constexpr unsigned kConnector = 0xff2e3946;

std::string barsText(int bars) { return std::to_string(bars) + (bars == 1 ? " bar" : " bars"); }
} // namespace

SongStrip::SongStrip(Context& context)
    : Panel(context), tiles_(context, *this), knots_(context, *this), launches_(context, *this), fires_(context, *this),
      add_(context), land_(context), follow_(context) {
    addChild(&add_);
    addChild(&land_);
    addChild(&follow_);
    add_.setup(Control::Look::custom, "+", [this] { context_.edit([](Document& d) { d.addSection(); }); });
    add_.setAccessibleName("Add a section");
    add_.setPainter([](const Paint& p, const Control& c) {
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        p.dashedRoundRect(c.hovered() ? colour::textDim : colour::hoverEdge, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 9.0f, 1.0f,
                          3.0f, 2.5f);
        p.text("+", 18.0f, Weight::regular,
               c.enabled() ? (c.hovered() ? colour::text : colour::textFaint)
                           : colour::withAlpha(colour::textFaint, 0.4f),
               0.0f, -1.0f, w, h, visage::Font::kCenter);
    });
    follow_.setup(Control::Look::button, "Follow", [this] {
        context_.follow = !context_.follow;
        context_.changed();
    });
    follow_.setAccessibleName("Follow the playing item");
    follow_.setAnnounce(Control::Announce::toggle);
}

namespace {
int landIndex(int bars) { return bars <= 1 ? 0 : bars == 2 ? 1 : 2; }
} // namespace

float SongStrip::pulse(float period) const {
    using namespace std::chrono;
    const double t = duration<double>(steady_clock::now().time_since_epoch()).count();
    return 0.5f + 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265 * std::fmod(t, period) / period));
}

SongStrip::LiveState SongStrip::liveState() const {
    const auto& t = context_.model.telemetry();
    LiveState s;
    if (!t.playing)
        return s;
    const auto& song = context_.song();
    const auto& perf = t.performance;
    s.playing = true;
    s.item = perf.item;
    const float total = static_cast<float>(std::max(1, t.playingBars) * kStepsPerBar);
    s.progress = static_cast<int>(std::lround(std::clamp((t.stepInItem + 1.0f) / total, 0.0f, 1.0f) * 1000.0f));
    s.hold = perf.hold;
    const int toLand = t.barsToLand >= 0
                           ? t.barsToLand
                           : static_cast<int>(std::ceil((total - t.stepInItem) / static_cast<float>(kStepsPerBar)));
    if (perf.planActive) {
        if (perf.planTarget >= 0) {
            s.queuedTile = perf.planTarget;
            s.tileLabel = "LANDS IN " + std::to_string(std::max(0, toLand));
        }
        if (perf.planBridge >= 0 && perf.planBridge < static_cast<int>(song.bridges.size())) {
            s.queuedKnot = perf.planBridge;
            s.knotLabel = t.barsToStart <= 0 ? "NEXT" : "IN " + std::to_string(t.barsToStart);
            if (perf.planFit > 0 && perf.planFit < song.bridges[static_cast<std::size_t>(perf.planBridge)].bars)
                s.knotLabel += " · SQUEEZED";
        }
    } else if (perf.item.isBridge() && perf.target >= 0) {
        s.queuedTile = perf.target;
        s.tileLabel = perf.hold ? "HOLDING" : "LANDS IN " + std::to_string(std::max(0, toLand));
    }
    if (s.queuedTile >= 0 || s.queuedKnot >= 0 || s.hold)
        s.pulse = static_cast<int>(pulse(s.hold ? 0.45f : 0.9f) * 24.0f);
    return s;
}

void SongStrip::sync() {
    const auto& song = context_.song();
    const auto sections = song.sections.size();
    const auto bridges = song.bridges.size();
    tiles_.resize(sections);
    knots_.resize(bridges);
    launches_.resize(sections);
    fires_.resize(bridges);

    for (std::size_t i = 0; i < sections; ++i) {
        const int index = static_cast<int>(i);
        const auto& sec = song.sections[i];
        auto& tile = tiles_[i];
        tile.setup(Control::Look::custom, sec.name, [this, index] { context_.focusOn(ItemRef::section(index)); });
        tile.setAccessibleName("Section " + sectionLetter(index) + ", " + sec.name + ", " + keyName(sec.key));
        tile.setPainter([this, index](const Paint& p, const Control& c) { paintTile(p, c, index); });
        tile.onRightClick = [this, index] {
            visage::PopupMenu menu;
            menu.addOption(1, "Launch directly (skip the bridge)");
            if (context_.song().sections.size() > 1)
                menu.addOption(2, "Remove section");
            menu.onSelection() = [this, index](int id) {
                if (id == 1)
                    context_.model.launch(index, true);
                else if (id == 2) {
                    const int count = static_cast<int>(context_.song().sections.size());
                    context_.edit([index](Document& d) { d.removeSection(index); });
                    context_.sectionRemoved(index, count);
                }
            };
            menu.show(&tiles_[static_cast<std::size_t>(index)]);
        };

        auto& launch = launches_[i];
        launch.setup(Control::Look::custom, "Launch " + sec.name, {});
        launch.onClick = [this, index](const visage::MouseEvent* e) {
            context_.model.launch(index, e != nullptr && e->isShiftDown());
        };
        launch.setPainter([this](const Paint& p, const Control& c) { paintRound(p, c, false); });
        launch.setOnTop(true);
    }
    for (std::size_t i = 0; i < bridges; ++i) {
        const int index = static_cast<int>(i);
        const auto name = itemName(song, ItemRef::bridge(index));
        auto& knot = knots_[i];
        knot.setup(Control::Look::custom, name, [this, index] { context_.focusOn(ItemRef::bridge(index)); });
        knot.setAccessibleName("Bridge " + itemLetters(song, ItemRef::bridge(index)) + ", " + name);
        knot.setPainter([this, index](const Paint& p, const Control& c) { paintKnot(p, c, index); });

        auto& fire = fires_[i];
        fire.setup(Control::Look::custom, "Fire " + name + ", hold to build", {});
        fire.onPress = [this, index] {
            pressedBridge_ = index;
            held_ = false;
            pressTime_ = std::chrono::steady_clock::now();
        };
        fire.onRelease = [this, index] {
            wasHeld_ = held_;
            if (held_)
                context_.model.hold(index, false);
            held_ = false;
            pressedBridge_ = -1;
        };
        fire.onClick = [this, index](const visage::MouseEvent* e) {
            if (e != nullptr && wasHeld_)
                return;
            context_.model.fire(index);
        };
        fire.setPainter([this](const Paint& p, const Control& c) { paintRound(p, c, true); });
        fire.setOnTop(true);
    }

    add_.setEnabled(sections < static_cast<std::size_t>(kMaxSections));
    land_.set(
        {"Bar", "2", "Phrase"}, landIndex(context_.model.land()),
        [this](int i) {
            context_.model.setLand(i == 0 ? 1 : i == 1 ? 2 : 4);
            context_.changed();
        },
        "Land on");
    follow_.setOn(context_.follow);
}

void SongStrip::layoutChildren() {
    const auto& c = context_;
    const auto& song = c.song();
    if (tiles_.size() != song.sections.size() || knots_.size() != song.bridges.size())
        return; // not synced to this song yet
    const float w = designWidth();
    const auto order = stripOrder(song);
    const int n = static_cast<int>(song.sections.size());
    const int nb = static_cast<int>(song.bridges.size());
    const float loopW = textWidth("\u21ba " + sectionLetter(0), 11.0f, Weight::semibold);
    const float followW = std::ceil(textWidth("Follow", 11.5f, Weight::semibold)) + 22.0f;
    const float landW = land_.designWidth();
    const float labelW = textWidth("Land", 11.0f, Weight::semibold);
    const float natural = static_cast<float>(n) * 212.0f + static_cast<float>(nb) * 156.0f;
    const auto fixedFor = [&](float margin) {
        return static_cast<float>(nb) * 2.0f * margin + (nb > 0 ? -2.0f : 10.0f) + loopW + 14.0f + 34.0f;
    };

    // Right: Land [Bar|2|Phrase] [Follow] in a row, or stacked when three or four sections need the room.
    const float rowWidth = labelW + 8.0f + landW + 8.0f + followW;
    compact_ = 12.0f + natural + fixedFor(kKnotMargin) + 16.0f + rowWidth + 12.0f > w;
    float rightStart = 0.0f;
    if (!compact_) {
        float r = w - 12.0f - followW;
        place(follow_, c, r, kMid - 13.0f, followW, 26.0f);
        r -= 8.0f + landW;
        place(land_, c, r, kMid - 13.0f, landW, 26.0f);
        landLabelX_ = r - 8.0f - labelW;
        landLabelY_ = kMid;
        rightStart = landLabelX_;
    } else {
        const float r = w - 12.0f;
        place(land_, c, r - landW, 14.0f, landW, 26.0f);
        place(follow_, c, r - followW, 50.0f, followW, 26.0f);
        landLabelX_ = r - landW - 8.0f - labelW;
        landLabelY_ = 27.0f;
        rightStart = landLabelX_;
    }

    // Left: the ring. Knots keep room for their route name; tiles give way first.
    knotMargin_ = compact_ ? 10.0f : kKnotMargin;
    const float available = rightStart - 16.0f - 12.0f - fixedFor(knotMargin_);
    if (natural <= available || n == 0) {
        tileW_ = 212.0f;
        knotW_ = 156.0f;
    } else {
        knotW_ = nb > 0 ? std::clamp(available * 0.42f / static_cast<float>(nb), 100.0f, 156.0f) : 0.0f;
        tileW_ = (available - static_cast<float>(nb) * knotW_) / static_cast<float>(n);
        if (tileW_ < 110.0f) {
            tileW_ = 110.0f;
            knotW_ =
                nb > 0 ? std::max(72.0f, (available - static_cast<float>(n) * tileW_) / static_cast<float>(nb)) : 0.0f;
        }
        tileW_ = std::floor(std::min(tileW_, 212.0f));
        knotW_ = std::floor(knotW_);
    }

    slots_.clear();
    float x = 12.0f;
    for (const auto& item : order) {
        if (item.isBridge()) {
            x += knotMargin_;
            slots_.push_back({item, x, knotW_});
            auto& knot = knots_[static_cast<std::size_t>(item.index)];
            place(knot, c, x - kRing, kKnotY - kRing, knotW_ + 2.0f * kRing, kKnotH + 2.0f * kRing);
            place(fires_[static_cast<std::size_t>(item.index)], c, x + knotW_ - 30.0f, kKnotY + 13.0f, 24.0f, 24.0f);
            x += knotW_ + knotMargin_;
        } else {
            slots_.push_back({item, x, tileW_});
            place(tiles_[static_cast<std::size_t>(item.index)], c, x - kRing, kTileY - kRing, tileW_ + 2.0f * kRing,
                  kTileH + 2.0f * kRing);
            place(launches_[static_cast<std::size_t>(item.index)], c, x + tileW_ - 34.0f, kTileY + 8.0f, 24.0f, 24.0f);
            x += tileW_;
        }
    }
    x += nb > 0 ? -2.0f : 10.0f;
    loopX_ = x;
    x += loopW + 14.0f;
    place(add_, c, x, kMid - 17.0f, 34.0f, 34.0f);
}

void SongStrip::live() {
    land_.select(landIndex(context_.model.land())); // Land is automatable: the host can change it any time
    const auto now = liveState();
    if (!(now == shown_)) {
        shown_ = now;
        redraw();
        for (std::size_t i = 0; i < tiles_.size(); ++i)
            tiles_[i].redraw();
        for (std::size_t i = 0; i < knots_.size(); ++i)
            knots_[i].redraw();
        for (std::size_t i = 0; i < fires_.size(); ++i)
            fires_[i].redraw();
    }
    if (pressedBridge_ >= 0 && !held_ &&
        std::chrono::steady_clock::now() - pressTime_ >= std::chrono::milliseconds(230)) {
        held_ = true;
        context_.model.hold(pressedBridge_, true);
    }
}

void SongStrip::paintRound(const Paint& p, const Control& c, bool fire) const {
    const float w = c.width() / p.s;
    const float h = c.height() / p.s;
    unsigned fill = c.hovered() ? colour::knobHover : colour::knob;
    if (c.pressed())
        fill = colour::track;
    p.circle(fill, w * 0.5f, h * 0.5f, std::min(w, h) * 0.5f);
    const bool holding = fire && held_ && c.pressed();
    p.playIcon(holding ? colour::gold() : colour::text, w * 0.5f + 0.5f, h * 0.5f, 10.0f);
}

void SongStrip::paintTile(const Paint& p, const Control& c, int section) const {
    const auto& song = context_.song();
    if (section >= static_cast<int>(song.sections.size()))
        return;
    const auto& sec = song.sections[static_cast<std::size_t>(section)];
    const float hue = keyHue(sec.key);
    const float x0 = kRing;
    const float y0 = kRing;
    const float W = c.width() / p.s - 2.0f * kRing;
    const float H = kTileH;
    const ItemRef me = ItemRef::section(section);
    const bool playing = shown_.playing && shown_.item == me;
    const bool queued = shown_.queuedTile == section;

    if (playing)
        p.vgrad(colour::mix(colour::raised, colour::oklch(0.72f, 0.15f, hue), 0.12f), colour::raised, x0, y0, W, H,
                10.0f);
    else
        p.rect(colour::raised, x0, y0, W, H, 10.0f);
    p.clipped(x0, y0, 3.0f, H, [&] { p.rect(colour::oklch(0.72f, 0.15f, hue), x0, y0, W, H, 10.0f); });
    p.border(playing ? colour::oklch(0.72f, 0.15f, hue, 0.8f) : (c.hovered() ? colour::hoverEdge : colour::edge), x0,
             y0, W, H, 10.0f);
    if (context_.focus == me)
        p.border(colour::focusRing, x0 - 1.5f, y0 - 1.5f, W + 3.0f, H + 3.0f, 11.5f, 1.5f);
    else if (queued)
        p.border(colour::withAlpha(colour::gold(), 0.35f + 0.65f * static_cast<float>(shown_.pulse) / 24.0f), x0 - 1.5f,
                 y0 - 1.5f, W + 3.0f, H + 3.0f, 11.5f, 1.5f);

    // Letter, name, launch (the launch button is its own control)
    p.rect(colour::oklch(0.72f, 0.15f, hue, 0.22f), x0 + 12.0f, y0 + 11.0f, 18.0f, 18.0f, 5.0f);
    p.text(sectionLetter(section), 11.0f, Weight::semibold, colour::oklch(0.86f, 0.1f, hue), x0 + 12.0f, y0 + 11.0f,
           18.0f, 18.0f, visage::Font::kCenter);
    // "LANDS IN 3" sits beside the name on a wide tile; with three or four sections it takes the key line's end.
    const bool label = queued && !shown_.tileLabel.empty();
    const float lw = label ? spacedWidth(shown_.tileLabel, 9.5f, Weight::semibold, 0.06f) : 0.0f;
    const bool labelBesideName = label && W - 73.0f - lw - 10.0f >= 60.0f;
    const float nameW = W - 37.0f - 36.0f - (labelBesideName ? lw + 10.0f : 0.0f);
    p.text(ellipsize(sec.name, 13.0f, Weight::semibold, nameW), 13.0f, Weight::semibold, colour::text, x0 + 37.0f,
           y0 + 10.0f, nameW, 20.0f);
    if (labelBesideName)
        p.spaced(shown_.tileLabel, 9.5f, Weight::semibold, colour::gold(), x0 + W - 40.0f - lw, y0 + 18.5f, 0.06f);
    else if (label)
        p.spaced(shown_.tileLabel, 9.5f, Weight::semibold, colour::gold(), x0 + W - 10.0f - lw, y0 + 44.5f, 0.06f);

    // Key and length
    const float keyW = W - 22.0f - (label && !labelBesideName ? lw + 8.0f : 0.0f);
    p.text(ellipsize(keyName(sec.key) + " · " + barsText(sec.bars), 11.0f, Weight::medium, keyW), 11.0f, Weight::medium,
           colour::textDim, x0 + 12.0f, y0 + 34.0f, keyW, 14.0f);

    // The progression in miniature: width is length, height is tension.
    const auto& pattern = context_.doc().pattern(me);
    const float miniW = W - 62.0f;
    const float total = static_cast<float>(std::max(1, pattern.totalSteps()));
    float x = x0 + 12.0f;
    const unsigned miniInk = colour::oklch(0.72f, 0.12f, hue, 0.55f);
    for (const auto& span : pattern.spans) {
        const float bw = std::max(2.0f, span.dur / total * miniW);
        const float bh = 4.0f + std::clamp(span.tension, 0.0f, 1.0f) * 10.0f;
        p.rect(miniInk, x, y0 + 64.0f - bh, bw, bh, 2.0f);
        x += bw + 2.0f;
    }

    if (playing) {
        const float progress = static_cast<float>(shown_.progress) / 1000.0f;
        p.clipped(x0, y0 + H - 2.0f, W * progress, 2.0f,
                  [&] { p.rect(colour::oklch(0.8f, 0.13f, hue), x0, y0, W, H, 10.0f); });
    }
}

void SongStrip::paintKnot(const Paint& p, const Control& c, int bridge) const {
    const auto& song = context_.song();
    if (bridge >= static_cast<int>(song.bridges.size()))
        return;
    const auto& b = song.bridges[static_cast<std::size_t>(bridge)];
    const Key from = song.sections[static_cast<std::size_t>(b.from)].key;
    const Key to = song.sections[static_cast<std::size_t>(b.to)].key;
    const float x0 = kRing;
    const float y0 = kRing;
    const float W = c.width() / p.s - 2.0f * kRing;
    const float H = kKnotH;
    const ItemRef me = ItemRef::bridge(bridge);
    const bool playing = shown_.playing && shown_.item == me;
    const bool held = playing && shown_.hold;
    const bool queued = shown_.queuedKnot == bridge;
    const float alpha = playing ? 0.38f : 0.2f;

    p.hgrad(colour::mix(colour::panel, colour::keyColour(from), alpha),
            colour::mix(colour::panel, colour::keyColour(to), alpha), x0, y0, W, H, H * 0.5f);
    p.border(playing ? colour::gold() : (c.hovered() ? kKnotHover : kKnotEdge), x0, y0, W, H, H * 0.5f);
    if (context_.focus == me)
        p.border(colour::focusRing, x0 - 1.5f, y0 - 1.5f, W + 3.0f, H + 3.0f, H * 0.5f + 1.5f, 1.5f);
    else if (held || queued)
        p.border(colour::withAlpha(colour::gold(), 0.35f + 0.65f * static_cast<float>(shown_.pulse) / 24.0f), x0 - 1.5f,
                 y0 - 1.5f, W + 3.0f, H + 3.0f, H * 0.5f + 1.5f, 1.5f);

    const bool narrow = W < 130.0f;
    const float textX = narrow ? 11.0f : 14.0f;
    const float textW = W - textX - 36.0f;
    const float nameSize = narrow ? 11.0f : 12.0f;
    p.text(ellipsize(itemName(song, me), nameSize, Weight::semibold, textW), nameSize, Weight::semibold, colour::text,
           x0 + textX, y0 + 10.0f, textW, 16.0f);
    const std::string keys = keyShort(from) + " " + std::string(kArrow) + " " + keyShort(to);
    std::string meta = barsText(b.bars) + " \u00b7 " + keys;
    if (textWidth(meta, 10.0f, Weight::medium) > textW)
        meta = keys;
    p.text(ellipsize(meta, 10.0f, Weight::medium, textW), 10.0f, Weight::medium, colour::textDim, x0 + textX,
           y0 + 26.0f, textW, 14.0f);
}

void SongStrip::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    chrome(p);
    const auto& song = context_.song();

    for (const auto& slot : slots_) {
        if (!slot.item.isBridge())
            continue;
        p.line(kConnector, slot.x - knotMargin_, kMid, slot.x, kMid, 1.5f);
        p.line(kConnector, slot.x + slot.w, kMid, slot.x + slot.w + knotMargin_, kMid, 1.5f);
        if (shown_.queuedKnot == slot.item.index && !shown_.knotLabel.empty())
            p.spaced(shown_.knotLabel, 9.5f, Weight::semibold, colour::gold(), slot.x + 14.0f, kKnotY - 4.0f, 0.06f);
    }
    if (!song.sections.empty())
        p.text("↺ " + sectionLetter(0), 11.0f, Weight::semibold, colour::textFaint, loopX_, kMid - 8.0f, 60.0f, 16.0f);
    p.text("Land", 11.0f, Weight::semibold, colour::textDim, landLabelX_, landLabelY_ - 8.0f, 40.0f, 16.0f);
}

} // namespace lg::ui
