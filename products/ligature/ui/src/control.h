// SPDX-License-Identifier: AGPL-3.0-or-later
// One clickable frame for everything the editor offers: buttons, toggles, segments, chips, tiles and rows. It is an
// hba::ui::ActionControl, so the plugin's accessibility layer mirrors it, and it takes Return/Space from the keyboard.
// Panels keep their controls for the editor's lifetime (pools grow, extras hide), so a handler may change anything.
#pragma once

#include "theme.h"

#include <algorithm>
#include <hba/ui/accessible_controls.h>
#include <hba/ui/controls.h>
#include <visage/ui.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lg::ui {

class Control final : public visage::Frame, public hba::ui::ActionControl {
  public:
    enum class Look : std::uint8_t {
        button,  // .lg-b: raised pill; `on` lights it
        segment, // one option of a segmented control (the container draws the track)
        arrow,   // preset arrows: text only, hover fill
        chip,    // style chips and key buttons: `accent` borders the selected one
        custom,  // `painter` draws everything
    };
    using Painter = std::function<void(const Paint&, const Control&)>;
    // What a screen reader hears after the name: a toggle says on or off, a choice says when it is selected.
    enum class Announce : std::uint8_t { none, toggle, choice };

    explicit Control(Context& context) : context_(context) {
        setAcceptsKeystrokes(true);
        onDraw() += [this](visage::Canvas& canvas) { hba::ui::drawKeyboardFocus(canvas, *this, context_.scale); };
    }

    // Set during a panel's sync; every setter redraws.
    void setup(Look look, std::string label, std::function<void()> click) {
        look_ = look;
        setLabel(std::move(label));
        onClick = [fn = std::move(click)](const visage::MouseEvent*) {
            if (fn)
                fn();
        };
        redraw();
    }
    void setLabel(std::string label) {
        if (label_ != label) {
            label_ = std::move(label);
            redraw();
        }
        updateName();
    }
    void setAccessibleName(std::string name) {
        accessibleName_ = std::move(name);
        updateName();
    }
    void setAnnounce(Announce announce) {
        announce_ = announce;
        updateName();
    }
    void setOn(bool on) {
        if (on_ != on) {
            on_ = on;
            updateName();
            redraw();
        }
    }
    void setEnabled(bool enabled) {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        if (!enabled)
            hovered_ = pressed_ = false;
        setAcceptsKeystrokes(enabled);
        redraw();
    }
    void setAccent(unsigned accent) {
        if (accent_ != accent) {
            accent_ = accent;
            redraw();
        }
    }
    void setPainter(Painter painter) {
        painter_ = std::move(painter);
        look_ = Look::custom;
        redraw();
    }
    void setTextSize(float size) { textSize_ = size; }

    const std::string& label() const { return label_; }
    bool on() const { return on_; }
    bool enabled() const { return enabled_; }
    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    unsigned accent() const { return accent_; }
    Context& context() const { return context_; }

    // Click (mouse up inside, or Return/Space). The event is null for keyboard and accessibility activation.
    std::function<void(const visage::MouseEvent*)> onClick;
    std::function<void()> onPress;   // mouse down
    std::function<void()> onRelease; // mouse up, inside or not
    std::function<bool(const visage::MouseEvent&)> onWheel;
    std::function<void(bool)> onHover;
    std::function<void()> onRightClick;
    std::function<void()> onDragOut;   // a press that moves 5 px
    std::function<void()> onAlternate; // Shift+Return or Shift+Space, e.g. copy A to B

