// SPDX-License-Identifier: AGPL-3.0-or-later
#include "theme.h"

#include <embedded/hba_fonts.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lg::ui {

// ---------------------------------------------------------------------------------------------------------------
// Context

void Context::focusOn(ItemRef item) {
    focus = item;
    follow = false;
    sel = {SelKind::item, item, 0, sel.role};
    changed();
}

void Context::selectChord(ItemRef item, int span) {
    focus = item;
    sel = {SelKind::chord, item, span, sel.role};
    changed();
}

void Context::selectPart(Role role) {
    sel = {SelKind::part, focus, 0, role};
    changed();
}

void Context::edit(const std::function<void(Document&)>& change) {
    model.edit(change);
    changed();
    validate(); // before anything draws: a removed section or a shorter pattern must not be indexed
}

namespace {
bool validItem(const Song& song, ItemRef item) {
    const int n = item.isBridge() ? static_cast<int>(song.bridges.size()) : static_cast<int>(song.sections.size());
    return item.index >= 0 && item.index < n;
}
} // namespace

void Context::validate() {
    const Song& s = song();
    if (!validItem(s, focus)) {
        focus = ItemRef::section(0);
        changed();
    }
    if (sel.kind != SelKind::part && !validItem(s, sel.item)) {
        sel = {SelKind::item, focus, 0, sel.role};
        changed();
    }
    if (sel.kind == SelKind::chord) {
        const auto& pattern = doc().pattern(sel.item);
        if (sel.item.isBridge() || sel.chord < 0 || sel.chord >= static_cast<int>(pattern.spans.size())) {
            sel = {SelKind::item, sel.item, 0, sel.role};
            changed();
        }
    }
}

void Context::sectionRemoved(int removed, int oldCount) {
    if (static_cast<int>(song().sections.size()) != oldCount - 1)
        return; // the edit did not happen (a follower ignores edits)
    const int n = oldCount;
    const int left = n - 1;
    const auto remap = [&](ItemRef item) {
        if (!item.isBridge()) {
            const int i = item.index > removed ? item.index - 1 : std::min(item.index, left - 1);
            return ItemRef::section(std::max(0, i));
        }
        if (left < 2)
            return ItemRef::section(std::min(removed, left - 1));
        // Bridge b ran from section b to b + 1. One that touched the removed section becomes the new bridge that
        // joins its neighbours, which starts at the section before it.
        int from = item.index;
        if (from == removed || (from + 1) % n == removed)
            from = (removed - 1 + n) % n;
        return ItemRef::bridge(from > removed ? from - 1 : from);
    };
    focus = remap(focus);
    if (sel.kind != SelKind::part) {
        const bool chord = sel.kind == SelKind::chord && !sel.item.isBridge() && sel.item.index != removed;
        sel = {chord ? SelKind::chord : SelKind::item, remap(sel.item), chord ? sel.chord : 0, sel.role};
    } else {
        sel.item = focus;
    }
    changed();
    validate();
}

void Context::songLoaded() {
    loadsSeen = doc().loadCount();
    focus = ItemRef::section(0);
    sel = {SelKind::chord, focus, 1, sel.role};
    follow = true;
    changed();
    validate();
}

void Context::followTo(ItemRef playing) {
    if (!follow || playing == focus || !validItem(song(), playing))
        return;
    focus = playing;
    if (sel.kind != SelKind::part)
        sel = {SelKind::item, playing, 0, sel.role};
    changed();
}

// ---------------------------------------------------------------------------------------------------------------
// Colour

