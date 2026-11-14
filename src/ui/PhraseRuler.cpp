// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/PhraseRuler.h"

#include "engine/Cutter.h"
#include "ui/Theme.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace livecut::ui
{

PhraseRuler::Snapshot PhraseRuler::Snapshot::of (const DisplayState& state)
{
    Snapshot snapshot;
    snapshot.status = state.status.load (std::memory_order_relaxed);
    snapshot.phraseBars = state.phraseBars.load (std::memory_order_relaxed);
    snapshot.unitsPerBar = state.unitsPerBar.load (std::memory_order_relaxed);
    snapshot.blockStart = state.blockStart.load (std::memory_order_relaxed);
    snapshot.blockEnd = state.blockEnd.load (std::memory_order_relaxed);
    snapshot.position = state.position.load (std::memory_order_relaxed);
    return snapshot;
}

bool PhraseRuler::Snapshot::sameAs (const Snapshot& other) const
{
    const auto bits = [] (double value) { return std::bit_cast<std::uint64_t> (value); };

    return status == other.status && phraseBars == other.phraseBars && unitsPerBar == other.unitsPerBar
        && bits (blockStart) == bits (other.blockStart) && bits (blockEnd) == bits (other.blockEnd)
        && bits (position) == bits (other.position);
}

void PhraseRuler::show (const Snapshot& snapshot)
{
    if (snapshot.sameAs (current))
        return;

    current = snapshot;
    describeForScreenReaders();
    repaint();
}

void PhraseRuler::setHeadline (const juce::String& text)
{
    if (headline == text)
        return;

    headline = text;
    describeForScreenReaders();
    repaint();
}

void PhraseRuler::describeForScreenReaders()
{
    setTitle ("Phrase");
    setDescription (headline.isNotEmpty() ? headline : describe (current));
}

juce::String PhraseRuler::describe (const Snapshot& snapshot)
{
    using Status = DisplayState::Status;

    switch (snapshot.status)
    {
        case Status::waitingForHost:
            return "Press play in your host to start cutting";
        case Status::barTooLong:
            return "Bars longer than " + juce::String (engine::Cutter::maxBarSeconds, 0)
                 + " seconds pass through uncut. Raise the tempo to cut them.";
        case Status::planOverflow: return "Cut plan queue exhausted; restart transport";
        case Status::sourceUnavailable: return "Source audio unavailable; restart transport";
        case Status::filling: return "Studio filling its 500 ms audio buffer";
        case Status::preparationError: return "Cannot prepare audio: use a sample rate from 8 to 384 kHz and prepare again";
        case Status::streamError: return "Audio stream error: prepare the plug-in again";
        case Status::startingSoon:
            return "Starting on the next unit";
        case Status::cutting:
        {
            const auto bar = juce::jlimit (1, juce::jmax (1, snapshot.phraseBars),
                                           static_cast<int> (std::floor (snapshot.position / snapshot.unitsPerBar)) + 1);
            return "Bar " + juce::String (bar) + " of " + juce::String (snapshot.phraseBars);
        }
    }

    return {};
}

void PhraseRuler::setAccent (juce::Colour colour)
{
    if (accent == colour)
        return;

    accent = colour;
    repaint();
}

void PhraseRuler::paint (juce::Graphics& g)
{
    constexpr auto tapeHeight = 26.0f, barGap = 4.0f;
    const auto area = getLocalBounds().toFloat();

    // With no phrase to show, the line takes the tape's place.
    if (current.status != DisplayState::Status::cutting || current.phraseBars < 1 || current.unitsPerBar < 1)
    {
        g.setColour (palette::ink);
        g.setFont (uiFont (14.0f, weight::medium));
        g.drawText (headline.isNotEmpty() ? headline : describe (current), area, juce::Justification::centredLeft);
        return;
    }

    const auto tape = area.withSizeKeepingCentre (area.getWidth(), tapeHeight);
    const auto barWidth = (tape.getWidth() - barGap * static_cast<float> (current.phraseBars - 1)) / static_cast<float> (current.phraseBars);
    const auto units = static_cast<double> (current.unitsPerBar);

    // Where a position in units falls along the tape, within the bar that holds it.
    const auto xFor = [&] (double position, int bar)
    {
        const auto within = juce::jlimit (0.0, units, position - bar * units);
        return tape.getX() + static_cast<float> (bar) * (barWidth + barGap) + static_cast<float> (within / units) * barWidth;
    };

    for (int bar = 0; bar < current.phraseBars; ++bar)
    {
        const auto piece = juce::Rectangle<float> (xFor (bar * units, bar), tape.getY(), barWidth, tape.getHeight());
        juce::Path shape;
        shape.addRoundedRectangle (piece, 3.0f);
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);

        g.setColour (palette::rule.withMultipliedAlpha (0.55f));
        g.fillRect (piece);

        // The block being cut, where it crosses this bar.
        g.setColour (accent);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (current.blockStart, bar), piece.getY(),
                                                                xFor (current.blockEnd, bar), piece.getBottom()));

        g.setColour (palette::rule);
        for (int unit = 1; unit < current.unitsPerBar; ++unit)
            g.fillRect (juce::Rectangle<float> (xFor (bar * units + unit, bar) - 0.5f, piece.getY(), 1.0f, piece.getHeight()));
    }

    const auto bar = juce::jlimit (0, current.phraseBars - 1, static_cast<int> (std::floor (current.position / units)));
    g.setColour (palette::ink);
    g.fillRoundedRectangle (juce::Rectangle<float> (xFor (current.position, bar) - 1.0f, tape.getY() - 3.0f, 2.0f, tape.getHeight() + 6.0f), 1.0f);
}

} // namespace livecut::ui
