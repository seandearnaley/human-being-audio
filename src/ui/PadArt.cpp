// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/PadArt.h"

#include "ui/Theme.h"

#include <array>
#include <cmath>

namespace livecut::ui
{

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

float mix (float a, float b, float t) { return a + (b - a) * t; }
float ease (float t) { return t * t * (3.0f - 2.0f * t); }
float fraction (float value) { return value - std::floor (value); }

// A repeatable value from 0 to 1 for up to three whole numbers.
float chance (int a, int b, int c = 0)
{
    auto h = static_cast<juce::uint32> (a) * 374761393u + static_cast<juce::uint32> (b) * 668265263u
           + static_cast<juce::uint32> (c) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float> ((h ^ (h >> 16)) & 0xffffffu) / 16777216.0f;
}

// Card in tones of one colour: below zero is deeper, above zero paler.
struct Card
{
    juce::Colour tone (float amount) const
    {
        return amount >= 0.0f ? colour.interpolatedWith (juce::Colours::white, amount * 0.8f)
                              : colour.interpolatedWith (colour.withMultipliedBrightness (0.34f).withMultipliedSaturation (1.2f), -amount);
    }

    juce::Colour colour;
};

// Lays a piece of card down, on the shadow it casts a little below itself.
void lay (juce::Graphics& g, const juce::Path& piece, juce::Colour colour, float lift = 3.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillPath (piece, juce::AffineTransform::translation (lift * 0.4f, lift));
    g.setColour (colour);
    g.fillPath (piece);
}

// Shows the next piece of card down through a hole, in the shadow of the hole's upper edge.
void sink (juce::Graphics& g, const juce::Path& hole, juce::Colour colour, float depth = 4.0f)
{
    g.setColour (colour.interpolatedWith (juce::Colours::black, 0.28f));
    g.fillPath (hole);
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (hole);
    g.setColour (colour);
    g.fillPath (hole, juce::AffineTransform::translation (depth * 0.4f, depth));
}

juce::Path disc (juce::Point<float> centre, float radiusX, float radiusY)
{
    juce::Path path;
    path.addEllipse (centre.x - radiusX, centre.y - radiusY, 2.0f * radiusX, 2.0f * radiusY);
    return path;
}

juce::Path disc (juce::Point<float> centre, float radius) { return disc (centre, radius, radius); }

// Strips of tape cut on the diagonal. X cuts them into more pieces, Y slides
// the pieces further out of place, and each cut re-edits them.
void drawCut (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (-0.62f));
    const juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-0.06f).scaled (1.14f).translated (area.getCentre())); // rows are laid out around the origin
    constexpr int rows = 7;
    const auto width = area.getWidth(), rowHeight = area.getHeight() / rows;
    const auto pieces = 2 + juce::roundToInt (m.x * 5.0f);
    const auto settled = ease (1.0f - m.kick); // 0 just after a cut, 1 once the pieces have slid home
    const auto lean = rowHeight * 0.55f;

    for (int row = 0; row < rows; ++row)
    {
        const auto top = (static_cast<float> (row) - rows * 0.5f) * rowHeight;

        // Where this row is cut: roughly even, never quite.
        const auto edge = [&] (int index)
        {
            if (index <= 0 || index >= pieces)
                return (index <= 0 ? -0.5f : 0.5f) * width * 1.3f;

            return (static_cast<float> (index) / static_cast<float> (pieces) - 0.5f + (chance (row, index) - 0.5f) * 0.4f / static_cast<float> (pieces)) * width;
        };

        for (int piece = 0; piece < pieces; ++piece)
        {
            const auto slide = [&] (int cut) { return (chance (row * 31 + piece, cut, 1) - 0.5f) * 2.8f * rowHeight * m.y; };
            const auto dx = mix (slide (m.cuts - 1), slide (m.cuts), settled) + std::sin (m.time * 0.5f + static_cast<float> (row) * 1.3f + static_cast<float> (piece) * 2.1f) * 4.0f;
            const auto dy = (chance (row, piece, 2) - 0.5f) * rowHeight * 0.14f;
            const auto left = edge (piece) + 2.0f, right = edge (piece + 1) - 2.0f;

            juce::Path strip;
            strip.addQuadrilateral (left + lean, top + 1.5f, right + lean, top + 1.5f, right - lean, top + rowHeight - 1.5f, left - lean, top + rowHeight - 1.5f);
            strip.applyTransform (juce::AffineTransform::translation (dx, dy));
            lay (g, strip, card.tone (mix (-0.3f, 0.5f, chance (row, piece, 3))));
        }
    }
}

