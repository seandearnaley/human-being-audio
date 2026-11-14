// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/XYPad.h"

#include "ui/Theme.h"

#include <cmath>

namespace livecut::ui
{

namespace
{
constexpr int frame = 8;         // the sheet showing around the art, left and top
constexpr int rail = 24;         // and right and bottom, where the axis markers run
constexpr float sheetRadius = 14.0f;
constexpr float artRadius = 7.0f;
constexpr int tagHeight = 24;
constexpr int tagPadding = 9;
constexpr float markerReach = 7.0f; // half a marker's width

constexpr float markRadius = 10.0f;
constexpr float pressedRing = 22.0f;
constexpr float cutRingTravel = 30.0f;

constexpr float kickSeconds = 0.6f;       // how long the art takes to settle after a new block
constexpr float changeSeconds = 0.18f;    // how long one program's sheet takes to become another's
constexpr float ringSeconds = 0.32f;      // how long the ring from the pointer mark lasts
constexpr float restingPace = 0.45f;      // the art's clock at rest, against 1 while the pad plays
constexpr double restingFrame = 1.0 / 30; // the pad is redrawn this often at rest,
constexpr double movingFrame = 1.0 / 60;  // and this often while it plays or something on it is changing
constexpr float sharpest = 2.0f;          // the art is drawn at up to this many pixels a point

juce::Font captionFont() { return uiFont (13.0f, weight::semibold); }
juce::Font readoutFont() { return numberFont (13.0f); }

// Quick at first, then gentle.
float easedOut (float progress) { return 1.0f - juce::square (1.0f - progress); }
} // namespace

XYPad::AxisSlider::AxisSlider (XYPad& pad, bool horizontalAxis)
    // A bar's range is its whole length, so the slider's ends are the art's edges whatever look and feel it's in.
    : juce::Slider (horizontalAxis ? LinearBar : LinearBarVertical, NoTextBox), owner (pad), horizontal (horizontalAxis)
{
    setRange (0.0, 1.0, 0.001);
}

void XYPad::AxisSlider::paint (juce::Graphics& g)
{
    // The pad draws the marker, which overhangs the ends; the slider only shows that it has the keyboard.
    if (hasKeyboardFocus (false))
    {
        g.setColour (palette::ink);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
    }
}

bool XYPad::AxisSlider::keyPressed (const juce::KeyPress& key)
{
    return owner.axisKey (horizontal, key);
}

XYPad::Tag::Tag (XYPad& pad, bool horizontalAxis) : owner (pad), horizontal (horizontalAxis)
{
    setInterceptsMouseClicks (false, false);
    caption.setFont (captionFont());
    readout.setFont (readoutFont());

    for (auto* label : { &caption, &readout })
    {
        label->setBorderSize ({});
        label->setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (label);
    }
}

int XYPad::Tag::widestReadout()
{
    return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (readoutFont(), "100%")));
}

int XYPad::Tag::fittedWidth() const
{
    // Room for the widest value, so the tag keeps its size while the value changes.
    const auto captionWidth = juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (captionFont(), caption.getText())));
    return 2 * tagPadding + captionWidth + 6 + widestReadout();
}

void XYPad::Tag::paint (juce::Graphics& g)
{
    // Square where it meets the sheet, rounded where it lies on the art.
    const auto bounds = getLocalBounds().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 8.0f, 8.0f,
                               horizontal, false, false, ! horizontal);
    g.setColour (owner.sheetColour());
    g.fillPath (shape);
}

void XYPad::Tag::resized()
{
    auto area = getLocalBounds().reduced (tagPadding, 0);
    caption.setBounds (area.removeFromLeft (area.getWidth() - widestReadout()));
    readout.setBounds (area);
}

XYPad::Axis::Axis (XYPad& owner, juce::RangedAudioParameter& p, bool horizontal)
    : parameter (p),
      attachment (p, [&owner, horizontal] (float denormalised) { owner.receive (horizontal, denormalised); }),
      slider (owner, horizontal),
      tag (owner, horizontal)
{
}

