// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/Theme.h"

#include "LivecutAssets.h"

#include <array>

namespace livecut::ui
{

namespace
{
// Bricolage Grotesque is one variable font. Each style the skin uses is one
// instance of it, made once and shared by every editor in the process.
struct Typefaces
{
    juce::Typeface::Ptr instance (float wght, float wdth, float opsz) const
    {
        if (family == nullptr)
            return {};

        // Windows before 10 1709 can't make instances of a variable font. There every style is the font as it comes.
        const std::array settings { juce::FontVariableSetting { "wght", wght },
                                    juce::FontVariableSetting { "wdth", wdth },
                                    juce::FontVariableSetting { "opsz", opsz } };
        const auto face = family->cloneWithVariableSettings (settings);
        return face != nullptr ? face : family;
    }

    juce::Typeface::Ptr family = juce::Typeface::createSystemTypefaceFor (assets::BricolageGrotesque_ttf,
                                                                           static_cast<size_t> (assets::BricolageGrotesque_ttfSize));
    std::array<juce::Typeface::Ptr, 4> text { instance (weight::regular, 100.0f, 14.0f), instance (weight::medium, 100.0f, 14.0f),
                                              instance (weight::semibold, 100.0f, 14.0f), instance (weight::bold, 100.0f, 14.0f) };
    juce::Typeface::Ptr display = instance (800.0f, 75.0f, 48.0f);
};

constexpr int programCount = 8;

std::size_t programIndex (int program)
{
    jassert (program >= 0 && program < programCount);
    return static_cast<std::size_t> (juce::jlimit (0, programCount - 1, program));
}

// If the system won't load the font at all, as a policy against fonts from memory can make it, text is set in the system's own.
juce::FontOptions options (const juce::Typeface::Ptr& face, float height)
{
    return face != nullptr ? juce::FontOptions (face).withHeight (height) : juce::FontOptions (height);
}

juce::FontOptions textOptions (float height, float weight)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    const auto index = juce::jlimit (0, 3, juce::roundToInt ((weight - weight::regular) / 100.0f));
    return options (faces->text[static_cast<std::size_t> (index)], height);
}

} // namespace

juce::Font uiFont (float height, float weight)
{
    return juce::Font (textOptions (height, weight));
}

juce::Font numberFont (float height)
{
    return juce::Font (textOptions (height, weight::regular).withFeatureEnabled ("tnum"));
}

juce::Font displayFont (float height)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return juce::Font (options (faces->display, height));
}

std::shared_ptr<void> keepTypeface()
{
    return std::make_shared<juce::SharedResourcePointer<Typefaces>>();
}

bool isSkinFont (const juce::Font& font)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return faces->family != nullptr && font.getTypefaceName() == faces->family->getName();
}

juce::Colour programColour (int program)
{
    // Round the wheel from Cut's orange, each light enough for ink to read on it.
    constexpr std::array<juce::uint32, programCount> colours { 0xffff6b1a, 0xfffa6e8c, 0xffec5fb3, 0xffab70f0,
                                                    0xff48a4f2, 0xff34c9c5, 0xff6cd04f, 0xfffcd21c };
    return juce::Colour (colours[programIndex (program)]);
}

void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area)
{
    // CUT drops by this much of the height, so the letters are set that much shorter.
    constexpr auto drop = 0.12f;
    const auto capHeight = area.getHeight() * (1.0f - drop);
    const auto font = displayFont (capHeight / 0.7f); // Bricolage's capitals are 70% of its height
    const auto baseline = area.getY() + capHeight;

    // Set as one word for its spacing, then as the two the blade will part.
    juce::GlyphArrangement glyphs, liveGlyphs, cutGlyphs;
    glyphs.addLineOfText (font, "LIVECUT", area.getX(), baseline);
    liveGlyphs.addLineOfText (font, "LIVE", area.getX(), baseline);
    cutGlyphs.addLineOfText (font, "CUT", glyphs.getGlyph (4).getLeft(), baseline);
    juce::Path live, cut;
    liveGlyphs.createPath (live);
    cutGlyphs.createPath (cut);

    // The blade passes between the E and the C, leaning like a forward slash,
    // and shaves whatever of either word crosses its line.
    const auto gap = (glyphs.getGlyph (3).getRight() + glyphs.getGlyph (4).getLeft()) * 0.5f;
    const auto middle = baseline - capHeight * 0.5f;
    const auto top = area.getY() - capHeight, bottom = area.getBottom() + capHeight;
    const auto bladeAt = [&] (float y) { return gap + capHeight * 0.03f + 0.2f * (middle - y); };

    const auto piece = [&] (const juce::Path& letters, float outerEdge, juce::AffineTransform offset)
    {
        juce::Path side;
        side.addQuadrilateral (bladeAt (top), top, outerEdge, top, outerEdge, bottom, bladeAt (bottom), bottom);
        const juce::Graphics::ScopedSaveState state (g);
        g.addTransform (offset);
        g.reduceClipRegion (side);
        g.fillPath (letters);
    };

    piece (live, area.getX() - capHeight, {});
    piece (cut, cut.getBounds().getRight() + capHeight, juce::AffineTransform::translation (0.0f, area.getHeight() * drop));
}

} // namespace livecut::ui
