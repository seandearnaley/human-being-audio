// SPDX-License-Identifier: GPL-3.0-or-later

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ProcessorTestSupport.h"
#include "ui/AboutSheet.h"
#include "ui/Controls.h"
#include "ui/LivecutLookAndFeel.h"
#include "ui/PadArt.h"
#include "ui/PadPage.h"
#include "ui/PhraseRuler.h"
#include "ui/RangeSlider.h"
#include "ui/Theme.h"
#include "ui/XYPad.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <ctime>
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

// The colour of an image's bright card, averaged over a grid of samples. Its deep
// card leans toward red or blue on purpose, so it is the bright card that says what colour a picture is.
juce::Colour average (const juce::Image& image)
{
    double red = 0.0, green = 0.0, blue = 0.0;
    int samples = 0;
    constexpr int columns = 32, rows = 18;

    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
        {
            const auto pixel = image.getPixelAt ((2 * column + 1) * image.getWidth() / (2 * columns), (2 * row + 1) * image.getHeight() / (2 * rows));

            if (pixel.getBrightness() < 0.6f)
                continue;

            red += pixel.getFloatRed();
            green += pixel.getFloatGreen();
            blue += pixel.getFloatBlue();
            ++samples;
        }

    REQUIRE (samples > 20); // there is bright card to speak of
    const auto count = static_cast<double> (samples);
    return juce::Colour::fromFloatRGBA (static_cast<float> (red / count), static_cast<float> (green / count), static_cast<float> (blue / count), 1.0f);
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
static juce::Image art (int program, const ui::ArtMotion& motion)
{
    juce::Image image (juce::Image::ARGB, 404, 248, true);
    juce::Graphics g (image);
    ui::drawProgramArt (g, program, image.getBounds().toFloat(), motion);
    return image;
}

// How many of a grid of sample points differ between two pictures of the same size.
static int difference (const juce::Image& a, const juce::Image& b)
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
        CHECK (contrast (colour, ui::palette::onColour) >= 4.5); // a playing tab's name and the axis tags are black on it
        CHECK (contrast (colour, ui::palette::well) >= 4.5);     // a resting tab's name is the colour, on a dark tab

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
        later.beat = later.spin = 1.3f;
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

TEST_CASE ("The pad's art keeps a slow pulse at rest, and the host's beat while it cuts", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    ui::XYPad pad (*processor.getParameterState().getParameter (livecut::ids::padX),
                   *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);
    const auto beatPhase = [&pad] { return pad.artMotion().beat - std::floor (pad.artMotion().beat); };

    // At rest: half a beat a second.
    pad.animate (1.0);
    CHECK_THAT (pad.artMotion().beat, WithinAbs (0.5, 0.001));

    // The host cuts at 150 beats a minute. The editor reports its place 30 times a second and the display refreshes at 60.
    constexpr auto tempo = 2.5; // beats a second
    auto host = 7.3;            // beats into the phrase: nowhere near the pad's own count
    const auto run = [&] (double seconds)
    {
        for (int frame = 0; frame < juce::roundToInt (seconds * 60.0); ++frame)
        {
            if (frame % 2 == 0)
                pad.followBeat (ui::HostBeat { host, tempo });

            pad.animate (1.0 / 60);
            host += tempo / 60.0;
        }
    };

    run (1.5); // long enough to be drawn onto the beat
    const auto phaseError = [&]
    {
        const auto error = static_cast<float> (host - std::floor (host)) - beatPhase();
        return std::abs (error - std::round (error));
    };
    CHECK (phaseError() < 0.03f);

    const auto before = pad.artMotion().beat;
    run (2.0);
    CHECK_THAT (pad.artMotion().beat - before, WithinAbs (2.0 * tempo, 0.05)); // it runs at the host's tempo
    CHECK (phaseError() < 0.03f);

    // The phrase starts again: the host's count jumps back, on a beat. The art doesn't flinch.
    host -= 4.0;
    const auto atJump = pad.artMotion().beat;
    run (1.0);
    CHECK_THAT (pad.artMotion().beat - atJump, WithinAbs (tempo, 0.05));
    CHECK (phaseError() < 0.03f);

    // The host stops cutting: back to the slow pulse.
    pad.followBeat (std::nullopt);
    const auto resting = pad.artMotion().beat;
    pad.animate (1.0);
    CHECK_THAT (pad.artMotion().beat - resting, WithinAbs (0.5, 0.001));
}

TEST_CASE ("The pad stays on the beat when the host's place arrives in large blocks", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    ui::XYPad pad (*processor.getParameterState().getParameter (livecut::ids::padX),
                   *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);

    // 174 beats a minute through a 4096-sample buffer at 44.1 kHz. The host's place moves on only every 93 ms,
    // so the editor, looking 30 times a second, often sees the same place twice.
    constexpr auto tempo = 2.9, block = 4096.0 / 44100.0;
    auto time = 0.0, worst = 0.0;

    for (int frame = 0; frame < 60 * 8; ++frame)
    {
        if (frame % 2 == 0)
            pad.followBeat (ui::HostBeat { std::floor (time / block) * block * tempo, tempo });

        pad.animate (1.0 / 60);
        time += 1.0 / 60;

        if (frame > 60) // once it has been drawn onto the beat
        {
            const auto error = time * tempo - static_cast<double> (pad.artMotion().beat);
            worst = std::max (worst, std::abs (error - std::round (error)));
        }
    }

    // It can only know the host's place to within one look, a tenth of a beat here.
    CHECK (worst < 0.12);
}

TEST_CASE ("The art's count of beats wraps where no picture shows it", "[skin][art]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (int program = 0; program < 8; ++program)
        for (const auto axis : { 0.0f, 0.4f, 0.7f, 1.0f }) // Bounce's three, four, five and six hops among them
        {
            INFO ("program " << program << " with its axes at " << axis);
            ui::ArtMotion early, late;
            early.x = early.y = late.x = late.y = axis;
            early.beat = early.spin = 1.25f;
            late.beat = late.spin = ui::ArtMotion::beatsBeforeWrap + 1.25f;
            CHECK (difference (art (program, early), art (program, late)) <= 2);
        }
}

TEST_CASE ("A new block kicks the art, which glides after the pointer mark", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    auto& x = *processor.getParameterState().getParameter (livecut::ids::padX);
    ui::XYPad pad (x, *processor.getParameterState().getParameter (livecut::ids::padY),
                   *processor.getParameterState().getParameter (livecut::ids::padTouch));
    pad.setSize (840, 528);
    pad.animate (1.0);

    // The kick settles in well under a second; the braked count stands still at the kick, then runs again.
    pad.pulse();
    CHECK (pad.artMotion().cuts == 1);
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

TEST_CASE ("The pad follows the host's beat only while it cuts", "[skin][art][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    auto* pad = dynamic_cast<ui::XYPad*> (findById (editor, "padTouch"));
    REQUIRE (pad != nullptr);
    auto& display = const_cast<livecut::DisplayState&> (processor.getDisplayState());
    const auto beatsInASecond = [&]
    {
        const auto before = pad->artMotion().beat;
        for (int frame = 0; frame < 60; ++frame)
        {
            display.beat.store (display.beat.load() + 3.0 / 60.0); // 180 beats a minute
            editor.refreshDisplay();
            pad->animate (1.0 / 60);
        }
        return pad->artMotion().beat - before;
    };

    // Stopped: the pad's own slow pulse, whatever the display's stale place says.
    CHECK_THAT (beatsInASecond(), WithinAbs (0.5, 0.01));

    // Cutting at 180 beats a minute.
    display.status.store (livecut::DisplayState::Status::cutting);
    display.phraseBars.store (1);
    display.unitsPerBar.store (14);
    display.bpm.store (180.0);
    display.padEngaged.store (true);
    beatsInASecond();
    CHECK_THAT (beatsInASecond(), WithinAbs (3.0, 0.1));

    // The art's beat is the host's quarter note, not a count from where the phrase began. In 7/8 a one-bar
    // phrase starts again every three and a half of them, and the art stays on the host's.
    display.beat.store (20.0);

    for (int frame = 0; frame < 140; ++frame)
    {
        editor.refreshDisplay();
        pad->animate (1.0 / 60);
        display.beat.store (display.beat.load() + 3.0 / 60.0);
        display.position.store (std::fmod (display.position.load() + 14.0 * 3.0 / 3.5 / 60.0, 14.0)); // a phrase of 14 units, again and again
    }

    // Seven beats on, the host is on a quarter note, and so is the art.
    CHECK_THAT (pad->artMotion().beat - std::round (pad->artMotion().beat), WithinAbs (0.0, 0.05));

    // Away on the Classic pages the pad is not shown, and goes back to its own slow pulse.
    useClassicSurface (processor);
    editor.refreshDisplay();
    const auto before = pad->artMotion().beat;
    pad->animate (1.0);
    CHECK_THAT (pad->artMotion().beat - before, WithinAbs (0.5, 0.01));
}

TEST_CASE ("A tab's name reads on its face, whatever the tab is doing", "[skin][pad]")
{
    using State = ui::TabState;

    for (int program = 0; program < 8; ++program)
        for (const auto state : { State::resting, State::hovered, State::pressed, State::waiting, State::playing })
            for (const auto lit : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) // a waiting tab's light, from the beat to the next
            {
                INFO ("program " << program << ", state " << static_cast<int> (state) << ", lit " << lit);
                const auto colours = ui::tabColours (ui::programColour (program), state, lit);
                CHECK (contrast (colours.face, colours.name) >= 4.5);
            }

    // A waiting tab is brightest on the beat, and never as bright as the tab that is playing.
    const auto orange = ui::programColour (0);
    const auto onTheBeat = ui::tabColours (orange, State::waiting, 1.0f).face, before = ui::tabColours (orange, State::waiting, 0.0f).face;
    CHECK (luminance (onTheBeat) > luminance (before));
    CHECK (luminance (onTheBeat) < luminance (ui::tabColours (orange, State::playing, 0.0f).face));
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
    CHECK (shut.getPixelAt (4, 4).isTransparent()); // they lie on the sheet, with nothing behind them
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
                              "published by Human Being Audio", "GNU General Public License, version 3 or later" })
    {
        INFO (fact);
        CHECK (story.contains (fact));
    }

    ui::AboutSheet sheet;
    CHECK (sheet.getTitle() == "About Livecut Redux");
    CHECK (sheet.getDescription() == story); // read out by a screen reader

    juce::Image header (juce::Image::ARGB, 128, 64, true, juce::SoftwareImageType{});
    {
        juce::Graphics graphics (header);
        sheet.paintOverChildren (graphics);
    }
    const auto mark = header.getPixelAt (35, 24);
    CHECK (mark.getBlue() > mark.getRed());
    auto* source = findControl<juce::HyperlinkButton> (sheet);
    REQUIRE (source != nullptr);
    CHECK (source->getURL().toString (false) == "https://github.com/seandearnaley/human-being-audio/tree/main/products/livecut-redux");
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

TEST_CASE ("The pad surface is laid out on one even grid", "[skin][pad]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);
    editor.refreshDisplay();
    const auto in = [&editor] (const char* id)
    {
        auto* component = findById (editor, id);
        REQUIRE (component != nullptr);
        return editor.getLocalArea (component, component->getLocalBounds());
    };
    constexpr int margin = 20;
    const auto width = editor.getWidth(), height = editor.getHeight();

    // The header: the wordmark and the surface switch are a pair, with the tape centred between them on one line.
    const auto wordmark = in ("about"), surface = in ("surface");
    const auto tape = editor.getLocalArea (findControl<ui::PhraseRuler> (editor), findControl<ui::PhraseRuler> (editor)->getLocalBounds());
    CHECK (wordmark.getX() == margin);
    CHECK (surface.getRight() == width - margin);
    CHECK (wordmark.getWidth() == surface.getWidth());
    CHECK (tape.getCentreX() == width / 2);
    CHECK (tape.getX() - wordmark.getRight() == surface.getX() - tape.getRight());
    CHECK (wordmark.getCentreY() == tape.getCentreY());
    CHECK (surface.getCentreY() == tape.getCentreY());

    // The tabs and the sheet run from margin to margin, and the art has the same rail on every side.
    const auto sheet = in ("padTouch");
    auto* pad = dynamic_cast<ui::XYPad*> (findById (editor, "padTouch"));
    const auto art = pad->padBounds().toNearestInt();
    CHECK (in ("program0").getX() == margin);
    CHECK (in ("program7").getRight() == width - margin);
    CHECK (in ("program0").getBottom() == sheet.getY());
    CHECK (sheet.getX() == margin);
    CHECK (sheet.getRight() == width - margin);
    CHECK (art.getX() == art.getY());
    CHECK (art.getX() == pad->getWidth() - art.getRight());
    CHECK (art.getX() == pad->getHeight() - art.getBottom());

    // The bottom row: one centre line, even gaps, and the same margin beneath it as beside it.
    const auto hold = in ("padHold"), straight = in ("padStraight"), quantize = in ("padQuantize"), mix = in ("padMix");
    CHECK (hold.getX() == margin);
    CHECK (mix.getRight() == width - margin);
    CHECK (mix.getBottom() == height - margin);
    CHECK (straight.getX() - hold.getRight() == quantize.getX() - straight.getRight());
    for (const auto& control : { straight, quantize, mix })
        CHECK (control.getCentreY() == hold.getCentreY());
    CHECK (hold.getY() - sheet.getBottom() == margin);

    // Mix is on one line: its name, its slider and its value share that centre line too.
    auto* mixRow = findById (editor, "padMix");
    for (auto* child : mixRow->getChildren())
        CHECK (editor.getLocalArea (child, child->getLocalBounds()).getCentreY() == hold.getCentreY());
}

TEST_CASE ("The wordmark makes its cut again at each new block", "[skin][wordmark]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ui::Wordmark wordmark;
    wordmark.setSize (96, 36);

    // How many pixels differ between two pictures of the wordmark, over the columns from `from` up to `to`.
    const auto changed = [] (const juce::Image& a, const juce::Image& b, int from, int to)
    {
        int count = 0;

        for (int x = from; x < to; ++x)
            for (int y = 0; y < a.getHeight(); ++y)
                count += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;

        return count;
    };
    const auto resting = snapshot (wordmark);
    const auto width = resting.getWidth();

    // CUT drops under the blade while LIVE and Redux stay put, and then it springs back.
    wordmark.cut();
    const auto dropped = snapshot (wordmark);
    CHECK (changed (resting, dropped, 0, 30) == 0); // LIVE ends at 33
    CHECK (changed (resting, dropped, width / 2, width) > 40);
    wordmark.animate (0.1);
    const auto rising = snapshot (wordmark);
    CHECK (changed (resting, rising, width / 2, width) > 0);
    CHECK (changed (dropped, rising, width / 2, width) > 0);
    wordmark.animate (1.0);
    CHECK (changed (resting, snapshot (wordmark), 0, width) == 0);

    // In the editor it follows the tape: a cut for each block that starts, and none for the one it opened on.
    LivecutAudioProcessor processor;
    auto& display = const_cast<livecut::DisplayState&> (processor.getDisplayState());
    display.status.store (livecut::DisplayState::Status::cutting);
    display.phraseBars.store (4);
    display.unitsPerBar.store (8);
    display.blockStart.store (8.0);
    LivecutAudioProcessorEditor editor (processor);
    auto* mark = dynamic_cast<ui::Wordmark*> (findById (editor, "about"));
    REQUIRE (mark != nullptr);
    editor.refreshDisplay();
    CHECK_FALSE (mark->isCutting());

    display.blockStart.store (12.0);
    editor.refreshDisplay();
    CHECK (mark->isCutting());
    mark->animate (1.0);
    editor.refreshDisplay(); // the same block, seen again
    CHECK_FALSE (mark->isCutting());

    display.status.store (livecut::DisplayState::Status::waitingForHost);
    display.blockStart.store (16.0);
    editor.refreshDisplay();
    CHECK_FALSE (mark->isCutting());
}

TEST_CASE ("A slider's thumb travels from one end of its row to the other", "[skin][classic]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    LivecutAudioProcessorEditor editor (processor);

    // At its ends the thumb's edge meets the row's, where the caption starts and the value stops, whatever the row's height.
    for (const auto* id : { "wet", "seed", "resonanceRootHz", "padMix" })
    {
        INFO (id);
        auto* row = findById (editor, id);
        REQUIRE (row != nullptr);
        auto* slider = findControl<juce::Slider> (*row);
        REQUIRE (slider != nullptr);
        const auto radius = static_cast<double> (slider->getLookAndFeel().getSliderThumbRadius (*slider));
        CHECK_THAT (slider->getPositionOfValue (slider->getMinimum()), WithinAbs (radius, 0.01));
        CHECK_THAT (slider->getPositionOfValue (slider->getMaximum()), WithinAbs (slider->getWidth() - radius, 0.01));
    }
}

TEST_CASE ("The Classic pages are laid out on one grid", "[skin][classic]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LivecutAudioProcessor processor;
    useClassicSurface (processor);
    LivecutAudioProcessorEditor editor (processor);
    editor.refreshDisplay();
    constexpr int margin = 20, gap = 12, padding = 12, rowPadding = 8, sliderRow = 40;
    const auto width = editor.getWidth(), height = editor.getHeight();
    const auto in = [&editor] (juce::Component& component) { return editor.getLocalArea (&component, component.getLocalBounds()); };
    const auto at = [&] (const char* id)
    {
        auto* component = findById (editor, id);
        REQUIRE (component != nullptr);
        return in (*component);
    };

    // One of `count` equal columns between the margins, `gap` apart.
    const auto column = [&] (int index, int count)
    {
        const auto each = (width - 2 * margin - (count - 1) * gap) / count;
        return juce::Range<int>::withStartAndLength (margin + index * (each + gap), each);
    };
    const auto across = [] (juce::Rectangle<int> bounds) { return bounds.getHorizontalRange(); };

    // Four bands, a margin apart and a margin from every edge: the header, the tape, a page of cards and the strip
    // of controls that every page shares.
    const auto wordmark = at ("about"), surface = at ("surface"), tape = in (*findControl<ui::PhraseRuler> (editor));
    const auto page = at ("impactPage");
    CHECK (wordmark.getPosition() == juce::Point<int> (margin, margin));
    CHECK (surface.getRight() == width - margin);
    CHECK (across (tape) == juce::Range<int> (margin, width - margin));
    CHECK (tape.getY() - wordmark.getBottom() == margin);
    CHECK (across (page) == juce::Range<int> (margin, width - margin));
    CHECK (page.getY() - tape.getBottom() == margin);
    CHECK (at ("wet").getY() - page.getBottom() == margin);
    CHECK (at ("swing").getBottom() == height - margin);
    CHECK (at ("fracturePage") == page);
    CHECK (at ("resonancePage") == page);

    // The header's groups share its centre line: the tabs evenly spaced, then the procedures, then the surface switch.
    const std::array tabs { "patternTab", "recutTab", "impactTab", "fractureTab", "resonanceTab" };
    for (const auto* id : { "patternTab", "recutTab", "impactTab", "fractureTab", "resonanceTab", "procedure", "surface" })
    {
        INFO (id);
        CHECK (at (id).getCentreY() == wordmark.getCentreY());
    }
    for (std::size_t tab = 2; tab < tabs.size(); ++tab)
        CHECK (at (tabs[tab]).getX() - at (tabs[tab - 1]).getRight() == at (tabs[1]).getX() - at (tabs[0]).getRight());
    CHECK (at ("patternTab").getX() >= wordmark.getRight());
    CHECK (at ("procedure").getX() - at ("resonanceTab").getRight() == gap);
    CHECK (surface.getX() - at ("procedure").getRight() == gap);

    // Every slider on every page fills a card's column, in thirds or in halves, and they are all one height.
    const std::function<void (juce::Component&)> checkSliders = [&] (juce::Component& component)
    {
        if (! component.isVisible())
            return;

        if (dynamic_cast<ui::SliderRow*> (&component) != nullptr || dynamic_cast<ui::RangeSlider*> (&component) != nullptr)
        {
            INFO (component.getComponentID());
            const auto bounds = in (component);
            CHECK (bounds.getHeight() == sliderRow);

            if (bounds.getY() >= page.getBottom())
                return; // the strip's sliders, below

            auto fills = false;
            for (const auto count : { 2, 3 })
                for (int index = 0; index < count; ++index)
                    fills = fills || across (bounds) == column (index, count).expanded (-padding);
            CHECK (fills);
            CHECK (page.contains (bounds));
            return;
        }

        for (auto* child : component.getChildren())
            checkSliders (*child);
    };

    for (const auto* tab : tabs)
    {
        INFO (tab);
        dynamic_cast<juce::TextButton*> (findById (editor, tab))->onClick();
        checkSliders (editor);
    }

    // Each procedure brings its own controls to the Pattern page.
    dynamic_cast<juce::TextButton*> (findById (editor, "patternTab"))->onClick();

    for (int procedure = 0; procedure < 4; ++procedure)
    {
        INFO ("procedure " << procedure);
        set (processor, livecut::ids::procedure, static_cast<float> (procedure));
        checkSliders (editor);
    }

    // On Pattern and Recut the cards end where the page does.
    CHECK (at ("minDelay,maxDelay").getBottom() + rowPadding == page.getBottom());

    // The strip: four sliders in the left half, and capture and hold in the right, each with its status beside it.
    const auto left = column (0, 2), right = column (1, 2);
    CHECK (at ("wet").getX() == left.getStart());
    CHECK (at ("outputTrim").getRight() == left.getEnd());
    CHECK (at ("wet").getWidth() == at ("outputTrim").getWidth());
    CHECK (at ("outputTrim").getX() - at ("wet").getRight() == gap);
    CHECK (across (at ("swing")) == across (at ("wet")));
    CHECK (across (at ("accent")) == across (at ("outputTrim")));
    CHECK (at ("capture").getX() == right.getStart());
    CHECK (at ("saveCapture").getRight() == right.getEnd());
    CHECK (at ("capture").getWidth() == at ("saveCapture").getWidth());
    CHECK (at ("capture").getCentreY() == at ("wet").getCentreY());
    CHECK (at ("captureStatus").getX() == right.getStart());
    CHECK (at ("hold").getX() == right.getStart());
    CHECK (at ("hold").getCentreY() == at ("swing").getCentreY());
    CHECK (at ("holdStatus").getCentreY() == at ("hold").getCentreY());
    CHECK (at ("holdStatus").getRight() == right.getEnd());
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
    const auto square = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    juce::Path plate;
    plate.addRoundedRectangle (square, 186.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 22, { 0, 12 }).drawForPath (g, plate);
    g.setColour (ui::palette::backdrop);
    g.fillPath (plate);
    g.reduceClipRegion (plate);
    ui::drawScissors (g, square.reduced (70.0f), 0.8f);

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
        motion.beat = motion.spin = static_cast<float> (frame) / 30.0f;
        motion.x = motion.y = 1.0f;
        motion.kick = static_cast<float> (frame % 30) / 30.0f;
        motion.cuts = frame / 30;
        return motion;
    };
    const auto time = [&] (auto&& drawFrame)
    {
        // Processor time, not the wall clock, so a busy machine doesn't slow the answer.
        const auto start = std::clock();
        for (int frame = 0; frame < frames; ++frame)
            drawFrame (frame);
        return 1000.0 * static_cast<double> (std::clock() - start) / CLOCKS_PER_SEC / frames;
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
