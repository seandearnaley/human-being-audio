// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/FrameClock.h"
#include "ui/PadArt.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace livecut::ui
{

// The pad: a sheet of card in a program's colour, with that program's art
// laid on it. The art is drawn afresh each frame, so it drifts at rest, glides
// after the pointer mark and answers every new block of cuts. It plays two
// parameters at once, with the mouse button as the third. Pressing sets Pad Touch and letting go clears it, and the X and Y
// gestures open and close together, so a host records one drag as one
// automation pass on each. Each axis has a tag in a corner of the art, naming
// it and giving its value, and a native slider along the art's edge, under a
// marker that follows the pointer mark, for precise keyboard and
// screen-reader editing.
class XYPad final : public juce::Component, private juce::Timer
{
public:
    XYPad (juce::RangedAudioParameter& x, juce::RangedAudioParameter& y, juce::RangedAudioParameter& touch);
    ~XYPad() override;

    // What left/right and up/down do, shown on the tags and the sliders.
    void setCaptions (juce::String x, juce::String y);

    // The program whose colour the sheet takes and whose art lies on it. A change crossfades.
    void setProgram (int program);

    // Fills the pointer mark, and quickens the art, while the pad is playing, pressed or held.
    void showEngaged (bool engaged);

    // A new block of cuts: a ring leaves the pointer mark and the art answers.
    void pulse();

    // The display refreshed at this time, in seconds. Moves the pad on if a
    // frame is due, 30 times a second at rest and 60 while anything on it is
    // moving, and says whether it did.
    bool displayRefreshed (double now);

    // Moves everything that moves on by this many seconds: the art, a change of program and the ring.
    void animate (double seconds);
    const ArtMotion& artMotion() const { return motion; }

    // How far the sheet has changed to the latest program: 0 just after the change, 1 once it's done.
    float programChange() const { return change; }

    // The surface that responds to the pointer, in local coordinates.
    juce::Rectangle<float> padBounds() const;
    juce::Slider& xControl() { return x.slider; }
    juce::Slider& yControl() { return y.slider; }

    // Lets go: ends the drag, the touch and both axis gestures.
    void endInteraction();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void visibilityChanged() override;
    void enablementChanged() override;

private:
    class AxisSlider final : public juce::Slider
    {
    public:
        AxisSlider (XYPad& owner, bool horizontal);
        void paint (juce::Graphics&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        XYPad& owner;
        bool horizontal;
    };

    // An axis's name and value, on a tab of the sheet that folds over a corner of the art.
    class Tag final : public juce::Component
    {
    public:
        Tag (XYPad& owner, bool horizontal);
        void paint (juce::Graphics&) override;
        void resized() override;
        int fittedWidth() const;
        static int widestReadout();

        juce::Label caption, readout;

    private:
        XYPad& owner;
        bool horizontal;
    };

    struct Axis
    {
        Axis (XYPad& owner, juce::RangedAudioParameter& parameter, bool horizontal);

        juce::RangedAudioParameter& parameter;
        juce::ParameterAttachment attachment;
        AxisSlider slider;
        Tag tag;
        float value = 0.0f; // normalised
        bool inGesture = false;
    };

    Axis& axis (bool horizontal) { return horizontal ? x : y; }
    juce::Colour sheetColour() const;
    juce::Point<float> mark() const;
    juce::Point<float> values() const { return { x.value, y.value }; }
    void layoutTags();

    void timerCallback() override;
    void movePointer (const juce::MouseEvent&, bool updateFine = true);
    bool axisKey (bool horizontal, const juce::KeyPress&);
    void beginGesture (Axis&);
    void endGesture (Axis&);
    void setAxis (Axis&, float normalised);
    void receive (bool horizontal, float value);
    void pressTouch();
    void releaseTouch();

    Axis x, y;
    juce::ParameterAttachment touchAttachment;
    juce::Point<float> dragOrigin, dragValues;
    ArtMotion motion;
    juce::Image canvas, canvasBefore; // the art as last drawn: the program showing and, mid-change, the one before
    int program = 0, programBefore = 0;
    float change = 1.0f;     // how far the sheet has changed from the program before: 1 is done
    float ring = 1.0f;       // how far the ring has travelled from the pointer mark: 1 is gone
    float liveliness = 0.0f; // eases to 1 while the pad plays, and sets the art's pace
    bool artStale = true;    // whether the art has moved since it was last drawn
    bool dragging = false, fineDragging = false, touching = false, engaged = false;

    FrameClock clock;
    juce::VBlankAttachment frames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};

} // namespace livecut::ui
