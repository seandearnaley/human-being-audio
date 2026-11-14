// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/LivecutLookAndFeel.h"

#include <cmath>

namespace livecut::ui
{

namespace
{
constexpr float thumbDiameter = 14.0f;
constexpr float trackHeight = 4.0f;
constexpr float tooltipWidth = 300.0f;
constexpr int tooltipPadding = 10;

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float radius)
{
    g.setColour (palette::ink);
    g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 1.5f);
}

juce::TextLayout tooltipLayout (const juce::String& text)
{
    juce::AttributedString string;
    string.setJustification (juce::Justification::centredLeft);
    string.append (text, uiFont (13.0f), palette::paper);
    juce::TextLayout layout;
    layout.createLayout (string, tooltipWidth);
    return layout;
}
} // namespace

LivecutLookAndFeel::LivecutLookAndFeel()
{
    using namespace palette;

    setColour (juce::ResizableWindow::backgroundColourId, paper);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::ComboBox::backgroundColourId, paper);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, rule);
    setColour (juce::ComboBox::arrowColourId, ink);
    setColour (juce::PopupMenu::backgroundColourId, paper);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour (juce::PopupMenu::highlightedTextColourId, paper);
    setColour (juce::ToggleButton::textColourId, ink);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, paper);
}

void LivecutLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::TwoValueHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centreY = bounds.getCentreY();
    const auto segment = [&] (float from, float to)
    {
        return juce::Rectangle<float> (from, centreY - trackHeight * 0.5f, to - from, trackHeight);
    };

    g.setColour (palette::rule);
    g.fillRoundedRectangle (segment (bounds.getX(), bounds.getRight()), trackHeight * 0.5f);

    const auto isRange = style == juce::Slider::TwoValueHorizontal;
    g.setColour (palette::ink);
    g.fillRoundedRectangle (segment (isRange ? minSliderPos : bounds.getX(), isRange ? maxSliderPos : sliderPos), trackHeight * 0.5f);

    const auto thumb = [&] (float position)
    {
        g.fillEllipse (juce::Rectangle<float> (thumbDiameter, thumbDiameter).withCentre ({ position, centreY }));
    };

    if (isRange)
    {
        thumb (minSliderPos);
        thumb (maxSliderPos);
    }
    else
    {
        thumb (sliderPos);
    }

    if (slider.hasKeyboardFocus (true))
        drawFocusRing (g, slider.getLocalBounds().toFloat(), 4.0f);
}

int LivecutLookAndFeel::getSliderThumbRadius (juce::Slider&)
{
    return juce::roundToInt (thumbDiameter * 0.5f);
}

void LivecutLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // A switch: its knob slides right and the track fills with ink when on.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto track = juce::Rectangle<float> (30.0f, 18.0f).withCentre ({ bounds.getX() + 15.0f, bounds.getCentreY() });
    const auto on = button.getToggleState();

    g.setColour (on ? palette::ink : palette::rule.withMultipliedAlpha (highlighted ? 1.6f : 1.0f));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (palette::paper);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ on ? track.getRight() - 9.0f : track.getX() + 9.0f, track.getCentreY() }));

    g.setColour (palette::ink);
    g.setFont (uiFont (14.0f, weight::medium));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (track.getWidth() + 9.0f), juce::Justification::centredLeft);

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, bounds, 4.0f);
}

void LivecutLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    // A pill that fills with ink when chosen. A button joined to its
    // neighbours is a segment of their shared pill, which the group outlines.
    const auto segment = button.getConnectedEdgeFlags() != 0;
    const auto bounds = button.getLocalBounds().toFloat().reduced (segment ? 3.0f : 0.75f);
    const auto radius = bounds.getHeight() * 0.5f;

    if (button.getToggleState())
    {
        g.setColour (palette::ink);
        g.fillRoundedRectangle (bounds, radius);
    }
    else
    {
        if (highlighted || down)
        {
            g.setColour (palette::rule.withMultipliedAlpha (down ? 1.0f : 0.5f));
            g.fillRoundedRectangle (bounds, radius);
        }

        if (! segment)
        {
            g.setColour (palette::rule);
            g.drawRoundedRectangle (bounds, radius, 1.5f);
        }
    }

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, button.getLocalBounds().toFloat(), radius);
}

void LivecutLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? palette::paper : palette::ink);
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

juce::Font LivecutLookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    return uiFont (14.0f, button.getToggleState() ? weight::semibold : weight::medium);
}

void LivecutLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.75f);
    const auto radius = bounds.getHeight() * 0.5f;
    g.setColour (palette::rule);
    g.drawRoundedRectangle (bounds, radius, 1.5f);

    juce::Path chevron;
    const auto centre = juce::Point<float> (bounds.getRight() - 14.0f, bounds.getCentreY());
    chevron.startNewSubPath (centre.translated (-4.0f, -2.0f));
    chevron.lineTo (centre.translated (0.0f, 2.0f));
    chevron.lineTo (centre.translated (4.0f, -2.0f));
    g.setColour (palette::ink);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (box.hasKeyboardFocus (true))
        drawFocusRing (g, bounds, radius);
}

juce::Font LivecutLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (14.0f);
}

void LivecutLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 0, box.getWidth() - 34, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font LivecutLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

juce::Font LivecutLookAndFeel::getLabelFont (juce::Label& label)
{
    // A label that asked for one of the skin's fonts keeps it; any other is plain text.
    return isSkinFont (label.getFont()) ? label.getFont() : uiFont (14.0f);
}

void LivecutLookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool mouseOver, bool dragging)
{
    // Two short strokes across the corner, quiet until the pointer finds them.
    const auto w = static_cast<float> (width), h = static_cast<float> (height);
    g.setColour (mouseOver || dragging ? palette::inkMuted : palette::rule);

    for (const auto inset : { 0.45f, 0.7f })
        g.drawLine (w * inset, h - 3.0f, w - 3.0f, h * inset, 1.5f);
}

juce::Rectangle<int> LivecutLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPosition, juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (text);
    const auto width = juce::roundToInt (std::ceil (layout.getWidth())) + 2 * tooltipPadding;
    const auto height = juce::roundToInt (std::ceil (layout.getHeight())) + 2 * tooltipPadding;

    return juce::Rectangle<int> (screenPosition.x > parentArea.getCentreX() ? screenPosition.x - (width + 12) : screenPosition.x + 24,
                                 screenPosition.y > parentArea.getCentreY() ? screenPosition.y - (height + 6) : screenPosition.y + 6,
                                 width, height)
        .constrainedWithin (parentArea);
}

void LivecutLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (palette::ink);
    g.fillRoundedRectangle (bounds, 8.0f);
    tooltipLayout (text).draw (g, bounds.reduced (static_cast<float> (tooltipPadding)));
}

} // namespace livecut::ui