XYPad::XYPad (juce::RangedAudioParameter& xParameter, juce::RangedAudioParameter& yParameter,
              juce::RangedAudioParameter& touchParameter)
    : x (*this, xParameter, true),
      y (*this, yParameter, false),
      touchAttachment (touchParameter, [] (float) {}),
      frames (this, [this] (double now) { displayRefreshed (now); })
{
    setComponentID (touchParameter.getParameterID());
    setTitle ("Pad");
    setWantsKeyboardFocus (true);
    setHelpText ("Press to cut; let go to stop on the grid. Drag to change the sound side to side and up and down, "
                 "Shift-drag for fine changes. Arrow keys nudge 1%, Shift 0.1%, Ctrl 10%. Home resets.");

    for (auto* a : { &x, &y })
    {
        a->slider.setComponentID (a->parameter.getParameterID());
        a->slider.setTitle (a->parameter.getName (80));
        a->slider.setHelpText (getHelpText());
        a->slider.setDoubleClickReturnValue (true, a->parameter.getDefaultValue());
        a->slider.onDragStart = [this, a] { beginGesture (*a); };
        a->slider.onDragEnd = [this, a] { endGesture (*a); };
        a->slider.onValueChange = [this, a] { setAxis (*a, static_cast<float> (a->slider.getValue())); };
        addAndMakeVisible (a->tag);
        addAndMakeVisible (a->slider);
    }

    x.slider.setExplicitFocusOrder (1); // left/right before up/down, though the Y slider sits higher
    y.slider.setExplicitFocusOrder (2);
    x.attachment.sendInitialUpdate();
    y.attachment.sendInitialUpdate();
    motion.x = x.value;
    motion.y = y.value;
}

XYPad::~XYPad()
{
    endInteraction();
}

void XYPad::setCaptions (juce::String xCaption, juce::String yCaption)
{
    if (x.tag.caption.getText() == xCaption && y.tag.caption.getText() == yCaption)
        return;

    x.tag.caption.setText (xCaption, juce::dontSendNotification);
    y.tag.caption.setText (yCaption, juce::dontSendNotification);
    setDescription ("Left/right changes " + xCaption + "; up/down changes " + yCaption);
    layoutTags();
}

void XYPad::setProgram (int next)
{
    if (program == next)
        return;

    // A change that lands mid-change starts from whichever program showed more.
    programBefore = change >= 0.5f ? program : programBefore;
    program = next;
    change = 0.0f;
    artStale = true;
    repaint();
}

void XYPad::showEngaged (bool state)
{
    if (engaged == state)
        return;

    engaged = state;
    repaint();
}

void XYPad::pulse()
{
    motion.kick = 1.0f;
    ++motion.cuts;
    ring = 0.0f;
}

bool XYPad::displayRefreshed (double now)
{
    const auto moving = liveliness > 0.01f || change < 1.0f || ring < 1.0f;
    const auto elapsed = clock.tick (now, moving ? movingFrame : restingFrame);

    if (elapsed.has_value())
        animate (*elapsed);

    return elapsed.has_value();
}

void XYPad::animate (double seconds)
{
    const auto elapsed = static_cast<float> (seconds);
    const auto towards = [elapsed] (float from, float to, float rate) { return from + (to - from) * juce::jmin (1.0f, elapsed * rate); };

    change = juce::jmin (1.0f, change + elapsed / changeSeconds);
    ring = juce::jmin (1.0f, ring + elapsed / ringSeconds);
    liveliness = towards (liveliness, engaged ? 1.0f : 0.0f, 5.0f);
    motion.kick = juce::jmax (0.0f, motion.kick - elapsed / kickSeconds);
    motion.x = towards (motion.x, x.value, 14.0f);
    motion.y = towards (motion.y, y.value, 14.0f);

    const auto pace = restingPace + (1.0f - restingPace) * liveliness;
    motion.time += elapsed * pace;
    motion.spin += elapsed * pace * juce::square (1.0f - motion.kick);
    artStale = true;
    repaint();
}

juce::Colour XYPad::sheetColour() const
{
    return programColour (programBefore).interpolatedWith (programColour (program), easedOut (change));
}

juce::Rectangle<float> XYPad::padBounds() const
{
    return getLocalBounds().withTrimmedLeft (frame).withTrimmedTop (frame).withTrimmedRight (rail).withTrimmedBottom (rail).toFloat();
}

juce::Point<float> XYPad::mark() const
{
    const auto area = padBounds();
    return { area.getX() + x.value * area.getWidth(), area.getBottom() - y.value * area.getHeight() };
}

void XYPad::layoutTags()
{
    const auto area = padBounds().toNearestInt();
    y.tag.setBounds (area.getX(), area.getY(), y.tag.fittedWidth(), tagHeight);
    const auto width = x.tag.fittedWidth();
    x.tag.setBounds (area.getRight() - width, area.getBottom() - tagHeight, width, tagHeight);
}

