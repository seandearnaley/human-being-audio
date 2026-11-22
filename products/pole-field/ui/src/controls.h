#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <functional>
#include <span>
#include <string>

namespace pf::ui {

class ParamFrame : public visage::Frame, public Live {
public:
    ParamFrame(Context& context, ParamId id, unsigned accent);
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
    void nudge(float normalisedDelta);
    void showMenu();

    ParamId id_;
    const ParamSpec& spec_;
    float drawn_ = -1.0e30f;
    float dragPosition_ = 0.0f;
    float lastY_ = 0.0f;
    bool hovered_ = false;
    bool dragging_ = false;
};

class Knob final : public ParamFrame {
public:
    Knob(Context& context, ParamId id, unsigned accent) : ParamFrame(context, id, accent) {}

private:
    void paint(visage::Canvas& canvas) override;
};

class Row final : public ParamFrame {
public:
    Row(Context& context, ParamId id, unsigned accent, std::string label = {}, bool bar = false);

private:
    void paint(visage::Canvas& canvas) override;

    std::string label_;
    bool bar_;
};

class Chip final : public ParamFrame {
public:
    Chip(Context& context, ParamId id, unsigned accent) : ParamFrame(context, id, accent) {}
    void setAccent(unsigned accent);

private:
    void paint(visage::Canvas& canvas) override;
};

class Button final : public visage::Frame {
public:
    Button(Context& context, std::string label, std::function<void()> onClick);

    void setLabel(std::string label);
    void setAccent(unsigned accent);

    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;

private:
    Context& context_;
    std::string label_;
    std::function<void()> onClick_;
    unsigned accent_ = colour::text;
    bool hovered_ = false;
    bool pressed_ = false;
};

void drawChip(visage::Canvas& canvas, const Context& context, const std::string& text, float width, float height,
              unsigned textArgb);

void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y, float width,
               float height, unsigned colourArgb = colour::textDim,
               visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
               Weight weight = Weight::medium);

void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
               float width, float height, unsigned colourArgb, float low = -1.0f, float high = 1.0f,
               float fillAlpha = 0.0f, float baseline = 0.0f, float designThickness = metric::stroke);

}
