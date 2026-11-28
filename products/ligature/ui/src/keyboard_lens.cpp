// SPDX-License-Identifier: AGPL-3.0-or-later
#include "keyboard_lens.h"

#include <algorithm>

namespace lg::ui {

namespace {
constexpr int kLowest = 36;  // C2
constexpr int kHighest = 96; // C7

bool isBlack(int midi) {
    const int pc = mod12(midi);
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

bool sounding(const std::array<std::uint64_t, 2>& mask, int midi) {
    return ((mask[static_cast<std::size_t>(midi / 64)] >> static_cast<unsigned>(midi % 64)) & 1u) != 0;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Keyboard

void KeyboardPanel::live() {
    const auto& t = context_.model.telemetry();
    if (t.sounding != shown_) {
        shown_ = t.sounding;
        redraw();
    }
}

void KeyboardPanel::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    chrome(p);
    const auto& masks = context_.model.telemetry().sounding;
    // The part that lights a key: melody over arp over chords over bass.
    const auto lit = [&](int midi) -> unsigned {
        for (Role r : {Role::melody, Role::arp, Role::chords, Role::bass})
            if (sounding(masks[static_cast<std::size_t>(r)], midi))
                return colour::role(r);
        return 0;
    };

    const float x0 = 10.0f;
    const float w = designWidth() - 20.0f;
    const float h = 58.0f;
    const float y0 = (designHeight() - 62.0f) * 0.5f;
    int whites = 0;
    for (int m = kLowest; m <= kHighest; ++m)
        whites += isBlack(m) ? 0 : 1;
    const float kw = w / static_cast<float>(whites);

    int wi = 0;
    for (int m = kLowest; m <= kHighest; ++m) {
        if (isBlack(m))
            continue;
        const float x = x0 + static_cast<float>(wi) * kw;
        const unsigned on = lit(m);
        p.rect(on != 0 ? on : colour::whiteKey, x + 0.5f, y0, kw - 1.0f, h, 2.5f);
        if (mod12(m) == 0)
            p.textAt("C" + std::to_string(m / 12 - 1), 8.0f, Weight::semibold, 0xff6b7785u, x + 3.0f, y0 + h - 5.0f);
        ++wi;
    }
    wi = 0;
    for (int m = kLowest; m <= kHighest; ++m) {
        if (!isBlack(m)) {
            ++wi;
            continue;
        }
        const unsigned on = lit(m);
        p.rect(on != 0 ? on : colour::blackKey, x0 + static_cast<float>(wi) * kw - kw * 0.3f, y0, kw * 0.6f, h * 0.6f,
               2.0f);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Lens

LensCard::Shown LensCard::current() const {
    const auto& t = context_.model.telemetry();
    Shown s;
    if (t.playing) {
        s.playing = true;
        s.item = t.performance.item;
        auto& pattern = context_.doc().pattern(t.performance.item, t.performance.fit);
        s.span = pattern.spanAt(t.stepInItem);
        return s;
    }
    const auto& sel = context_.sel;
    if (sel.kind == SelKind::chord && !sel.item.isBridge()) {
        s.item = sel.item;
        s.span = sel.chord;
    } else {
        s.item = context_.focus;
        s.span = 0;
    }
    return s;
}

void LensCard::sync() { setVisible(context_.lens); }

void LensCard::live() {
    const auto now = current();
    if (!(now == shown_)) {
        shown_ = now;
        redraw();
    }
}

void LensCard::draw(visage::Canvas& canvas) {
    const Paint p{canvas, context_.scale};
    chrome(p);
    const Shown s = current();
    auto& doc = context_.doc();
    const auto& t = context_.model.telemetry();
    auto& pattern = doc.pattern(s.item, s.playing ? t.performance.fit : 0);
    if (s.span < 0 || s.span >= static_cast<int>(pattern.spans.size()))
        return;
    const auto& span = pattern.spans[static_cast<std::size_t>(s.span)];
    const float w = designWidth() - 28.0f;

    const std::string key = pattern.bridge
                                ? keyShort(pattern.key) + " " + std::string(kArrow) + " " + keyShort(pattern.toKey)
                                : keyName(pattern.key);
    p.spaced(std::string(s.playing ? "NOW PLAYING" : "SELECTED") + " · " + key, 9.5f, Weight::semibold,
             colour::textFaint, 14.0f, 19.0f, 0.09f);

    const std::string name = chordName(span.chord);
    p.textAt(name, 17.0f, Weight::semibold, colour::text, 14.0f, 40.0f);
    p.textAt(numeralText(pattern, span), 12.0f, Weight::semibold, colour::textDim,
             14.0f + textWidth(name, 17.0f, Weight::semibold) + 8.0f, 40.0f);

    std::string text =
        span.pivot ? "Pivot. Belongs to both keys, so the key can turn here." : functionText(span.analysis.function);
    if (const Span* prev = previousSpan(doc, pattern, s.span); prev != nullptr && prev != &span) {
        const auto shared = sharedPcs(span.chord, prev->chord);
        if (!shared.empty()) {
            std::string notes;
            for (std::size_t i = 0; i < shared.size(); ++i)
                notes += (i > 0 ? ", " : "") + pcName(shared[i]);
            text += " Keeps " + notes + " from " + chordName(prev->chord) + ".";
        }
    }
    auto lines = wrapText(text, 11.0f, Weight::medium, w);
    if (lines.size() > 2) {
        lines[1] = ellipsize(lines[1] + " " + lines[2], 11.0f, Weight::medium, w);
        lines.resize(2);
    }
    for (std::size_t i = 0; i < lines.size(); ++i)
        p.textAt(lines[i], 11.0f, Weight::medium, colour::textDim, 14.0f, 57.0f + static_cast<float>(i) * 14.8f);
}

} // namespace lg::ui