void XYPad::resized()
{
    const auto area = padBounds().toNearestInt();

    // JUCE lays a bar out inside a one-pixel border, so each slider is that much longer than the art at both ends.
    x.slider.setBounds (area.getX() - 1, area.getBottom(), area.getWidth() + 2, rail);
    y.slider.setBounds (area.getRight(), area.getY() - 1, rail, area.getHeight() + 2);
    layoutTags();
    endInteraction();
    artStale = true;
}

void XYPad::paint (juce::Graphics& g)
{
    // The sheet: square along the top, where the program tabs join it.
    const auto bounds = getLocalBounds().toFloat();
    juce::Path sheet;
    sheet.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                               sheetRadius, sheetRadius, false, false, true, true);
    g.setColour (sheetColour());
    g.fillPath (sheet);

    // The art is drawn in software and copied across, which costs a fraction of
    // what the platform's renderers take to draw it themselves.
    const auto area = padBounds();
    const auto density = juce::jmin (sharpest, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto width = juce::roundToInt (area.getWidth() * density), height = juce::roundToInt (area.getHeight() * density);

    if (width <= 0 || height <= 0)
        return;

    const auto draw = [&] (juce::Image& canvas, int scene)
    {
        if (canvas.getWidth() != width || canvas.getHeight() != height)
            canvas = juce::Image (juce::Image::RGB, width, height, false, juce::SoftwareImageType());
        else if (! artStale)
            return;

        juce::Graphics art (canvas);
        art.addTransform (juce::AffineTransform::scale (density));
        drawProgramArt (art, scene, area.withZeroOrigin(), motion);
    };

    juce::Path shape;
    shape.addRoundedRectangle (area, artRadius);
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);

    if (change < 1.0f)
    {
        draw (canvasBefore, programBefore);
        g.drawImage (canvasBefore, area);
    }

    draw (canvas, program);
    g.setOpacity (easedOut (change));
    g.drawImage (canvas, area);
    artStale = false;
}

void XYPad::paintOverChildren (juce::Graphics& g)
{
    const auto area = padBounds();
    const auto at = mark();

    {
        const juce::Graphics::ScopedSaveState state (g);
        juce::Path shape;
        shape.addRoundedRectangle (area, artRadius);
        g.reduceClipRegion (shape);

        g.setColour (palette::paper.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (area.getX(), at.y - 0.5f, area.getWidth(), 1.0f));
        g.fillRect (juce::Rectangle<float> (at.x - 0.5f, area.getY(), 1.0f, area.getHeight()));

        if (ring < 1.0f)
        {
            // A new block: a ring leaves the mark and fades.
            g.setColour (palette::paper.withAlpha (1.0f - ring));
            g.drawEllipse (juce::Rectangle<float>().withCentre (at).expanded (markRadius + easedOut (ring) * cutRingTravel), 2.0f);
        }

        if (dragging)
        {
            g.setColour (palette::ink.withAlpha (0.25f));
            g.drawEllipse (juce::Rectangle<float>().withCentre (at).expanded (pressedRing), 4.0f);
            g.setColour (palette::paper);
            g.drawEllipse (juce::Rectangle<float>().withCentre (at).expanded (pressedRing), 2.0f);
        }

        // The pointer mark: a paper dot that fills with ink while the pad plays.
        const auto dot = juce::Rectangle<float>().withCentre (at).expanded (markRadius);
        juce::Path disc;
        disc.addEllipse (dot);
        juce::DropShadow (palette::ink.withAlpha (0.45f), 8, { 0, 3 }).drawForPath (g, disc);
        g.setColour (engaged ? palette::ink : palette::paper);
        g.fillPath (disc);
        g.setColour (engaged ? palette::paper : palette::ink);
        g.drawEllipse (dot.reduced (1.25f), 2.5f);
    }

    // A marker on each rail, pointing in at the mark from the art's edge.
    juce::Path markers;
    markers.addTriangle (at.x, area.getBottom() + 4.0f, at.x - markerReach, area.getBottom() + 14.0f, at.x + markerReach, area.getBottom() + 14.0f);
    markers.addTriangle (area.getRight() + 4.0f, at.y, area.getRight() + 14.0f, at.y - markerReach, area.getRight() + 14.0f, at.y + markerReach);
    g.setColour (palette::ink);
    g.fillPath (markers);
    g.strokePath (markers, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved)); // rounds their corners

    if (hasKeyboardFocus (false))
        g.drawRoundedRectangle (area.expanded (3.0f), artRadius + 3.0f, 2.0f);
}

