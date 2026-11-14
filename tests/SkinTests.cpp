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

    // With nothing holding the typeface, each font loads it afresh.
    CHECK (ui::uiFont (14.0f).getTypefacePtr() != ui::uiFont (14.0f).getTypefacePtr());

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
    ruler.setSize (616, 26);

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
    CHECK (near (image.getPixelAt (183, 13), colour));       // inside the block, between unit marks
    CHECK_FALSE (near (image.getPixelAt (260, 13), colour)); // the rest of the bar
    CHECK (image.getPixelAt (153, 13).isTransparent());      // the cut between bars 1 and 2
    CHECK (near (image.getPixelAt (173, 13), ui::palette::ink)); // the playhead, one unit into bar 2
    CHECK (ruler.statusText() == "Bar 2 of 4");

    // With no phrase to show, the status takes the tape's place.
    phrase.status = livecut::DisplayState::Status::waitingForHost;
    ruler.show (phrase);
    const auto waiting = snapshot (ruler);
    CHECK_FALSE (near (waiting.getPixelAt (183, 13), colour));
    CHECK (waiting.getPixelAt (500, 3).isTransparent());
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

    wordmark->onClick();
    CHECK (isShowing (*sheet, editor));
    CHECK (sheet->getBounds() == juce::Rectangle<int> (LivecutAudioProcessorEditor::baseWidth, LivecutAudioProcessorEditor::baseHeight));
    close->onClick();
    CHECK_FALSE (sheet->isVisible());

    wordmark->onClick();
    REQUIRE (sheet->isVisible());
    CHECK_FALSE (sheet->keyPressed (juce::KeyPress ('a')));
    CHECK (sheet->isVisible());
    CHECK (sheet->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (sheet->isVisible());

    // The Classic pages keep the wordmark, and the sheet.
    useClassicSurface (processor);
    CHECK (isShowing (*wordmark, editor));
    wordmark->onClick();
    CHECK (isShowing (*sheet, editor));
}

TEST_CASE ("The About sheet says what Livecut was, why Redux exists and who made each", "[skin][about]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto story = ui::AboutSheet::story().joinIntoString (" ");

    for (const auto* fact : { "Remy Muller", "Smartelectronix", "2003 and 2004", "Nick Collins", "BBCut", "32-bit",
                              "ground-up rewrite", "VST3", "Apple Silicon", "Windows", "CutProc11", "WarpCut", "SQPusher",
                              "Human Being Music", "GNU General Public License, version 3 or later" })
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
// draw at the size and density a Retina display shows it, with JUCE's software renderer.
TEST_CASE ("Time each program's art", "[.benchmark]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Image image (juce::Image::ARGB, 1616, 992, true, juce::SoftwareImageType());
    constexpr int frames = 60;

    for (int program = 0; program < 8; ++program)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int frame = 0; frame < frames; ++frame)
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            ui::ArtMotion motion;
            motion.time = motion.spin = static_cast<float> (frame) / 60.0f;
            motion.kick = static_cast<float> (frame % 30) / 30.0f;
            motion.cuts = frame / 30;
            ui::drawProgramArt (g, program, { 808.0f, 496.0f }, motion);
        }

        const auto each = (juce::Time::getMillisecondCounterHiRes() - start) / frames;
        INFO ("program " << program << ": " << each << " ms a frame");
        CHECK (each < 16.0); // inside a 60 Hz frame, even drawn in software
    }
}