// A ball's hops. X adds hops; Y makes each one climb higher than the last
// instead of dying away.
void drawBounce (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (0.5f));
    const auto width = area.getWidth(), height = area.getHeight();
    const auto floor = area.getY() + height * 0.76f;

    // Card hanging in from the top, and low hills behind the floor, so the ball has somewhere to be.
    for (int drape = 0; drape < 2; ++drape)
    {
        juce::Path edge;
        edge.startNewSubPath (area.getX(), area.getY() - 8.0f);
        for (auto x = 0.0f; x <= 1.001f; x += 0.04f)
            edge.lineTo (area.getX() + x * width,
                         area.getY() + height * (0.02f + (0.2f - 0.09f * static_cast<float> (drape)) * x * x)
                             + std::sin (x * 6.0f + static_cast<float> (drape) * 1.4f - m.time * 0.12f) * height * 0.03f);
        edge.lineTo (area.getRight(), area.getY() - 8.0f);
        edge.closeSubPath();
        lay (g, edge, card.tone (drape == 0 ? 0.3f : 0.08f), 4.0f);
    }

    for (int hill = 0; hill < 2; ++hill)
    {
        juce::Path ground;
        ground.startNewSubPath (area.getX(), area.getBottom());
        for (auto x = 0.0f; x <= 1.001f; x += 0.04f)
            ground.lineTo (area.getX() + x * width,
                           floor + 16.0f + static_cast<float> (hill) * height * 0.09f + std::sin (x * (5.0f + static_cast<float> (hill) * 2.0f) + static_cast<float> (hill) * 2.0f + m.time * 0.15f) * height * 0.035f);
        ground.lineTo (area.getRight(), area.getBottom());
        ground.closeSubPath();
        lay (g, ground, card.tone (hill == 0 ? 0.15f : -0.25f), -3.0f);
    }

    const auto hops = 3 + juce::roundToInt (m.x * 3.0f);
    constexpr auto shrink = 0.76f;
    const auto first = width * 0.86f * (1.0f - shrink) / (1.0f - std::pow (shrink, static_cast<float> (hops)));
    const auto heightOf = [&] (int hop)
    {
        const auto i = static_cast<float> (hop);
        return height * 0.5f * mix (std::pow (0.7f, i), 0.42f * std::pow (1.16f, i), m.y);
    };

    juce::Path arcs, landings;
    auto x = area.getX() + width * 0.07f, span = first;
    std::array<juce::Point<float>, 8> starts;
    std::array<float, 8> spans;

    for (int hop = 0; hop < hops; ++hop)
    {
        const auto index = static_cast<std::size_t> (hop);
        starts[index] = { x, floor };
        spans[index] = span;
        arcs.startNewSubPath (x, floor);
        arcs.quadraticTo (x + span * 0.5f, floor - 2.0f * heightOf (hop), x + span, floor);
        landings.addEllipse (juce::Rectangle<float>().withCentre ({ x + span, floor }).expanded (mix (13.0f, 7.0f, static_cast<float> (hop) / static_cast<float> (hops))));
        x += span;
        span *= shrink;
    }

    juce::Path path;
    juce::PathStrokeType (13.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (path, arcs);
    lay (g, path, card.tone (-0.2f));
    lay (g, landings, card.tone (-0.55f));

    // The ball, taking the hops one after another and squashing as it lands on a cut.
    const auto travelled = fraction (m.time * 0.2f) * static_cast<float> (hops);
    const auto hop = juce::jlimit (0, hops - 1, static_cast<int> (travelled));
    const auto along = travelled - static_cast<float> (hop);
    const auto index = static_cast<std::size_t> (hop);
    const juce::Point<float> ball { starts[index].x + spans[index] * along, floor - 4.0f * heightOf (hop) * along * (1.0f - along) - 17.0f };
    lay (g, disc (ball, 17.0f * (1.0f + 0.25f * m.kick), 17.0f * (1.0f - 0.2f * m.kick)), card.tone (-0.85f), 5.0f);
}