    void activate() override {
        if (enabled_ && onClick)
            onClick(nullptr);
    }

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / context_.scale;
        const float h = height() / context_.scale;
        switch (look_) {
        case Look::button: {
            const bool lit = on_ || (hovered_ && enabled_);
            p.rect(on_ ? colour::buttonOn : colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
            p.border(on_ ? colour::buttonOnEdge : (hovered_ && enabled_ ? colour::track : colour::edge), 0.0f, 0.0f, w,
                     h, 7.0f);
            const unsigned ink = !enabled_ ? colour::withAlpha(colour::textDim, 0.4f)
                                 : accent_ ? accent_
                                 : lit     ? colour::text
                                           : colour::textDim;
            p.text(label_, textSize_ > 0.0f ? textSize_ : 11.5f, Weight::semibold, ink, 0.0f, 0.0f, w, h,
                   visage::Font::kCenter);
            break;
        }
        case Look::segment:
            if (on_)
                p.rect(colour::segmentOn, 0.0f, 0.0f, w, h, 5.0f);
            p.text(label_, textSize_ > 0.0f ? textSize_ : 11.0f, Weight::semibold,
                   on_ || hovered_ ? colour::text : colour::textDim, 0.0f, 0.0f, w, h, visage::Font::kCenter);
            break;
        case Look::arrow:
            if (hovered_)
                p.rect(colour::raised, 0.0f, 0.0f, w, h, 7.0f);
            p.text(label_, 15.0f, Weight::medium, hovered_ ? colour::text : colour::textDim, 0.0f, -1.0f, w, h,
                   visage::Font::kCenter);
            break;
        case Look::chip: {
            const unsigned fill = on_ && accent_ ? colour::mix(colour::raised, accent_, 0.16f) : colour::raised;
            p.rect(fill, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f);
            p.border(on_ && accent_ ? accent_ : (hovered_ ? colour::hoverEdge : colour::edge), 0.0f, 0.0f, w, h, 6.0f);
            p.text(label_, textSize_ > 0.0f ? textSize_ : 11.0f, Weight::semibold,
                   on_ || hovered_ ? colour::text : colour::textDim, 0.0f, 0.0f, w, h, visage::Font::kCenter);
            break;
        }
        case Look::custom:
            if (painter_)
                painter_(p, *this);
            break;
        }
    }

    void focusChanged(bool, bool) override { redraw(); }

    bool keyPress(const visage::KeyEvent& key) override {
        if (!enabled_ || key.isMainModifier())
            return false;
        if (key.keyCode() != visage::KeyCode::Return && key.keyCode() != visage::KeyCode::Space)
            return false;
        if (!key.isRepeat()) {
            if (key.isShiftDown() && onAlternate)
                onAlternate();
            else
                activate();
        }
        return true;
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = enabled_;
        if (onHover)
            onHover(true);
        redraw();
    }

    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        if (onHover)
            onHover(false);
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (!enabled_)
            return;
        if (e.shouldTriggerPopup()) {
            if (onRightClick)
                onRightClick();
            return;
        }
        pressed_ = true;
        draggedOut_ = false;
        pressPosition_ = e.position;
        if (onPress)
            onPress();
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!pressed_ || draggedOut_ || !onDragOut)
            return;
        const float dx = e.position.x - pressPosition_.x;
        const float dy = e.position.y - pressPosition_.y;
        const float threshold = context_.px(5.0f);
        if (dx * dx + dy * dy >= threshold * threshold) {
            draggedOut_ = true;
            pressed_ = false;
            redraw();
#if defined(__linux__)
            hba::ui::releasePointerForFileDrag(*this, e);
#endif
            onDragOut();
        }
    }

    void mouseUp(const visage::MouseEvent& e) override {
        const bool wasPressed = pressed_;
        const bool click = enabled_ && pressed_ && !draggedOut_ && localBounds().contains(e.position);
        pressed_ = false;
        draggedOut_ = false;
        redraw();
        if (wasPressed && onRelease)
            onRelease();
        if (click && onClick)
            onClick(&e);
    }

    bool mouseWheel(const visage::MouseEvent& e) override { return onWheel ? onWheel(e) : false; }

  private:
    void updateName() {
        std::string name = accessibleName_.empty() ? label_ : accessibleName_;
        if (announce_ == Announce::toggle)
            name += on_ ? ", on" : ", off";
        else if (announce_ == Announce::choice && on_)
            name += ", selected";
        setName(name);
    }

    Context& context_;
    Look look_ = Look::button;
    Announce announce_ = Announce::none;
    std::string label_;
    std::string accessibleName_;
    Painter painter_;
    unsigned accent_ = 0;
    float textSize_ = 0.0f;
    bool on_ = false;
    bool enabled_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool draggedOut_ = false;
    visage::Point pressPosition_{};
};

// A pool of controls that only grows: sync() asks for `count` and hides the rest.
class ControlPool {
  public:
    ControlPool(Context& context, visage::Frame& parent) : context_(context), parent_(parent) {}

    Control& operator[](std::size_t i) { return *controls_[i]; }
    std::size_t size() const { return shown_; }

    void resize(std::size_t count) {
        while (controls_.size() < count) {
            controls_.push_back(std::make_unique<Control>(context_));
            parent_.addChild(controls_.back().get(), false);
        }
        for (std::size_t i = 0; i < controls_.size(); ++i)
            controls_[i]->setVisible(i < count);
        shown_ = count;
    }
    void hide() { resize(0); }

