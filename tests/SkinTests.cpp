// SPDX-License-Identifier: GPL-3.0-or-later

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ProcessorTestSupport.h"
#include "ui/AboutSheet.h"
#include "ui/LivecutLookAndFeel.h"
#include "ui/PadArt.h"
#include "ui/PhraseRuler.h"
#include "ui/Theme.h"
#include "ui/XYPad.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <functional>
#include <set>

using Catch::Matchers::WithinAbs;

namespace
{
namespace ui = livecut::ui;

// WCAG 2 relative luminance and contrast ratio.
double luminance (juce::Colour colour)
{
    const auto linear = [] (float channel)
    {
        const auto c = static_cast<double> (channel);
        return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear (colour.getFloatRed()) + 0.7152 * linear (colour.getFloatGreen()) + 0.0722 * linear (colour.getFloatBlue());
}

double contrast (juce::Colour a, juce::Colour b)
{
    const auto lighter = std::max (luminance (a), luminance (b)), darker = std::min (luminance (a), luminance (b));
    return (lighter + 0.05) / (darker + 0.05);
}

// An image's colour, averaged over a grid of samples.
juce::Colour average (const juce::Image& image)
{
    double red = 0.0, green = 0.0, blue = 0.0;
    constexpr int columns = 32, rows = 18;

    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
        {
            const auto pixel = image.getPixelAt ((2 * column + 1) * image.getWidth() / (2 * columns), (2 * row + 1) * image.getHeight() / (2 * rows));
            red += pixel.getFloatRed();
            green += pixel.getFloatGreen();
            blue += pixel.getFloatBlue();
        }

    constexpr auto samples = static_cast<double> (columns * rows);
    return juce::Colour::fromFloatRGBA (static_cast<float> (red / samples), static_cast<float> (green / samples), static_cast<float> (blue / samples), 1.0f);
}

// How far apart two hues are, from 0 to 0.5 of the way round the wheel.
float hueDistance (juce::Colour a, juce::Colour b)
{
    const auto distance = std::abs (a.getHue() - b.getHue());
    return std::min (distance, 1.0f - distance);
}

bool near (juce::Colour a, juce::Colour b)
{
    return std::abs (a.getRed() - b.getRed()) <= 1 && std::abs (a.getGreen() - b.getGreen()) <= 1
        && std::abs (a.getBlue() - b.getBlue()) <= 1 && std::abs (a.getAlpha() - b.getAlpha()) <= 1;
}

juce::Image snapshot (juce::Component& component)
{
    return component.createComponentSnapshot (component.getLocalBounds(), true, 1.0f);
}

void set (LivecutAudioProcessor& processor, const char* id, float value)
{
    auto& parameter = *processor.getParameterState().getParameter (id);
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
}
} // namespace

TEST_CASE ("The skin's typeface is embedded, in every weight it uses", "[skin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CHECK (ui::uiFont (14.0f).getTypefaceName() == "Bricolage Grotesque");
    CHECK (ui::isSkinFont (ui::numberFont (14.0f)));
    CHECK (ui::isSkinFont (ui::displayFont (24.0f)));
    CHECK_FALSE (ui::isSkinFont (juce::Font (juce::FontOptions (14.0f))));

    // It is one variable font, so each heavier weight sets the same word wider.
    const auto width = [] (const juce::Font& font) { return juce::GlyphArrangement::getStringWidth (font, "LIVECUT Suction"); };
    CHECK (width (ui::uiFont (14.0f, ui::weight::medium)) > width (ui::uiFont (14.0f, ui::weight::regular)));
    CHECK (width (ui::uiFont (14.0f, ui::weight::semibold)) > width (ui::uiFont (14.0f, ui::weight::medium)));
    CHECK (width (ui::uiFont (14.0f, ui::weight::bold)) > width (ui::uiFont (14.0f, ui::weight::semibold)));

    // The wordmark's face is heavier still, and condensed.
    CHECK (width (ui::displayFont (14.0f)) < width (ui::uiFont (14.0f, ui::weight::bold)));
}

TEST_CASE ("An open editor keeps the typeface loaded, so its fonts share one", "[skin]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    {
        const ui::LivecutLookAndFeel lookAndFeel;
        const auto held = ui::uiFont (14.0f).getTypefacePtr();
        CHECK (ui::uiFont (20.0f).getTypefacePtr() == held);
        CHECK (ui::numberFont (14.0f).getTypefacePtr() == held);
        CHECK (ui::uiFont (14.0f, ui::weight::bold).getTypefacePtr() != held);
    }