void XYPad::receive (bool horizontal, float value)
{
    auto& a = axis (horizontal);
    a.value = a.parameter.convertTo0to1 (value);
    a.slider.setValue (a.value, juce::dontSendNotification);
    const auto unit = a.parameter.getLabel();
    a.tag.readout.setText (a.parameter.getText (a.value, 0) + (unit.isEmpty() ? "" : " " + unit), juce::dontSendNotification);
    repaint();
}

void XYPad::beginGesture (Axis& a)
{
    if (a.inGesture)
        return;

    a.attachment.beginGesture();
    a.inGesture = true;
}

void XYPad::endGesture (Axis& a)
{
    if (! a.inGesture)
        return;

    a.attachment.endGesture();
    a.inGesture = false;
}

void XYPad::setAxis (Axis& a, float normalised)
{
    const auto value = a.parameter.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised));

    if (a.inGesture)
        a.attachment.setValueAsPartOfGesture (value);
    else
        a.attachment.setValueAsCompleteGesture (value);
}

void XYPad::pressTouch()
{
    if (touching)
        return;

    touching = true;
    touchAttachment.beginGesture();
    touchAttachment.setValueAsPartOfGesture (1.0f);
}

void XYPad::releaseTouch()
{
    if (! touching)
        return;

    touching = false;
    touchAttachment.setValueAsPartOfGesture (0.0f);
    touchAttachment.endGesture();
}

void XYPad::mouseDown (const juce::MouseEvent& event)
{
    if (! isEnabled() || ! event.mods.isLeftButtonDown() || ! padBounds().contains (event.position))
        return;

    endInteraction();
    grabKeyboardFocus();
    dragging = true;
    dragOrigin = event.position;
    dragValues = values();
    fineDragging = event.mods.isShiftDown();
    beginGesture (x);
    beginGesture (y);
    movePointer (event);
    pressTouch();
    startTimerHz (30);
    repaint();
}

void XYPad::movePointer (const juce::MouseEvent& event, bool updateFine)
{
    const auto area = padBounds();

    if (area.isEmpty())
        return;

    if (updateFine && event.mods.isShiftDown() && ! fineDragging)
    {
        dragOrigin = event.position;
        dragValues = values();
    }

    if (updateFine)
        fineDragging = event.mods.isShiftDown();

    auto next = juce::Point<float> { (event.position.x - area.getX()) / area.getWidth(),
                                     (area.getBottom() - event.position.y) / area.getHeight() };

    if (fineDragging)
        next = dragValues + juce::Point<float> { (event.position.x - dragOrigin.x) / area.getWidth(),
                                                 (dragOrigin.y - event.position.y) / area.getHeight() } * 0.1f;

    setAxis (x, next.x);
    setAxis (y, next.y);
}

void XYPad::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging)
        movePointer (event);
}

void XYPad::mouseUp (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    movePointer (event, false);
    endInteraction();
}

void XYPad::endInteraction()
{
    stopTimer();
    const auto wasDragging = dragging;
    dragging = false;
    releaseTouch();
    endGesture (x);
    endGesture (y);

    if (wasDragging)
        repaint();
}

void XYPad::timerCallback()
{
    if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown() || ! isShowing() || ! isEnabled())
        endInteraction();
}

bool XYPad::axisKey (bool horizontal, const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    auto& a = axis (horizontal);
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::homeKey)
    {
        endInteraction();
        setAxis (a, a.parameter.getDefaultValue());
        return true;
    }

    const auto direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1.0f
                         : code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey ? -1.0f : 0.0f;

    if (direction == 0.0f)
        return false;

    endInteraction();
    const auto step = key.getModifiers().isShiftDown() ? 0.001f : key.getModifiers().isCtrlDown() ? 0.1f : 0.01f;
    setAxis (a, a.value + direction * step);
    return true;
}

bool XYPad::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::homeKey)
    {
        axisKey (true, key);
        axisKey (false, key);
        return true;
    }

    const auto horizontal = key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey;
    return axisKey (horizontal, key);
}

void XYPad::focusGained (FocusChangeType)
{
    repaint();
}

void XYPad::focusLost (FocusChangeType)
{
    endInteraction();
    repaint();
}

void XYPad::visibilityChanged()
{
    if (! isVisible())
        endInteraction();
}

void XYPad::enablementChanged()
{
    if (! isEnabled())
        endInteraction();
}

} // namespace livecut::ui