  private:
    Context& context_;
    visage::Frame& parent_;
    std::vector<std::unique_ptr<Control>> controls_;
    std::size_t shown_ = 0;
};

// A segmented control (.lg-seg): a track with one Control per option. Positions are in design pixels.
class Segmented final : public visage::Frame {
  public:
    explicit Segmented(Context& context) : context_(context), options_(context, *this) {}

    // Lays the options out at their natural widths; returns the design width.
    float set(const std::vector<std::string>& labels, int selected, std::function<void(int)> choose,
              const std::string& groupName = {}, float minOption = 0.0f) {
        options_.resize(labels.size());
        widths_.clear();
        float total = 2.0f;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            auto& c = options_[i];
            c.setup(Control::Look::segment, labels[i], [choose, i] {
                if (choose)
                    choose(static_cast<int>(i));
            });
            c.setAccessibleName(groupName.empty() ? labels[i] : groupName + " " + labels[i]);
            c.setAnnounce(Control::Announce::choice);
            c.setOn(static_cast<int>(i) == selected);
            const float w = std::max(minOption, textWidth(labels[i], 11.0f, Weight::semibold) + 16.0f);
            widths_.push_back(w);
            total += w + 2.0f;
        }
        designWidth_ = total;
        layoutOptions();
        redraw();
        return total;
    }
    float designWidth() const { return designWidth_; }
    // Lights one option (or none, with -1) without rebuilding: for values the host can automate.
    void select(int selected) {
        for (std::size_t i = 0; i < options_.size(); ++i)
            options_[i].setOn(static_cast<int>(i) == selected);
    }
    Control& option(std::size_t i) { return options_[i]; }
    std::size_t count() const { return options_.size(); }
    void resized() override { layoutOptions(); }

    void draw(visage::Canvas& canvas) override {
        const Paint p{canvas, context_.scale};
        const float w = width() / context_.scale;
        const float h = height() / context_.scale;
        p.rect(colour::raised, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 7.0f);
        p.border(colour::edge, 0.0f, 0.0f, w, h, 7.0f);
    }

  private:
    void layoutOptions() {
        const float s = context_.scale;
        const float h = height() / s;
        const float stretch = widths_.empty() ? 1.0f : std::max(1.0f, (width() / s) / std::max(1.0f, designWidth_));
        float x = 2.0f * stretch;
        for (std::size_t i = 0; i < widths_.size(); ++i) {
            const float w = widths_[i] * stretch;
            options_[i].setBounds(x * s, 2.0f * s, w * s, (h - 4.0f) * s);
            x += w + 2.0f;
        }
    }

    Context& context_;
    ControlPool options_;
    std::vector<float> widths_;
    float designWidth_ = 0.0f;
};

// Places a frame in design pixels.
inline void place(visage::Frame& frame, const Context& context, float x, float y, float w, float h) {
    frame.setBounds(context.px(x), context.px(y), context.px(w), context.px(h));
}

} // namespace lg::ui

namespace lg::ui {

// A panel of the editor: syncs its controls when the song or the editor state changes, redraws live state each frame.
class Panel : public visage::Frame, public Live {
  public:
    explicit Panel(Context& context) : context_(context) { context_.live.push_back(this); }
    ~Panel() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        refresh();
        live();
    }
    // A resize can arrive before the first tick, or between an edit and the next tick: sync first, then lay out.
    void resized() override {
        if (!refresh())
            layoutChildren();
    }

  protected:
    virtual void sync() {}
    virtual void layoutChildren() {}
    virtual void live() {}

    // Syncs and lays out when the song or the editor state changed since the last sync.
    bool refresh() {
        const auto docRevision = context_.doc().revision();
        if (synced_ && docRevision == docRevision_ && context_.revision == uiRevision_)
            return false;
        docRevision_ = docRevision;
        uiRevision_ = context_.revision;
        synced_ = true;
        sync();
        layoutChildren();
        redraw();
        return true;
    }

    float designWidth() const { return width() / context_.scale; }
    float designHeight() const { return height() / context_.scale; }
    void chrome(const Paint& p) const {
        p.rect(colour::panel, 0.5f, 0.5f, designWidth() - 1.0f, designHeight() - 1.0f, 12.0f);
        p.border(colour::edge, 0.0f, 0.0f, designWidth(), designHeight(), 12.0f);
    }

    Context& context_;

  private:
    std::uint64_t docRevision_ = 0;
    std::uint64_t uiRevision_ = 0;
    bool synced_ = false;
};

} // namespace lg::ui