    const auto hold = ui::keepTypeface();
    CHECK (ui::displayFont (24.0f).getTypefacePtr() == ui::displayFont (48.0f).getTypefacePtr());
}

// A program's art, drawn 404 by 248: half the size the pad shows it.
juce::Image art (int program, const ui::ArtMotion& motion)
{
    juce::Image image (juce::Image::ARGB, 404, 248, true);
    juce::Graphics g (image);
    ui::drawProgramArt (g, program, image.getBounds().toFloat(), motion);
    return image;
}

// How many of a grid of sample points differ between two pictures of the same size.
int difference (const juce::Image& a, const juce::Image& b)
{
    int differing = 0;

    for (int y = 2; y < a.getHeight(); y += 4)
        for (int x = 2; x < a.getWidth(); x += 4)
            differing += near (a.getPixelAt (x, y), b.getPixelAt (x, y)) ? 0 : 1;

    return differing;
}

TEST_CASE ("Each program has its own colour, which ink reads on, and art in that colour", "[skin]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::set<juce::uint32> colours;

    for (int program = 0; program < 8; ++program)
    {
        INFO ("program " << program);
        const auto colour = ui::programColour (program);
        colours.insert (colour.getARGB());
        CHECK (colour.isOpaque());
        CHECK (contrast (colour, ui::palette::ink) >= 4.5); // the tab's name and the axis tags are ink on it

        const auto picture = art (program, {});
        CHECK (picture.getPixelAt (2, 2).isOpaque()); // it fills its area, corner to corner
        CHECK (picture.getPixelAt (401, 245).isOpaque());
        CHECK (hueDistance (average (picture), colour) < 0.06f);
    }

    CHECK (colours.size() == 8);
    CHECK (ui::programColour (0) == ui::palette::accent);
}

TEST_CASE ("Every program's art moves with time, with both axes and at a cut", "[skin][art]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (int program = 0; program < 8; ++program)
    {
        INFO ("program " << program);
        const ui::ArtMotion rest;
        const auto still = art (program, rest);
        CHECK (difference (still, art (program, rest)) == 0); // the same motion draws the same picture

        auto later = rest;
        later.time = later.spin = 1.5f;
        CHECK (difference (still, art (program, later)) > 0);

        auto across = rest, up = rest;
        across.x = 0.95f;
        up.y = 0.95f;
        CHECK (difference (still, art (program, across)) > 20);
        CHECK (difference (still, art (program, up)) > 20);

        auto cut = rest; // halfway through settling after its first cut
        cut.kick = 0.5f;
        cut.cuts = 1;
        CHECK (difference (still, art (program, cut)) > 0);
    }
}

TEST_CASE ("The pad's art idles at rest, quickens while it plays, and settles after a cut", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    auto& x = *processor.getParameterState().getParameter (livecut::ids::padX);
    ui::XYPad pad (x, *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);

    pad.animate (1.0);
    const auto resting = pad.artMotion().time;
    CHECK (resting > 0.0f);

    // Playing, the art's clock runs faster: more than twice as far in the same second.
    pad.showEngaged (true);
    for (int frame = 0; frame < 60; ++frame)
        pad.animate (1.0 / 60);
    const auto warming = pad.artMotion().time;
    for (int frame = 0; frame < 60; ++frame)
        pad.animate (1.0 / 60);
    CHECK (pad.artMotion().time - warming > 2.0f * resting);

    // A cut kicks the art, which settles in well under a second; the brake clock stands still at the kick.
    const auto cutsBefore = pad.artMotion().cuts;
    pad.pulse();
    CHECK (pad.artMotion().cuts == cutsBefore + 1);
    CHECK_THAT (pad.artMotion().kick, WithinAbs (1.0, 0.0001));
    const auto spin = pad.artMotion().spin;
    pad.animate (1.0 / 600);
    CHECK_THAT (pad.artMotion().spin, WithinAbs (spin, 0.0001));
    for (int frame = 0; frame < 45; ++frame)
        pad.animate (1.0 / 60);
    CHECK_THAT (pad.artMotion().kick, WithinAbs (0.0, 0.0001));
    CHECK (pad.artMotion().spin > spin);

    // The art glides after the pointer mark rather than jumping to it.
    x.setValueNotifyingHost (1.0f);
    const auto before = pad.artMotion().x;
    pad.animate (1.0 / 60);
    CHECK (pad.artMotion().x > before);
    CHECK (pad.artMotion().x < 1.0f);
    for (int frame = 0; frame < 60; ++frame)
        pad.animate (1.0 / 60);
    CHECK_THAT (pad.artMotion().x, WithinAbs (1.0, 0.001));
}