// Tiles in rows. X makes them smaller and more, Y stairs the columns from
// falling to rising, and each cut slips a different set of rows sideways.
void drawGlitch (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (-0.7f));
    const auto columns = 10 + juce::roundToInt (m.x * 10.0f);
    const auto tile = area.getWidth() / static_cast<float> (columns);
    const auto rows = static_cast<int> (std::ceil (area.getHeight() / tile)) + 2;
    const auto settled = ease (juce::jmin (1.0f, (1.0f - m.kick) * 3.0f)); // slips are sudden
    const auto flicker = static_cast<int> (m.time * 5.0f);

    for (int row = -1; row < rows; ++row)
    {
        // Rows near the top and bottom slip furthest; the middle stays calm.
        const auto fromMiddle = std::abs (static_cast<float> (row) - static_cast<float> (rows) * 0.5f + 1.0f) / (static_cast<float> (rows) * 0.5f);
        const auto slip = [&] (int cut)
        {
            return chance (row, cut) < 0.4f ? 0.0f : (chance (row, cut, 1) - 0.5f) * tile * 7.0f * mix (0.15f, 1.0f, fromMiddle);
        };
        const auto dx = mix (slip (m.cuts - 1), slip (m.cuts), settled);

        for (int column = -4; column < columns + 4; ++column)
        {
            const auto stair = std::floor (static_cast<float> (column) / 3.0f - static_cast<float> (columns) / 6.0f) * (m.y - 0.5f) * tile * 0.5f;
            const auto lit = chance (column * 7 + row, flicker, 5) > 0.985f;
            g.setColour (card.tone (lit ? 0.8f : mix (-0.4f, 0.35f, chance (column, row, 9))));
            g.fillRect (juce::Rectangle<float> (area.getX() + static_cast<float> (column) * tile + dx + 1.0f,
                                                area.getY() + static_cast<float> (row) * tile + stair + 1.0f, tile - 2.0f, tile - 2.0f));
        }
    }
}

// A tunnel. X slides it from left to right and Y squeezes it narrower. It
// draws inward all the time, and bursts open on each cut.
void drawSuction (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (0.6f));
    constexpr int rings = 9;
    const juce::Point<float> mouth { area.getX() + area.getWidth() * mix (0.26f, 0.74f, m.x), area.getY() + area.getHeight() * 0.56f };
    const auto reach = std::hypot (area.getWidth(), area.getHeight()) * 0.8f * (1.0f + 0.22f * ease (m.kick));
    const auto squeeze = mix (0.8f, 0.34f, m.y);
    const auto flow = fraction (m.time * 0.11f); // each ring takes the place of the one inside it

    for (int ring = rings + 1; ring >= 1; --ring)
    {
        const auto depth = (static_cast<float> (ring) - flow) / static_cast<float> (rings); // 0 at the mouth's centre
        const auto radius = reach * std::pow (depth, 1.7f);
        const auto centre = mouth + (area.getCentre() - mouth) * juce::jmin (1.0f, depth) * 0.55f;
        sink (g, disc (centre, radius, radius * squeeze), card.tone (mix (-1.0f, 0.42f, std::pow (juce::jmin (1.0f, depth), 0.65f))), 5.0f);
    }
}

