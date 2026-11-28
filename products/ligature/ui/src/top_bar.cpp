// SPDX-License-Identifier: AGPL-3.0-or-later
#include "top_bar.h"

#include <embedded/lg_brand.h>
#include <hba/ui/top_bar.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace lg::ui {

namespace {
constexpr float kBarHeight = 48.0f;
constexpr float kButtonHeight = 26.0f;
constexpr float kButtonY = (kBarHeight - kButtonHeight) * 0.5f;
constexpr float kGap = 10.0f;
constexpr float kMeterWidth = 64.0f;
constexpr float kMidiWidth = 68.0f;
constexpr float kPosWidth = 150.0f;

float buttonWidth(const std::string& label) { return std::ceil(textWidth(label, 11.5f, Weight::semibold)) + 22.0f; }

std::string badgeText(bool host, bool follower) { return follower ? "FOLLOW" : host ? "HOST" : "PREVIEW"; }
constexpr auto kStatusTime = std::chrono::seconds(5); // how long an export message replaces the position line

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}
} // namespace

TopBar::TopBar(Context& context, std::function<void()> showAbout)
    : Panel(context), previous_(context), name_(context), next_(context), undo_(context), redo_(context), ab_(context),
      play_(context), lens_(context), studio_(context), beat_(context), drag_(context) {
    addChild(&wordmark_);
    hba::ui::setupWordmarkButton(wordmark_, std::move(showAbout), [this](visage::Canvas& canvas) {
        hba::ui::drawBrandWordmark(canvas, wordmark_, brand::ligature_logotype_on_dark_svg, 1440.0f / 320.0f, 22.0f,
                                   0.0f, context_.scale, colour::focusRing);
    });
    for (visage::Frame* f : std::initializer_list<visage::Frame*>{&previous_, &name_, &next_, &undo_, &redo_, &ab_,
                                                                  &play_, &lens_, &studio_, &beat_, &drag_})
        addChild(f);

    previous_.setup(Control::Look::arrow, "‹", [this] {
        context_.model.stepPreset(-1);
        context_.songLoaded();
    });
    previous_.setAccessibleName("Previous preset");
    next_.setup(Control::Look::arrow, "›", [this] {
        context_.model.stepPreset(1);
        context_.songLoaded();
    });
    next_.setAccessibleName("Next preset");
    name_.setup(Control::Look::custom, "", [this] { hba::ui::showStudioPresetMenu(context_.model, &name_); });
    name_.setPainter([](const Paint& p, const Control& c) {
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        p.rect(colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
        p.border(c.hovered() ? colour::track : colour::edge, 0.0f, 0.0f, w, h, 7.0f);
        p.text(ellipsize(c.label(), 12.0f, Weight::semibold, w - 20.0f), 12.0f, Weight::semibold, colour::text, 0.0f,
               0.0f, w, h, visage::Font::kCenter);
    });
    undo_.setup(Control::Look::button, "Undo", [this] {
        context_.model.undo();
        context_.validate();
    });
    redo_.setup(Control::Look::button, "Redo", [this] {
        context_.model.redo();
        context_.validate();
    });

    play_.setup(Control::Look::custom, "Play", [this] { context_.model.togglePreview(context_.focus); });
    play_.setPainter([this](const Paint& p, const Control& c) {
        const float w = c.width() / p.s;
        const float h = c.height() / p.s;
        p.circle(c.hovered() ? colour::white : colour::text, w * 0.5f, h * 0.5f, std::min(w, h) * 0.5f);
        if (context_.model.telemetry().playing)
            p.stopIcon(colour::ink, w * 0.5f, h * 0.5f, 14.0f);
        else
            p.playIcon(colour::ink, w * 0.5f + 0.5f, h * 0.5f, 14.0f);
    });

    lens_.setup(Control::Look::button, "Lens", [this] {
        context_.lens = !context_.lens;
        context_.changed();
    });
    lens_.setAccessibleName("Theory lens");
    lens_.setAnnounce(Control::Announce::toggle);
    beat_.setup(Control::Look::button, "Beat", [this] {
        context_.model.setBeat(!context_.model.beat());
        context_.changed();
    });
    beat_.setAccessibleName("Audition beat");
    beat_.setAnnounce(Control::Announce::toggle);
    studio_.setup(Control::Look::button, "Studio", [this] {
        context_.model.setStudioVoices(!context_.model.studioVoices());
        context_.changed();
    });
    studio_.setAccessibleName("Studio instruments as the built-in sound");
    studio_.setAnnounce(Control::Announce::toggle);
    drag_.setup(Control::Look::button, "Drag MIDI", [this] { showDragMenu(); });
    // A failed export says why on the position line for a few seconds; the button keeps its size.
    drag_.onDragOut = [this] {
        context_.model.dragMidi(context_.dragScope, context_.focus, std::nullopt);
        status_ = context_.model.exportStatus();
        statusUntil_ = std::chrono::steady_clock::now() + kStatusTime;
        redraw();
    };
}

void TopBar::showDragMenu() {
    visage::PopupMenu menu;
    menu.addOption(static_cast<int>(DragScope::song), "Whole song: every section and bridge, one track per part");
    menu.addOption(static_cast<int>(DragScope::item),
                   context_.focus.isBridge() ? "This bridge, all parts" : "This section, all parts");
    menu.addOption(static_cast<int>(DragScope::performance), "Last performance: everything played since Play");
    menu.onSelection() = [this](int id) {
        context_.dragScope = static_cast<DragScope>(id);
        context_.changed();
    };
    menu.show(&drag_);
}

void TopBar::sync() {
    auto& m = context_.model;
    undo_.setEnabled(m.canUndo());
    redo_.setEnabled(m.canRedo());
    ab_.set(
        {"A", "B"}, m.isSlotB() ? 1 : 0,
        [this](int slot) {
            if ((slot == 1) != context_.model.isSlotB()) {
                context_.model.toggleAB();
                context_.validate();
            }
        },
        "Compare slot", 24.0f);
    for (std::size_t i = 0; i < ab_.count(); ++i) {
        ab_.option(i).onRightClick = [this] {
            hba::ui::showCopyABMenu(context_.model.isSlotB(), &ab_, [this] { context_.model.copyAB(); });
        };
        ab_.option(i).onAlternate = [this] { context_.model.copyAB(); }; // Shift+Return, as the preset menu says
    }
    lens_.setOn(context_.lens);
    beat_.setOn(m.beat());
    studio_.setOn(m.studioVoices());
    const char* scope = context_.dragScope == DragScope::song          ? "whole song"
                        : context_.dragScope == DragScope::performance ? "last performance"
                        : context_.focus.isBridge()                    ? "this bridge"
                                                                       : "this section";
    drag_.setAccessibleName(std::string("Drag MIDI, ") + scope);
    presetShown_ = m.presetName();
    name_.setLabel(presetShown_);
    name_.setAccessibleName("Preset " + presetShown_);
}

void TopBar::layoutChildren() {
    const auto& c = context_;
    const float w = designWidth();
    place(wordmark_, c, 12.0f, 0.0f, 105.0f, kBarHeight);
    float x = 127.0f;
    place(previous_, c, x, kButtonY, 24.0f, kButtonHeight);
    x += 26.0f;
    place(name_, c, x, kButtonY, 168.0f, kButtonHeight);
    x += 170.0f;
    place(next_, c, x, kButtonY, 24.0f, kButtonHeight);
    x += 24.0f + kGap;
    const float undoW = buttonWidth("Undo");
    place(undo_, c, x, kButtonY, undoW, kButtonHeight);
    x += undoW + kGap;
    const float redoW = buttonWidth("Redo");
    place(redo_, c, x, kButtonY, redoW, kButtonHeight);
    x += redoW + kGap;
    const float abW = std::max(54.0f, ab_.designWidth());
    place(ab_, c, x, kButtonY, abW, kButtonHeight);
    const float leftEnd = x + abW;

    // Right group, from the right edge.
    float r = w - 12.0f;
    meterX_ = r - kMeterWidth;
    r = meterX_ - kGap;
    const float dragW = std::max(buttonWidth(drag_.label()), 77.0f); // the label never changes: a fixed size
    place(drag_, c, r - dragW, kButtonY, dragW, kButtonHeight);
    r -= dragW + kGap;
    midiX_ = r - kMidiWidth;
    r = midiX_ - kGap;
    const float beatW = buttonWidth("Beat");
    place(beat_, c, r - beatW, kButtonY, beatW, kButtonHeight);
    r -= beatW + kGap;
    const float studioW = buttonWidth("Studio");
    place(studio_, c, r - studioW, kButtonY, studioW, kButtonHeight);
    r -= studioW + kGap;
    const float lensW = buttonWidth("Lens");
    place(lens_, c, r - lensW, kButtonY, lensW, kButtonHeight);
    rightStart_ = r - lensW;

    // Centre group between two equal spacers.
    badgeW_ = std::ceil(textWidth(badgeText(false, false), 10.0f, Weight::semibold) + 8.0f * 0.8f) + 14.0f;
    const float centreW = 30.0f + kGap + badgeW_ + kGap + kPosWidth;
    const float spacer = std::max(0.0f, (rightStart_ - leftEnd - centreW - 4.0f * kGap) * 0.5f);
    float cx = leftEnd + kGap + spacer + kGap;
    place(play_, c, cx, (kBarHeight - 30.0f) * 0.5f, 30.0f, 30.0f);
    cx += 30.0f + kGap;
    badgeX_ = cx;
    cx += badgeW_ + kGap;
    posX_ = cx;
}

TopBar::Shown TopBar::current() const {
    auto& m = context_.model;
    const auto& t = m.telemetry();
    Shown s;
    s.playing = t.playing;
    s.host = t.hostPlaying;
    s.follow = m.outputRole();
    s.leader = s.follow >= 0 && m.leaderFound();
    if (!status_.empty() && std::chrono::steady_clock::now() < statusUntil_)
        s.status = status_;
    if (t.playing) {
        const double beat = std::max(0.0, t.beat);
        s.bar = static_cast<int>(std::floor(beat / 4.0)) + 1;
        s.beat = static_cast<int>(std::floor(std::fmod(beat, 4.0))) + 1;
        s.item = itemName(context_.song(), t.performance.item);
        s.bpm = formatBpm(t.bpm);
    } else {
        s.bpm = formatBpm(context_.song().bpm);
    }
    const int out = m.outputRole();
    for (int r = 0; r < kRoleCount; ++r) {
        const auto& mask = t.sounding[static_cast<std::size_t>(r)];
        if ((mask[0] | mask[1]) == 0)
            continue;
        if (out < 0)
            s.midi |= 1u << r;
        else if (out == r)
            s.midi |= 1u << 4; // a follower sends its part on channel 1
    }
    s.meter = static_cast<int>(std::lround(std::clamp(t.peak * 1.1f, 0.0f, 1.0f) * 64.0f));
    return s;
}

void TopBar::live() {
    const auto now = current();
    if (now != shown_) {
        const bool transport = now.playing != shown_.playing;
        shown_ = now;
        if (transport) {
            play_.setAccessibleName(now.playing ? "Stop" : "Play");
            play_.redraw();
        }
        redraw();
    }
    // Beat and Studio are automatable: the host can change them between edits.
    beat_.setOn(context_.model.beat());
    studio_.setOn(context_.model.studioVoices());
    if (context_.model.presetName() != presetShown_) {
        presetShown_ = context_.model.presetName();
        name_.setLabel(presetShown_);
    }
}

void TopBar::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    p.rect(colour::rule, 0.0f, kBarHeight - 1.0f, designWidth(), 1.0f);
    const Shown s = current();

    // HOST / PREVIEW / FOLLOW
    const float by = 24.0f - 9.5f;
    const bool follower = s.follow >= 0;
    const unsigned badgeInk = follower ? colour::role(static_cast<Role>(s.follow))
                              : s.host ? colour::role(Role::chords)
                                       : colour::role(Role::bass);
    p.rect(follower ? colour::mix(colour::raised, badgeInk, 0.14f)
           : s.host ? 0xff1a2530u
                    : 0xff1d2a22u,
           badgeX_, by, badgeW_, 19.0f, 5.0f);
    const std::string badge = badgeText(s.host, follower);
    const float bw = spacedWidth(badge, 10.0f, Weight::semibold, 0.08f);
    p.spaced(badge, 10.0f, Weight::semibold, badgeInk, badgeX_ + (badgeW_ - bw) * 0.5f, 28.0f, 0.08f);

    // bar.beat · BPM · item
    const std::string pos = std::to_string(s.bar) + "." + std::to_string(s.beat);
    p.textAt(pos, 12.0f, Weight::semibold, colour::text, posX_, 28.5f);
    std::string rest = " · " + s.bpm + " BPM";
    unsigned restInk = colour::textDim;
    if (!s.status.empty()) {
        rest = " · " + s.status;
        restInk = colour::hot;
    } else if (follower) {
        // A follower's edits go nowhere: say whose song it plays, or that it has none.
        rest += s.leader ? " · " + lower(roleName(static_cast<Role>(s.follow))) + " of the Lead instance"
                         : " · no Lead instance: set one to Lead";
        if (!s.leader)
            restInk = colour::hot;
    } else if (!s.item.empty()) {
        rest += " · " + s.item;
    }
    const float posW = textWidth(pos, 12.0f, Weight::semibold);
    const float restW = std::max(40.0f, rightStart_ - kGap - (posX_ + posW));
    p.textAt(ellipsize(rest, 12.0f, Weight::medium, restW), 12.0f, Weight::medium, restInk, posX_ + posW, 28.5f,
             visage::Font::kLeft, restW + 8.0f);

    // MIDI 1-4
    p.textAt("MIDI", 10.5f, Weight::semibold, colour::textDim, midiX_, 28.0f);
    const float dotsX = midiX_ + kMidiWidth - 4.0f * 7.0f - 3.0f * 4.0f;
    const int out = context_.model.outputRole();
    for (int ch = 0; ch < 4; ++ch) {
        unsigned fill = colour::track;
        if (out < 0 && (s.midi & (1u << ch)) != 0)
            fill = colour::role(static_cast<Role>(ch));
        else if (out >= 0 && ch == 0 && (s.midi & (1u << 4)) != 0)
            fill = colour::role(static_cast<Role>(out));
        p.circle(fill, dotsX + static_cast<float>(ch) * 11.0f + 3.5f, 24.0f, 3.5f);
    }

    // Output meter
    p.rect(colour::meterTrack, meterX_, 21.0f, kMeterWidth, 6.0f, 3.0f);
    const float level = static_cast<float>(s.meter) / 64.0f * kMeterWidth;
    if (level > 0.5f) {
        visage::Gradient g(colour::role(Role::bass), colour::gold(), colour::hot);
        canvas.setColor(visage::Brush::linear(g, {p.px(meterX_), 0.0f}, {p.px(meterX_ + kMeterWidth), 0.0f}));
        canvas.roundedRectangle(p.px(meterX_), p.px(21.0f), p.px(level), p.px(6.0f), p.px(3.0f));
    }
}

} // namespace lg::ui