TEST_CASE ("The scissors are cut from the accent colour, and open", "[skin][art]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto draw = [] (float open)
    {
        juce::Image image (juce::Image::ARGB, 360, 360, true);
        juce::Graphics g (image);
        ui::drawScissors (g, image.getBounds().toFloat(), open);
        return image;
    };

    const auto shut = draw (0.0f), open = draw (1.0f);
    CHECK (shut.getPixelAt (4, 4).isTransparent()); // they lie on the paper, with nothing behind them
    CHECK (difference (shut, open) > 20);

    int handles = 0;
    for (int y = 2; y < open.getHeight(); y += 4)
        for (int x = 2; x < open.getWidth(); x += 4)
            handles += near (open.getPixelAt (x, y), ui::palette::accent) ? 1 : 0;
    CHECK (handles > 40);
}

TEST_CASE ("The pad wears the playing program's colour and art", "[skin][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    auto* pad = dynamic_cast<ui::XYPad*> (findById (editor, "padTouch"));
    REQUIRE (pad != nullptr);
    auto& display = const_cast<livecut::DisplayState&> (processor.getDisplayState());
    const auto area = pad->padBounds().toNearestInt();

    const auto check = [&] (int program)
    {
        INFO ("program " << program);
        editor.refreshDisplay();
        pad->animate (1.0); // long enough for a change of program to finish
        const auto image = snapshot (*pad);
        CHECK (near (image.getPixelAt (3, pad->getHeight() / 2), ui::programColour (program))); // the sheet, left of the art
        CHECK (hueDistance (average (image.getClippedImage (area.reduced (40))), ui::programColour (program)) < 0.06f);
    };

    check (0);

    // Idle, choosing a program dresses the pad at once.
    set (processor, livecut::ids::padProgram, 4);
    check (4);

    // While the pad plays, it wears the program you hear, not the one waiting.
    display.padEngaged.store (true);
    display.padActiveProgram.store (2);
    display.padPendingProgram.store (6);
    check (2);
}

TEST_CASE ("The axis markers sit exactly beside the pointer mark", "[skin][pad][xy]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    auto& x = *processor.getParameterState().getParameter (livecut::ids::padX);
    auto& y = *processor.getParameterState().getParameter (livecut::ids::padY);
    ui::XYPad pad (x, y, *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);
    const auto area = pad.padBounds();

    for (const auto value : { 0.0f, 0.25f, 0.68f, 1.0f })
    {
        INFO ("value " << value);
        x.setValueNotifyingHost (value);
        y.setValueNotifyingHost (1.0f - value);

        auto& across = pad.xControl();
        auto& up = pad.yControl();
        CHECK_THAT (static_cast<float> (across.getX()) + across.getPositionOfValue (across.getValue()),
                    WithinAbs (area.getX() + value * area.getWidth(), 0.5));
        CHECK_THAT (static_cast<float> (up.getY()) + up.getPositionOfValue (up.getValue()),
                    WithinAbs (area.getBottom() - (1.0f - value) * area.getHeight(), 0.5));
    }

    // Each marker runs along its own edge of the art, outside it.
    CHECK (pad.xControl().getY() >= juce::roundToInt (area.getBottom()));
    CHECK (pad.yControl().getX() >= juce::roundToInt (area.getRight()));
}