// A low-pass filter's response, layer on layer. X moves the cutoff; Y raises
// the resonant peak, which rings at each cut.
void drawFilter (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (0.55f));
    constexpr int layers = 7;
    const auto width = area.getWidth(), height = area.getHeight();
    const auto resonance = mix (0.75f, 7.0f, m.y) * (1.0f + 0.6f * m.kick) * (1.0f + 0.04f * std::sin (m.time * 1.6f));

    for (int layer = 0; layer < layers; ++layer)
    {
        const auto depth = static_cast<float> (layer) / static_cast<float> (layers - 1);
        const auto cutoff = mix (0.16f, 0.84f, m.x) + depth * 0.03f;
        const auto level = area.getY() + height * (0.3f + 0.09f * static_cast<float> (layer));

        juce::Path response;
        response.startNewSubPath (area.getX(), area.getBottom() + 8.0f);

        for (auto x = 0.0f; x <= 1.0f; x += 0.006f)
        {
            // The magnitude of a two-pole low-pass, seven octaves across the art.
            const auto ratio = std::pow (2.0f, (x - cutoff) * 7.0f);
            const auto magnitude = 1.0f / std::sqrt (juce::square (1.0f - ratio * ratio) + juce::square (ratio / resonance));
            const auto decibels = juce::jlimit (-60.0f, 30.0f, 20.0f * std::log10 (magnitude));
            response.lineTo (area.getX() + x * width, level - decibels * height * 0.0085f);
        }

        response.lineTo (area.getRight(), area.getBottom() + 8.0f);
        response.closeSubPath();
        lay (g, response, card.tone (mix (0.25f, -0.9f, depth)), -3.0f);
    }
}

// Two reels and the tape between them. X leaves the tape slacker and Y hangs
// it in more loops. At each cut the reels brake and the tape sags, before
// they spin back up.
void drawTape (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (0.35f));
    const auto width = area.getWidth(), height = area.getHeight();
    const auto radius = height * 0.3f;
    const std::array<juce::Point<float>, 2> hubs { juce::Point<float> { area.getX() + width * 0.25f, area.getY() + height * 0.43f },
                                                    juce::Point<float> { area.getX() + width * 0.75f, area.getY() + height * 0.43f } };

    // The tape hangs between the reels in loops: deeper with X, more of them with Y, and slackest just after a cut.
    const auto slack = height * mix (0.07f, 0.24f, m.x) * mix (0.6f, 1.0f, ease (m.kick));
    const auto loops = 1 + juce::roundToInt (m.y * 3.0f);
    const juce::Point<float> from { hubs[0].x - radius * 0.1f, hubs[0].y + radius * 0.82f }, to { hubs[1].x + radius * 0.1f, hubs[1].y + radius * 0.82f };
    juce::Path run, tape, rollers;
    run.startNewSubPath (from);

    for (int loop = 0; loop < loops; ++loop)
    {
        const auto end = from + (to - from) * (static_cast<float> (loop + 1) / static_cast<float> (loops));
        const auto middle = from + (to - from) * ((static_cast<float> (loop) + 0.5f) / static_cast<float> (loops));
        run.quadraticTo (middle.translated (0.0f, radius * 0.4f + 2.0f * slack * (1.0f + 0.06f * std::sin (m.time * 0.9f + static_cast<float> (loop)))), end);

        if (loop + 1 < loops) // a roller between each loop and the next
            rollers.addEllipse (juce::Rectangle<float>().withCentre (end.translated (0.0f, 7.0f)).expanded (9.0f));
    }

    juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (tape, run);
    lay (g, tape, card.tone (-0.6f), 5.0f);
    lay (g, rollers, card.tone (0.55f), 2.0f);

    for (std::size_t reel = 0; reel < hubs.size(); ++reel)
    {
        const auto hub = hubs[reel];
        const auto angle = m.spin + static_cast<float> (reel) * 0.4f;
        lay (g, disc (hub, radius * (reel == 0 ? 0.93f : 0.86f)), card.tone (-0.6f)); // the tape wound on it

        juce::Path flange;
        flange.addEllipse (juce::Rectangle<float>().withCentre (hub).expanded (radius));
        for (int window = 0; window < 3; ++window)
        {
            const auto direction = angle + static_cast<float> (window) * 2.0f * pi / 3.0f;
            flange.addEllipse (juce::Rectangle<float>().withCentre (hub + juce::Point<float> { std::cos (direction), std::sin (direction) } * radius * 0.55f).expanded (radius * 0.27f));
        }
        flange.setUsingNonZeroWinding (false); // the windows are cut out of the flange
        lay (g, flange, card.tone (reel == 0 ? 0.0f : 0.12f), 4.0f);
        lay (g, disc (hub, radius * 0.15f), card.tone (0.55f), 2.0f);
    }
}

