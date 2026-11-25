// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <functional>
#include <string>

namespace sb::ui {

class ParamFrame : public visage::Frame, public Live {
  public:
    ParamFrame(Context& ctx, ParamId id, unsigned accent);
    ~ParamFrame() override;

    ParamId id() const { return id_; }
    const ParamSpec& spec() const { return spec_; }
    float value() const { return context_.model.get(id_); }
    float normalised() const { return toNormalised(spec_, value()); }
    std::string text() const { return formatValue(spec_, value()); }

    void tick() override;
    void mouseEnter(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    bool mouseWheel(const visage::MouseEvent& e) override;
    void draw(visage::Canvas& canvas) final;

  protected:
    virtual void paint(visage::Canvas& canvas) = 0;
    bool active() const { return hovered_ || dragging_; }

    Context& context_;
    unsigned accent_;

  private:
    void commit(float real);
    void nudge(float delta);
    void showMenu();

    ParamId id_;
    const ParamSpec& spec_;
    float drawn_ = -1.0e30f;
    float dragPos_ = 0.0f;
    float lastY_ = 0.0f;
    bool hovered_ = false;
    bool dragging_ = false;
};

class Knob final : public ParamFrame {
  public:
    Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel = {}, bool hero = false);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    bool hero_;
};

// Sleek tactile filled-pill slider, choice chip, or compact toggle badge!
class Row final : public ParamFrame {
  public:
    enum class Style { fader, chip, badge };
    Row(Context& ctx, ParamId id, unsigned accent, std::string label = {}, Style style = Style::fader);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    Style style_;
};

class ActionButton final : public visage::Frame {
  public:
    ActionButton(Context& ctx, std::string label, std::function<void()> onClick = {});

    void setLabel(std::string next);
    void setAccent(unsigned argb);
    void setGradientAccent(unsigned fromArgb, unsigned toArgb);
    void setActive(bool on);
    void setOnRightClick(std::function<void()> cb) { onRightClick_ = std::move(cb); }
    void setOnDragOut(std::function<void()> cb) { onDragOut_ = std::move(cb); }

    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    void draw(visage::Canvas& canvas) override;

  private:
    Context& context_;
    std::string label_;
    unsigned accent_ = colour::text;
    unsigned gradEnd_ = 0u;
    bool active_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool draggedOut_ = false;
    visage::Point pressPos_{};
    std::function<void()> onClick_;
    std::function<void()> onRightClick_;
    std::function<void()> onDragOut_;
};

} // namespace sb::ui