namespace colour {

namespace {
float toSrgb(float linear) {
    const float v = std::clamp(linear, 0.0f, 1.0f);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

unsigned channel(float v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
} // namespace

unsigned oklch(float lightness, float chroma, float hueDegrees, float alpha) {
    const float h = hueDegrees * 3.14159265f / 180.0f;
    const float a = chroma * std::cos(h);
    const float b = chroma * std::sin(h);
    const float l_ = lightness + 0.3963377774f * a + 0.2158037573f * b;
    const float m_ = lightness - 0.1055613458f * a - 0.0638541728f * b;
    const float s_ = lightness - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    const float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    const float bl = -0.0041960863f * l - 0.7034186147f * m + 1.7076086168f * s;
    return (channel(alpha) << 24) | (channel(toSrgb(r)) << 16) | (channel(toSrgb(g)) << 8) | channel(toSrgb(bl));
}

unsigned role(Role r) {
    static const unsigned chords = oklch(0.80f, 0.11f, 215.0f);
    static const unsigned bass = oklch(0.79f, 0.15f, 150.0f);
    static const unsigned melody = oklch(0.85f, 0.14f, 78.0f);
    static const unsigned arp = oklch(0.75f, 0.16f, 352.0f);
    switch (r) {
    case Role::chords:
        return chords;
    case Role::bass:
        return bass;
    case Role::melody:
        return melody;
    case Role::arp:
        return arp;
    default:
        return text;
    }
}

unsigned keyColour(Key k, float lightness, float chroma, float alpha) {
    return oklch(lightness, chroma, keyHue(k), alpha);
}

unsigned mix(unsigned a, unsigned b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto lerp = [t](unsigned x, unsigned y) {
        return static_cast<unsigned>(
            std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return (lerp((a >> 24) & 0xffu, (b >> 24) & 0xffu) << 24) | (lerp((a >> 16) & 0xffu, (b >> 16) & 0xffu) << 16) |
           (lerp((a >> 8) & 0xffu, (b >> 8) & 0xffu) << 8) | lerp(a & 0xffu, b & 0xffu);
}

unsigned gold() {
    static const unsigned value = oklch(0.86f, 0.13f, 85.0f);
    return value;
}

unsigned stutter() {
    static const unsigned value = oklch(0.76f, 0.14f, 295.0f);
    return value;
}

} // namespace colour

// ---------------------------------------------------------------------------------------------------------------
// Text

namespace {
const visage::EmbeddedFile& fontFile(Weight weight) {
    return weight == Weight::semibold ? hba::ui::fonts::Inter_SemiBold_ttf
           : weight == Weight::medium ? hba::ui::fonts::Inter_Medium_ttf
                                      : hba::ui::fonts::Inter_Regular_ttf;
}

// Measured at 100 px and scaled: outline fonts advance linearly with size.
constexpr float kMeasureSize = 100.0f;
constexpr float kCapHeight = 0.7275f; // Inter: 1490 / 2048 units per em

const visage::Font& measureFont(Weight weight) {
    static const auto* regular = new visage::Font(kMeasureSize, fontFile(Weight::regular), 1.0f);
    static const auto* medium = new visage::Font(kMeasureSize, fontFile(Weight::medium), 1.0f);
    static const auto* semibold = new visage::Font(kMeasureSize, fontFile(Weight::semibold), 1.0f);
    return weight == Weight::semibold ? *semibold : weight == Weight::medium ? *medium : *regular;
}
} // namespace

float textWidth(std::string_view text, float size, Weight weight) {
    if (text.empty())
        return 0.0f;
    const auto utf32 = visage::String::convertToUtf32(std::string(text));
    return measureFont(weight).stringWidth(utf32) * size / kMeasureSize;
}

std::string ellipsize(std::string_view text, float size, Weight weight, float width) {
    if (textWidth(text, size, weight) <= width)
        return std::string(text);
    auto utf32 = visage::String::convertToUtf32(std::string(text));
    const std::u32string dots = U"…";
    while (!utf32.empty()) {
        utf32.pop_back();
        while (!utf32.empty() && utf32.back() == U' ')
            utf32.pop_back();
        const auto candidate = utf32 + dots;
        if (measureFont(weight).stringWidth(candidate) * size / kMeasureSize <= width)
            return visage::String::convertToUtf8(candidate);
    }
    return {};
}

std::vector<std::string> wrapText(std::string_view text, float size, Weight weight, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ')
            ++i;
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ')
            ++j;
        if (j == i)
            break;
        const std::string word(text.substr(i, j - i));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(candidate, size, weight) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        i = j;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}

float spacedWidth(std::string_view str, float size, Weight weight, float spacingEm) {
    const auto utf32 = visage::String::convertToUtf32(std::string(str));
    if (utf32.empty())
        return 0.0f;
    return measureFont(weight).stringWidth(utf32) * size / kMeasureSize +
           static_cast<float>(utf32.size() - 1) * spacingEm * size;
}

// ---------------------------------------------------------------------------------------------------------------
// Paint

void Paint::rect(unsigned argb, float x, float y, float w, float h, float radius) const {
    canvas.setColor(argb);
    if (radius > 0.0f)
        canvas.roundedRectangle(px(x), px(y), px(w), px(h), px(radius));
    else
        canvas.rectangle(px(x), px(y), px(w), px(h));
}

void Paint::border(unsigned argb, float x, float y, float w, float h, float radius, float thickness) const {
    canvas.setColor(argb);
    canvas.roundedRectangleBorder(px(x), px(y), px(w), px(h), px(radius), std::max(1.0f, px(thickness)));
}

void Paint::hgrad(unsigned left, unsigned right, float x, float y, float w, float h, float radius) const {
    hgrad(visage::Gradient(left, right), x, y, w, h, radius);
}

void Paint::hgrad(const visage::Gradient& gradient, float x, float y, float w, float h, float radius) const {
    canvas.setColor(visage::Brush::linear(gradient, {px(x), px(y)}, {px(x + w), px(y)}));
    if (radius > 0.0f)
        canvas.roundedRectangle(px(x), px(y), px(w), px(h), px(radius));
    else
        canvas.rectangle(px(x), px(y), px(w), px(h));
}

void Paint::vgrad(unsigned top, unsigned bottom, float x, float y, float w, float h, float radius) const {
    canvas.setColor(visage::Brush::linear(visage::Gradient(top, bottom), {px(x), px(y)}, {px(x), px(y + h)}));
    if (radius > 0.0f)
        canvas.roundedRectangle(px(x), px(y), px(w), px(h), px(radius));
    else
        canvas.rectangle(px(x), px(y), px(w), px(h));
}

void Paint::circle(unsigned argb, float cx, float cy, float radius) const {
    canvas.setColor(argb);
    canvas.circle(px(cx - radius), px(cy - radius), px(2.0f * radius));
}

void Paint::ring(unsigned argb, float cx, float cy, float radius, float thickness) const {
    canvas.setColor(argb);
    const float t = std::max(1.0f, px(thickness));
    canvas.ring(px(cx - radius) - 0.5f * t, px(cy - radius) - 0.5f * t, px(2.0f * radius) + t, t);
}

void Paint::line(unsigned argb, float x1, float y1, float x2, float y2, float thickness) const {
    canvas.setColor(argb);
    if (x1 == x2 || y1 == y2) {
        const float t = std::max(1.0f, px(thickness));
        if (x1 == x2)
            canvas.rectangle(px(x1) - 0.5f * t, px(std::min(y1, y2)), t, px(std::abs(y2 - y1)));
        else
            canvas.rectangle(px(std::min(x1, x2)), px(y1) - 0.5f * t, px(std::abs(x2 - x1)), t);
        return;
    }
    canvas.segment(px(x1), px(y1), px(x2), px(y2), std::max(1.0f, px(thickness)), true);
}

void Paint::stroke(const visage::Path& designPath, unsigned argb, float thickness, std::vector<float> dash) const {
    visage::Path scaled = designPath.scaled(s);
    for (float& d : dash)
        d *= s;
    canvas.setColor(argb);
    canvas.stroke(scaled, 0.0f, 0.0f, canvas.width(), canvas.height(), std::max(0.75f, px(thickness)),
                  visage::Path::Join::Round, visage::Path::EndCap::Butt, std::move(dash));
}

void Paint::text(std::string_view str, float size, Weight weight, unsigned argb, float x, float y, float w, float h,
                 visage::Font::Justification justification) const {
    if (str.empty())
        return;
    canvas.setColor(argb);
    canvas.text(std::string(str), hba::ui::fontPx(px(size), weight), justification, px(x), px(y), px(w), px(h));
}

void Paint::textAt(std::string_view str, float size, Weight weight, unsigned argb, float x, float baseline,
                   visage::Font::Justification justification, float width) const {
    if (str.empty())
        return;
    const int horizontal = justification & (visage::Font::kLeft | visage::Font::kRight);
    float left = x;
    if (horizontal == 0)
        left = x - 0.5f * width;
    else if (horizontal == visage::Font::kRight)
        left = x - width;
    // Visage centres a line on its cap height: baseline = top + (height + capHeight) / 2. A box that reaches well
    // below the baseline keeps descenders from being clipped.
    const auto j = static_cast<visage::Font::Justification>(horizontal);
    canvas.setColor(argb);
    const float h = 2.0f * size;
    const float top = baseline - 0.5f * (h + kCapHeight * size);
    canvas.text(std::string(str), hba::ui::fontPx(px(size), weight), j, px(left), px(top), px(width), px(h));
}

float Paint::spaced(std::string_view str, float size, Weight weight, unsigned argb, float x, float baseline,
                    float spacingEm) const {
    const auto utf32 = visage::String::convertToUtf32(std::string(str));
    float pen = x;
    for (char32_t c : utf32) {
        const std::u32string one(1, c);
        const std::string glyph = visage::String::convertToUtf8(one);
        textAt(glyph, size, weight, argb, pen, baseline, visage::Font::kLeft, 4.0f * size);
        pen += measureFont(weight).stringWidth(one) * size / kMeasureSize + spacingEm * size;
    }
    return pen - x - (utf32.empty() ? 0.0f : spacingEm * size);
}

void Paint::dashedRoundRect(unsigned argb, float x, float y, float w, float h, float radius, float thickness,
                            float dash, float gap) const {
    visage::Path path;
    path.addRoundedRectangle(x, y, w, h, radius, radius);
    stroke(path, argb, thickness, {dash, gap});
}

void Paint::playIcon(unsigned argb, float cx, float cy, float size) const {
    // M4.5 2.8v10.4L13 8z in a 16-unit box
    const float u = size / 16.0f;
    const float x0 = cx - 8.0f * u;
    const float y0 = cy - 8.0f * u;
    canvas.setColor(argb);
    canvas.triangle(px(x0 + 4.5f * u), px(y0 + 2.8f * u), px(x0 + 4.5f * u), px(y0 + 13.2f * u), px(x0 + 13.0f * u),
                    px(y0 + 8.0f * u));
}

void Paint::stopIcon(unsigned argb, float cx, float cy, float size) const {
    const float u = size / 16.0f;
    rect(argb, cx - 4.5f * u, cy - 4.5f * u, 9.0f * u, 9.0f * u, 1.5f * u);
}

void Paint::diceIcon(unsigned argb, float cx, float cy, float size) const {
    const float u = size / 16.0f;
    border(argb, cx - 5.8f * u, cy - 5.8f * u, 11.6f * u, 11.6f * u, 2.8f * u, 1.4f * u);
    circle(argb, cx - 2.4f * u, cy - 2.4f * u, 1.15f * u);
    circle(argb, cx, cy, 1.15f * u);
    circle(argb, cx + 2.4f * u, cy + 2.4f * u, 1.15f * u);
}

// ---------------------------------------------------------------------------------------------------------------
// Words

std::string itemName(const Song& song, ItemRef item) {
    if (item.isBridge()) {
        if (item.index < 0 || item.index >= static_cast<int>(song.bridges.size()))
            return {};
        return std::string(bridgeRoute(song, item.index).name);
    }
    if (item.index < 0 || item.index >= static_cast<int>(song.sections.size()))
        return {};
    return song.sections[static_cast<std::size_t>(item.index)].name;
}

std::string itemLetters(const Song& song, ItemRef item) {
    if (!item.isBridge())
        return sectionLetter(item.index);
    if (item.index < 0 || item.index >= static_cast<int>(song.bridges.size()))
        return {};
    const auto& b = song.bridges[static_cast<std::size_t>(item.index)];
    return sectionLetter(b.from) + " " + std::string(kArrow) + " " + sectionLetter(b.to);
}

Route bridgeRoute(const Song& song, int bridge) {
    const auto& b = song.bridges[static_cast<std::size_t>(bridge)];
    return routeOrFirst(song.sections[static_cast<std::size_t>(b.from)].key,
                        song.sections[static_cast<std::size_t>(b.to)].key, b.route);
}

std::string numeralText(const Pattern& pattern, const Span& span) {
    if (pattern.bridge)
        return span.analysis.numeral + " " + std::string(kArrow) + " " + span.toAnalysis.numeral;
    return span.analysis.numeral;
}

std::string functionText(Function f) {
    switch (f) {
    case Function::home:
        return "Home. Settled; the chord the loop returns to.";
    case Function::away:
        return "Away. Moves off home without pulling hard.";
    case Function::pull:
        return "Pull. Wants to resolve home.";
    case Function::colour:
        return "Colour. Borrowed from outside the key.";
    }
    return {};
}

std::string formatBpm(double bpm) {
    char text[32];
    if (std::abs(bpm - std::round(bpm)) < 0.05)
        std::snprintf(text, sizeof text, "%d", static_cast<int>(std::lround(bpm)));
    else
        std::snprintf(text, sizeof text, "%.1f", bpm);
    return text;
}

const Span* previousSpan(Document& doc, const Pattern& pattern, int span) {
    const int n = static_cast<int>(pattern.spans.size());
    if (span < 0 || span >= n)
        return nullptr;
    if (pattern.bridge && span == 0) {
        const auto& song = doc.song();
        if (pattern.item.index < 0 || pattern.item.index >= static_cast<int>(song.bridges.size()))
            return nullptr;
        const auto& from =
            doc.pattern(ItemRef::section(song.bridges[static_cast<std::size_t>(pattern.item.index)].from));
        return from.spans.empty() ? nullptr : &from.spans.back();
    }
    if (n < 2)
        return nullptr;
    return &pattern.spans[static_cast<std::size_t>((span - 1 + n) % n)];
}

} // namespace lg::ui