// Rings inside rings, each turning twice as fast as the one around it. X
// shrinks the loop, and Y nests more loops inside it.
void drawLoops (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (-0.55f));
    const auto height = area.getHeight();
    const auto nest = [&] (juce::Point<float> centre, float outer, int levels, float turn)
    {
        for (int level = 0; level < levels; ++level)
        {
            const auto radius = outer * std::pow (0.72f, static_cast<float> (level)) * (1.0f + 0.05f * m.kick);
            lay (g, disc (centre, radius), card.tone (mix (-0.3f, 0.75f, static_cast<float> (level) / 5.0f)), 4.0f);

            // A slot cut in each ring marks where its loop begins.
            const auto angle = turn * std::pow (2.0f, static_cast<float> (level)) + static_cast<float> (level) * 0.9f;
            juce::Path slot;
            slot.addPieSegment (juce::Rectangle<float>().withCentre (centre).expanded (radius * 0.97f), angle - 0.09f, angle + 0.09f, 0.76f);
            g.setColour (card.tone (-0.85f));
            g.fillPath (slot);
        }
    };

    const auto levels = 2 + juce::roundToInt (m.y * 4.0f);
    nest ({ area.getX() + area.getWidth() * 0.9f, area.getY() + height * 0.14f }, height * 0.34f, 4, -m.time * 0.05f);
    nest ({ area.getX() + area.getWidth() * 0.1f, area.getBottom() - height * 0.08f }, height * 0.42f, 4, m.time * 0.04f);
    nest (area.getCentre().translated (area.getWidth() * 0.02f, 0.0f), height * mix (0.6f, 0.4f, m.x), levels, m.time * 0.1f);
}

// Wedges racing to one point. X lifts the point from a falling drag to a
// rising rush, Y lets the fan start further back, and each cut lands on it.
void drawRush (juce::Graphics& g, juce::Rectangle<float> area, const Card& card, const ArtMotion& m)
{
    g.fillAll (card.tone (0.2f));
    constexpr int wedges = 15;
    const auto height = area.getHeight();
    const juce::Point<float> point { area.getRight() - area.getWidth() * 0.035f, area.getY() + height * mix (0.66f, 0.34f, m.x) };
    const auto start = area.getX() + area.getWidth() * mix (0.5f, 0.0f, m.y) - 20.0f;
    const auto rimAt = [&] (int index)
    {
        const auto spread = static_cast<float> (index) / static_cast<float> (wedges) + std::sin (m.time * 0.7f + static_cast<float> (index) * 0.9f) * 0.004f;
        return juce::Point<float> { start, area.getY() + height * mix (-0.45f, 1.45f, spread) };
    };

    for (int wedge = 0; wedge < wedges; ++wedge)
    {
        juce::Path piece;
        piece.addTriangle (rimAt (wedge), rimAt (wedge + 1), point);
        lay (g, piece, card.tone (mix (-0.4f, 0.6f, chance (wedge, 5))), 2.0f);
    }

    // Darts along the seams, gathering speed as they close on the point.
    for (int seam = 1; seam < wedges; ++seam)
    {
        const auto along = std::pow (fraction (m.time * 0.3f + chance (seam, 9)), 2.4f);
        const auto from = rimAt (seam), at = from + (point - from) * along;
        const auto tail = from + (point - from) * juce::jmax (0.0f, along - 0.05f * (1.0f - along) - 0.01f);
        g.setColour (card.tone (-0.9f).withAlpha (juce::jmin (1.0f, 4.0f * along) * (1.0f - along)));
        g.drawLine ({ tail, at }, 3.0f);
    }

    if (m.kick > 0.0f)
    {
        g.setColour (card.tone (0.9f).withAlpha (m.kick));
        g.fillEllipse (juce::Rectangle<float>().withCentre (point).expanded (8.0f + 46.0f * (1.0f - m.kick)));
    }
}
} // namespace

