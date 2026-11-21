// The controls every panel is assembled from. Each one edits exactly one
// parameter through the model and repaints only when that value changes.
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <functional>
#include <span>
#include <string>

namespace tf::ui {

// Shared behaviour: vertical drag (shift for fine), double-click to reset, wheel,
// click to toggle, click for a menu of choices.
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

// A macro: an arc with the value inside and the name underneath.
class Knob final : public ParamFrame {
public:
    // What the knob shows: the text inside the arc, and where along the arc (0..1) the fill ends.
    struct Face {
        std::string text;
        float position = 0.0f;
        bool operator==(const Face&) const = default;
    };
    using FaceProvider = std::function<Face(const Knob&)>;

    Knob(Context& context, ParamId id, unsigned accent) : ParamFrame(context, id, accent) {}

    // The default face: the value without its unit, at the value's position.
    Face valueFace() const;

    // Replaces the default face. The provider may read other parameters; the
    // knob repaints whenever the face it returns changes.
    void setFace(FaceProvider provider);

    void tick() override;

private:
    Face face() const { return provider_ ? provider_(*this) : valueFace(); }
    void paint(visage::Canvas& canvas) override;

    FaceProvider provider_;
    Face drawnFace_;
};

// "Label ......... value". Toggles draw a switch, choices a chevron. With `bar`
// set, a thin fill under the row shows the position: from the left, or from the
// centre for a value that runs below zero.
class Row final : public ParamFrame {
public:
    Row(Context& context, ParamId id, unsigned accent, std::string label = {}, bool bar = false);

private:
    void paint(visage::Canvas& canvas) override;

    std::string label_;
    bool bar_;
};

// A value on its own in a small pill, for tight spots such as wire labels.
class Chip final : public ParamFrame {
public:
    Chip(Context& context, ParamId id, unsigned accent) : ParamFrame(context, id, accent) {}

    // Changes the text colour, for a chip whose meaning changes (a lane turned off).
    void setAccent(unsigned accent);

private:
    void paint(visage::Canvas& canvas) override;
};

// A text button.
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

// The pill a Chip draws, for controls that look like one but behave differently.
void drawChip(visage::Canvas& canvas, const Context& context, const std::string& text, float width, float height,
              unsigned textArgb);

// Text helpers so panels do not repeat font and justification boilerplate.
void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y, float width,
               float height, unsigned colourArgb = colour::textDim,
               visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
               Weight weight = Weight::medium);

// One look for every curve in the interface: scope traces, spectra, node
// shapes and lanes. `values` run left to right; `low`..`high` map to the bottom
// and top of the rectangle. With `fillAlpha` above zero the area down to
// `baseline` (in value units) is shaded.
void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
               float width, float height, unsigned colourArgb, float low = -1.0f, float high = 1.0f,
               float fillAlpha = 0.0f, float baseline = 0.0f, float designThickness = metric::stroke);

}
