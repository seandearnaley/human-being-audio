// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <functional>
#include <string>
#include <vector>

namespace lc::ui {

class ActionButton final : public visage::Frame {
  public:
    ActionButton(Context& context, std::string label, std::function<void()> onClick);

    void setLabel(std::string label);
    void setAccent(unsigned accent);
    void setActive(bool active);
    void setEnabledState(bool enabled);
    void setOnDrag(std::function<void()> onDrag) { onDrag_ = std::move(onDrag); }
    void setOnRightClick(std::function<void()> onRightClick) { onRightClick_ = std::move(onRightClick); }

    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;

  private:
    Context& context_;
    std::string label_;
    std::function<void()> onClick_;
    std::function<void()> onDrag_;
    std::function<void()> onRightClick_;
    unsigned accent_ = colour::text;
    bool active_ = false;
    bool enabled_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool dragged_ = false;
    float downX_ = 0.0f;
    float downY_ = 0.0f;
};

class TogglePill final : public visage::Frame, public Live {
  public:
    TogglePill(Context& context, std::string paramId, std::string label, unsigned accent = colour::accent);
    ~TogglePill() override;

    void setAccent(unsigned accent);
    void setDimmed(bool dimmed);
    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;

  private:
    Context& context_;
    std::string id_;
    std::string label_;
    unsigned accent_;
    bool dimmed_ = false;
    bool hovered_ = false;
    bool drawnOn_ = false;
    bool initialised_ = false;
};

class ParamSlider final : public visage::Frame, public Live {
  public:
    ParamSlider(Context& context, std::string paramId, std::string label, unsigned accent = colour::accent,
                bool compact = false);
    ~ParamSlider() override;

    void setAccent(unsigned accent);
    void setDimmed(bool dimmed);
    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    bool mouseWheel(const visage::MouseEvent& e) override;

  private:
    void setFromX(float x);

    Context& context_;
    std::string id_;
    std::string label_;
    unsigned accent_;
    bool compact_ = false;
    bool dimmed_ = false;
    bool hovered_ = false;
    bool dragging_ = false;
    float dragAnchorVal_ = 0.0f;
    float dragAnchorX_ = 0.0f;
    float drawn_ = -1.0f;
};

class RangeSlider final : public visage::Frame, public Live {
  public:
    RangeSlider(Context& context, std::string lowId, std::string highId, std::string label,
                unsigned accent = colour::accent);
    ~RangeSlider() override;

    void setDimmed(bool dimmed);
    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;

  private:
    enum class DragTarget { none, low, high, span };

    Context& context_;
    std::string lowId_;
    std::string highId_;
    std::string label_;
    unsigned accent_;
    bool dimmed_ = false;
    bool hovered_ = false;
    DragTarget dragTarget_ = DragTarget::none;
    float anchorX_ = 0.0f;
    float anchorLow_ = 0.0f;
    float anchorHigh_ = 0.0f;
    float drawnLow_ = -1.0f;
    float drawnHigh_ = -1.0f;
};

class ChoiceRow final : public visage::Frame, public Live {
  public:
    ChoiceRow(Context& context, std::string paramId, std::string label, unsigned accent = colour::accent);
    ~ChoiceRow() override;

    void setDimmed(bool dimmed);
    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    bool mouseWheel(const visage::MouseEvent& e) override;

  private:
    void showMenu();

    Context& context_;
    std::string id_;
    std::string label_;
    unsigned accent_;
    bool dimmed_ = false;
    bool hovered_ = false;
    float drawn_ = -1.0f;
};

class SegmentedBar final : public visage::Frame {
  public:
    SegmentedBar(Context& context, std::vector<std::string> items, std::function<void(int)> onSelect,
                 unsigned accent = colour::accent);

    void setSelected(int index);
    int selected() const { return selected_; }
    void setAccent(unsigned accent);

    void draw(visage::Canvas& canvas) override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;

  private:
    int indexAt(float x) const;

    Context& context_;
    std::vector<std::string> items_;
    std::function<void(int)> onSelect_;
    unsigned accent_;
    int selected_ = 0;
    int hovered_ = -1;
};

// Parameter-bound segmented selector (e.g. Pad|Classic, Beat|Bar, Procedure, Reader Family).
class ParamSegmented final : public visage::Frame, public Live {
  public:
    ParamSegmented(Context& context, std::string paramId, std::vector<std::string> labels,
                   unsigned accent = colour::accent);
    ~ParamSegmented() override;

    void setAccent(unsigned accent);
    void tick() override;
    void resized() override;

  private:
    Context& context_;
    std::string id_;
    int count_ = 1;
    int drawn_ = -1;
    SegmentedBar bar_;
};

} // namespace lc::ui
