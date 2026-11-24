// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <functional>
#include <string>

namespace tg::ui {

class ParamFrame : public visage::Frame, public Live {
  public:
    ParamFrame(Context& ctx, ParamId id, unsigned accent);
    ~ParamFrame() override;

    ParamId id() const { return id_; }
    const ParamSpec& spec() const { return spec_; }
    float value() const { return context_.model.get(id_); }
    float normalised() const { return toNormalised(spec_, value()); }
    std::string text() const { return formatValue(spec_, value()); }
    void setAccent(unsigned argb) {
        if (accent_ != argb) {
            accent_ = argb;
            redraw();
        }
    }

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
    using ModProvider = std::function<float()>;
    Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel = {});
    void setModProvider(ModProvider provider) { modProvider_ = std::move(provider); }
    void tick() override;

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    ModProvider modProvider_;
    float drawnMod_ = -1.0f;
};

class Row final : public ParamFrame {
  public:
    Row(Context& ctx, ParamId id, unsigned accent, std::string label = {}, bool bar = true);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    bool bar_;
};

class Chip final : public ParamFrame {
  public:
    Chip(Context& ctx, ParamId id, unsigned accent, std::string prefix = {});

  private:
    void paint(visage::Canvas& canvas) override;
    std::string prefix_;
};

class ActionButton final : public visage::Frame {
  public:
    ActionButton(Context& ctx, std::string label, std::function<void()> onClick = {});

    void setLabel(std::string next);
    void setAccent(unsigned argb);
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
    bool active_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool draggedOut_ = false;
    visage::Point pressPos_{};
    std::function<void()> onClick_;
    std::function<void()> onRightClick_;
    std::function<void()> onDragOut_;
};

} // namespace tg::ui