TEST_CASE ("The phrase tape is cut into bars, with the block in the accent colour", "[skin][ruler]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ui::PhraseRuler ruler;
    ruler.setSize (616, 32); // as in the pad's header: the tape is 26 high, from 3 to 29

    ui::PhraseRuler::Snapshot phrase;
    phrase.status = livecut::DisplayState::Status::cutting;
    phrase.phraseBars = 4;
    phrase.unitsPerBar = 8;
    phrase.blockStart = 8.0; // the first half of bar 2
    phrase.blockEnd = 12.0;
    phrase.position = 9.0;
    ruler.show (phrase);
    const auto colour = ui::programColour (4);
    ruler.setAccent (colour);

    // Four pieces of 151 with gaps of 4: bar 2 runs from 155 to 306, and its units are 18.875 apart.
    const auto image = snapshot (ruler);
    CHECK (near (image.getPixelAt (183, 16), colour));       // inside the block, between unit marks
    CHECK_FALSE (near (image.getPixelAt (260, 16), colour)); // the rest of the bar
    CHECK (image.getPixelAt (153, 16).isTransparent());      // the cut between bars 1 and 2
    CHECK (near (image.getPixelAt (173, 16), ui::palette::ink)); // the playhead, one unit into bar 2,
    CHECK (near (image.getPixelAt (173, 1), ui::palette::ink));  // which stands proud of the tape
    CHECK (image.getPixelAt (183, 1).isTransparent());
    CHECK (ruler.statusText() == "Bar 2 of 4");

    // With no phrase to show, the status takes the tape's place.
    phrase.status = livecut::DisplayState::Status::waitingForHost;
    ruler.show (phrase);
    const auto waiting = snapshot (ruler);
    CHECK_FALSE (near (waiting.getPixelAt (183, 16), colour));
    CHECK (waiting.getPixelAt (500, 3).isTransparent());

    // Before any program has set it, the block is in the accent colour.
    ui::PhraseRuler plain;
    plain.setSize (616, 32);
    phrase.status = livecut::DisplayState::Status::cutting;
    plain.show (phrase);
    CHECK (near (snapshot (plain).getPixelAt (183, 16), ui::palette::accent));
}

TEST_CASE ("A screen reader can read the ruler's line, shown or not", "[skin][ruler]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    auto* ruler = findControl<ui::PhraseRuler> (editor);
    REQUIRE (ruler != nullptr);
    auto& display = const_cast<livecut::DisplayState&> (processor.getDisplayState());

    editor.refreshDisplay();
    CHECK (ruler->getTitle() == "Phrase");
    CHECK (ruler->getDescription() == "Press play in your host to start cutting");

    // While a phrase plays on the pad the tape has the header to itself, but a waiting switch is still described.
    display.status.store (livecut::DisplayState::Status::cutting);
    display.phraseBars.store (4);
    display.unitsPerBar.store (8);
    display.position.store (9.0);
    display.padEngaged.store (true);
    display.padPendingProgram.store (4);
    editor.refreshDisplay();
    CHECK (ruler->getDescription() == juce::String::fromUTF8 ("Cut \xe2\x86\x92 Filter on the beat"));

    display.padPendingProgram.store (-1);
    editor.refreshDisplay();
    CHECK (ruler->getDescription() == "Bar 2 of 4");
}