void drawProgramArt (juce::Graphics& g, int program, juce::Rectangle<float> area, const ArtMotion& motion)
{
    using Scene = void (*) (juce::Graphics&, juce::Rectangle<float>, const Card&, const ArtMotion&);
    constexpr std::array<Scene, 8> scenes { drawCut, drawBounce, drawGlitch, drawSuction, drawFilter, drawTape, drawLoops, drawRush };

    jassert (program >= 0 && program < static_cast<int> (scenes.size()));
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());
    scenes[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (scenes.size()) - 1, program))] (g, area, { programColour (program) }, motion);
}

void drawScissors (juce::Graphics& g, juce::Rectangle<float> area, float open)
{
    // Drawn in a unit square, pointing left, then fitted to the area.
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (size).translated (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f));

    const juce::Point<float> pivot { 0.6f, 0.5f };
    const auto spread = juce::degreesToRadians (mix (5.0f, 17.0f, juce::jlimit (0.0f, 1.0f, open)));
    const juce::Colour tape { 0xffe9dcb4 }, steel { 0xff9d9d9a }, shade { 0xff7b7b78 };
    const auto piece = [&] (const juce::Path& path, juce::Colour colour)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillPath (path, juce::AffineTransform::translation (0.004f, 0.012f));
        g.setColour (colour);
        g.fillPath (path);
    };
    const auto ribbon = [] (const juce::Path& line)
    {
        juce::Path band;
        juce::PathStrokeType (0.085f).createStrokedPath (band, line);
        return band;
    };

    // One half of the scissors: a blade to the left of the pivot and, rigid with it, a handle to the right.
    const auto half = [&] (float turn, juce::Colour blade)
    {
        const auto splay = turn > 0.0f ? 0.42f : -0.42f; // the handles stand wider apart than the blades
        const juce::Point<float> loop { pivot.x + 0.26f, pivot.y };

        juce::Path edge, neck, handle;
        edge.addTriangle (pivot.x + 0.06f, pivot.y - 0.062f, pivot.x - 0.5f, pivot.y, pivot.x + 0.06f, pivot.y + 0.062f);
        neck.addQuadrilateral (pivot.x, pivot.y - 0.045f, loop.x - 0.125f, loop.y - 0.05f, loop.x - 0.125f, loop.y + 0.05f, pivot.x, pivot.y + 0.045f);
        handle.addEllipse (loop.x - 0.15f, loop.y - 0.1f, 0.3f, 0.2f);
        handle.addEllipse (loop.x - 0.105f, loop.y - 0.058f, 0.21f, 0.116f);
        handle.setUsingNonZeroWinding (false); // a ring

        edge.applyTransform (juce::AffineTransform::rotation (turn, pivot.x, pivot.y));
        for (auto* part : { &neck, &handle })
            part->applyTransform (juce::AffineTransform::rotation (turn + splay, pivot.x, pivot.y));

        piece (edge, blade);
        piece (neck, palette::accent);
        piece (handle, palette::accent);
    };

    // The uncut tape runs in from the right; past the blades its two halves curl away.
    juce::Path uncut, upper, lower;
    uncut.startNewSubPath (1.06f, 0.5f);
    uncut.lineTo (0.42f, 0.5f);
    upper.startNewSubPath (0.44f, 0.5f);
    upper.quadraticTo (0.26f, 0.47f, 0.02f, 0.2f + 0.03f * open);
    lower.startNewSubPath (0.44f, 0.5f);
    lower.quadraticTo (0.25f, 0.55f, 0.0f, 0.84f - 0.03f * open);

    half (-spread, shade);
    piece (ribbon (lower), tape.darker (0.08f));
    piece (ribbon (uncut), tape);
    piece (ribbon (upper), tape);
    half (spread, steel);
    piece (disc (pivot, 0.028f), steel.brighter (0.35f));
}

} // namespace livecut::ui