TEST_CASE ("The wordmark opens the About sheet, and Close or Escape puts it away", "[skin][about]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    auto* wordmark = dynamic_cast<juce::Button*> (findById (editor, "about"));
    auto* sheet = dynamic_cast<ui::AboutSheet*> (findById (editor, "aboutSheet"));
    auto* close = dynamic_cast<juce::Button*> (findById (editor, "closeAbout"));
    REQUIRE (wordmark != nullptr);
    REQUIRE (sheet != nullptr);
    REQUIRE (close != nullptr);

    CHECK (isShowing (*wordmark, editor));
    CHECK (wordmark->getTitle() == "About Livecut Redux");
    CHECK_FALSE (sheet->isVisible());

    // It fades in over the whole editor.
    wordmark->onClick();
    CHECK (isShowing (*sheet, editor));
    CHECK (sheet->getBounds() == juce::Rectangle<int> (LivecutAudioProcessorEditor::baseWidth, LivecutAudioProcessorEditor::baseHeight));
    CHECK_THAT (sheet->getAlpha(), WithinAbs (0.0, 0.001));
    sheet->animate (0.08);
    CHECK_THAT (sheet->getAlpha(), WithinAbs (0.5, 0.01));
    sheet->animate (1.0);
    CHECK_THAT (sheet->getAlpha(), WithinAbs (1.0, 0.001));

    // Close fades it out, and it is gone once the fade is done.
    close->onClick();
    CHECK (sheet->isVisible());
    sheet->animate (0.08);
    CHECK (sheet->isVisible());
    sheet->animate (1.0);
    CHECK_FALSE (sheet->isVisible());

    wordmark->onClick();
    sheet->animate (1.0);
    REQUIRE (sheet->isVisible());
    CHECK_FALSE (sheet->keyPressed (juce::KeyPress ('a')));
    CHECK (sheet->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    sheet->animate (1.0);
    CHECK_FALSE (sheet->isVisible());

    // The Classic pages keep the wordmark, and the sheet.
    useClassicSurface (processor);
    CHECK (isShowing (*wordmark, editor));
    wordmark->onClick();
    CHECK (isShowing (*sheet, editor));
}

TEST_CASE ("The About sheet keeps the keyboard, and says when it has closed", "[skin][about]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ui::AboutSheet sheet;
    sheet.setSize (880, 724);
    int closed = 0;
    sheet.onClosed = [&closed] { ++closed; };

    // Tab moves between Close and the link, and never out to the editor beneath.
    CHECK (sheet.isKeyboardFocusContainer());
    CHECK (sheet.getWantsKeyboardFocus());
    for (const auto* id : { "closeAbout" })
        CHECK (findById (sheet, id)->getWantsKeyboardFocus());
    CHECK (findControl<juce::HyperlinkButton> (sheet)->getWantsKeyboardFocus());

    sheet.open();
    sheet.animate (1.0);
    sheet.close();
    sheet.animate (0.08);
    CHECK (closed == 0); // still fading

    // Opened again mid-fade, it carries on from where the fade had got to, and was never closed.
    sheet.open();
    CHECK_THAT (sheet.getAlpha(), WithinAbs (0.5, 0.01));
    sheet.animate (1.0);
    CHECK (sheet.isVisible());
    CHECK (closed == 0);

    sheet.close();
    sheet.animate (1.0);
    CHECK (closed == 1);
    sheet.animate (1.0);
    CHECK (closed == 1);
}

TEST_CASE ("The About sheet says what Livecut was, why Redux exists and who made each", "[skin][about]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto story = ui::AboutSheet::story().joinIntoString (" ");

    // The people and the licence. The rest of the wording is free to change.
    for (const auto* fact : { "Remy Muller", "Smartelectronix", "Nick Collins", "BBCut", "Made by Sean Dearnaley",
                              "published by Human Being Music", "GNU General Public License, version 3 or later" })
    {
        INFO (fact);
        CHECK (story.contains (fact));
    }

    ui::AboutSheet sheet;
    CHECK (sheet.getTitle() == "About Livecut Redux");
    CHECK (sheet.getDescription() == story); // read out by a screen reader

    auto* source = findControl<juce::HyperlinkButton> (sheet);
    REQUIRE (source != nullptr);
    CHECK (source->getURL().toString (false) == "https://github.com/seandearnaley/livecut");
}

TEST_CASE ("Frames come steadily at 30 and 60 a second, whatever the display", "[skin][art]")
{
    // Each case: refreshes a second, how far each may arrive early or late, and the frame interval asked for.
    struct Case { double refresh, jitter, interval; int expected; };

    for (const auto& c : { Case { 60.0, 0.0, 1.0 / 60, 60 }, Case { 60.0, 0.0004, 1.0 / 60, 60 }, Case { 60.0, 0.0004, 1.0 / 30, 30 },
                           Case { 120.0, 0.0004, 1.0 / 60, 60 }, Case { 120.0, 0.0004, 1.0 / 30, 30 }, Case { 59.94, 0.0003, 1.0 / 30, 30 } })
    {
        INFO (c.refresh << " Hz, frames every " << c.interval << " s");
        ui::FrameClock clock;
        int frames = 0;
        double moved = 0.0;
        const auto refreshes = static_cast<int> (c.refresh * 10.0);

        for (int refresh = 0; refresh <= refreshes; ++refresh)
        {
            // Alternately early and late, as real refreshes are.
            const auto now = 100.0 + refresh / c.refresh + (refresh % 2 == 0 ? -c.jitter : c.jitter);

            if (const auto elapsed = clock.tick (now, c.interval))
            {
                ++frames;
                moved += *elapsed;
            }
        }

        CHECK (std::abs (frames - c.expected * 10) <= 2); // over ten seconds
        CHECK_THAT (moved, WithinAbs (10.0, 0.05));       // and no time is lost between frames
    }

    // The first refresh only starts the clock, and after a pause the animation carries on rather than leaps.
    ui::FrameClock clock;
    CHECK_FALSE (clock.tick (500.0, 1.0 / 60).has_value());
    REQUIRE (clock.tick (500.02, 1.0 / 60).has_value());
    const auto afterPause = clock.tick (560.0, 1.0 / 60);
    REQUIRE (afterPause.has_value());
    CHECK_THAT (*afterPause, WithinAbs (0.1, 0.0001));
}

TEST_CASE ("The pad is redrawn at 30 a second at rest and 60 while it plays", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    ui::XYPad pad (*processor.getParameterState().getParameter (livecut::ids::padX),
                   *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);

    const auto framesInASecond = [&pad] (double from)
    {
        int frames = 0;
        for (int refresh = 1; refresh <= 120; ++refresh)
            frames += pad.displayRefreshed (from + refresh / 120.0) ? 1 : 0;
        return frames;
    };

    CHECK_FALSE (pad.displayRefreshed (10.0)); // the first refresh only starts the clock
    CHECK (std::abs (framesInASecond (10.0) - 30) <= 1);

    pad.showEngaged (true);
    framesInASecond (11.0); // it takes a moment to liven up
    CHECK (std::abs (framesInASecond (12.0) - 60) <= 1);
}

TEST_CASE ("A change of program crossfades the pad", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    ui::XYPad pad (*processor.getParameterState().getParameter (livecut::ids::padX),
                   *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);
    const auto sheet = [&pad] { return snapshot (pad).getPixelAt (3, 264); }; // the sheet, left of the art
    const auto cut = ui::programColour (0), filter = ui::programColour (4), rush = ui::programColour (7);

    CHECK_THAT (pad.programChange(), WithinAbs (1.0, 0.0001));
    CHECK (near (sheet(), cut));

    pad.setProgram (4);
    CHECK_THAT (pad.programChange(), WithinAbs (0.0, 0.0001));
    CHECK (near (sheet(), cut)); // nothing has changed yet

    pad.animate (0.09); // halfway through the 180 ms
    CHECK_THAT (pad.programChange(), WithinAbs (0.5, 0.01));
    CHECK_FALSE (near (sheet(), cut));
    CHECK_FALSE (near (sheet(), filter));

    // Another change mid-way starts from whichever program showed more: Filter, by now.
    pad.animate (0.03);
    pad.setProgram (7);
    CHECK_THAT (pad.programChange(), WithinAbs (0.0, 0.0001));
    CHECK (near (sheet(), filter));

    pad.animate (0.18);
    CHECK_THAT (pad.programChange(), WithinAbs (1.0, 0.0001));
    CHECK (near (sheet(), rush));

    pad.setProgram (7); // the same program again changes nothing
    CHECK_THAT (pad.programChange(), WithinAbs (1.0, 0.0001));
}

TEST_CASE ("The pad answers each new block once, and only while it plays", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    auto& display = const_cast<livecut::DisplayState&> (processor.getDisplayState());

    // The editor opens in the middle of a block, so it has seen none start.
    display.status.store (livecut::DisplayState::Status::cutting);
    display.phraseBars.store (4);
    display.unitsPerBar.store (8);
    display.padEngaged.store (true);
    display.blockStart.store (8.0);
    LivecutAudioProcessorEditor editor (processor);
    auto* pad = dynamic_cast<ui::XYPad*> (findById (editor, "padTouch"));
    REQUIRE (pad != nullptr);
    const auto answered = [&]
    {
        editor.refreshDisplay();
        return pad->artMotion().cuts;
    };

    CHECK (answered() == 0);

    display.blockStart.store (12.0);
    CHECK (answered() == 1);
    CHECK (answered() == 1); // the same block, seen again

    // Blocks that start while the pad is idle, or while nothing is being cut, go unanswered.
    display.padEngaged.store (false);
    display.blockStart.store (16.0);
    CHECK (answered() == 1);
    display.padEngaged.store (true);
    display.status.store (livecut::DisplayState::Status::startingSoon);
    display.blockStart.store (20.0);
    CHECK (answered() == 1);

    display.status.store (livecut::DisplayState::Status::cutting);
    display.blockStart.store (24.0);
    CHECK (answered() == 2);
}

TEST_CASE ("A screen reader meets the pad surface in reading order", "[skin][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    editor.refreshDisplay();

    // Where each control comes in the order a screen reader steps through. The About sheet is put away, so it isn't there.
    juce::StringArray order;
    const std::function<void (juce::Component&)> visit = [&] (juce::Component& parent)
    {
        for (auto* component : parent.createFocusTraverser()->getAllComponents (&parent))
        {
            if (! isShowing (*component, editor))
                continue;

            order.addIfNotAlreadyThere (component->getComponentID());

            if (component->isFocusContainer())
                visit (*component);
        }
    };
    visit (editor);

    const auto before = [&order] (const char* first, const char* second)
    {
        INFO (first << " before " << second);
        const auto a = order.indexOf (first), b = order.indexOf (second);
        CHECK (a >= 0);
        CHECK (b >= 0);
        CHECK (a < b);
    };

    before ("about", "program0");
    before ("program0", "program7");
    before ("program7", "padTouch");
    before ("padTouch", "padX");
    before ("padX", "padY"); // left/right, then up/down, though the Y slider sits higher
    before ("padY", "padHold");
    CHECK_FALSE (order.contains ("closeAbout"));
}

// Hidden: run `LIVECUT_SCREENSHOT_DIR=<dir> LivecutTests "[.icon]"` to draw the app icon to
// icon.png in that directory. assets/icon.png is a copy of it.
TEST_CASE ("Draw the app icon", "[.icon]")
{
    const auto directory = screenshotDirectory();
    juce::ScopedJuceInitialiser_GUI gui;

    // macOS icons sit in an 824-point rounded square on a 1024-point canvas.
    juce::Image icon (juce::Image::ARGB, 1024, 1024, true);
    juce::Graphics g (icon);
    const auto paper = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    juce::Path plate;
    plate.addRoundedRectangle (paper, 186.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 22, { 0, 12 }).drawForPath (g, plate);
    g.setColour (ui::palette::paper);
    g.fillPath (plate);
    g.reduceClipRegion (plate);
    ui::drawScissors (g, paper.reduced (70.0f), 0.8f);

    juce::FileOutputStream stream (directory.getChildFile ("icon.png"));
    REQUIRE (stream.openedOk());
    stream.setPosition (0);
    stream.truncate();
    REQUIRE (juce::PNGImageFormat().writeImageToStream (icon, stream));
}

// Hidden: run `LivecutTests "[.benchmark]" -s` to see how long each program's art takes to
// reach the screen at the size and density a Retina display shows it, with both axes at
// their busiest: drawn straight into the platform's own renderer, and drawn in software
// and then copied across, which is what the pad does.
TEST_CASE ("Time each program's art", "[.benchmark]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr int frames = 40;
    const juce::Rectangle<float> area { 808.0f, 496.0f };
    const auto motionAt = [] (int frame)
    {
        ui::ArtMotion motion;
        motion.time = motion.spin = static_cast<float> (frame) / 60.0f;
        motion.x = motion.y = 1.0f;
        motion.kick = static_cast<float> (frame % 30) / 30.0f;
        motion.cuts = frame / 30;
        return motion;
    };
    const auto time = [&] (auto&& drawFrame)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int frame = 0; frame < frames; ++frame)
            drawFrame (frame);
        return (juce::Time::getMillisecondCounterHiRes() - start) / frames;
    };

    juce::Image screen (juce::Image::ARGB, 1616, 992, true, juce::NativeImageType());
    juce::Image canvas (juce::Image::RGB, 1616, 992, true, juce::SoftwareImageType());

    for (int program = 0; program < 8; ++program)
    {
        const auto direct = time ([&] (int frame)
        {
            juce::Graphics g (screen);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            ui::drawProgramArt (g, program, area, motionAt (frame));
        });
        const auto copied = time ([&] (int frame)
        {
            {
                juce::Graphics g (canvas);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                ui::drawProgramArt (g, program, area, motionAt (frame));
            }
            juce::Graphics g (screen);
            g.drawImageAt (canvas, 0, 0);
        });
        INFO ("program " << program << ": " << direct << " ms direct, " << copied << " ms in software and copied");
        CHECK (copied < 8.0); // half a 60 Hz frame
    }
}
