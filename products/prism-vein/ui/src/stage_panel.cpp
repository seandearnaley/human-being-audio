// SPDX-License-Identifier: AGPL-3.0-or-later
// Hero Spectral Prism Lattice visualizer + interactive 12-note pitch-class chord bar
// + 8 one-click chord voicing chips. Zero helper text, zero duplicated parameters.
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace pv::ui {

namespace {

inline constexpr float kMinFreq = 32.0f;
inline constexpr float kMaxFreq = 18500.0f;
inline constexpr float kChordBarH = 38.0f;

float hzToNormX(float hz) {
    const float f = std::clamp(hz, kMinFreq, kMaxFreq);
    return std::log(f / kMinFreq) / std::log(kMaxFreq / kMinFreq);
}

class ChordStrip final : public visage::Frame, public Live {
  public:
    explicit ChordStrip(Context& ctx) : context_(ctx) {
        context_.live.push_back(this);
        for (std::size_t i = 0; i < kChordPresets.size(); ++i) {
            const auto mask = kChordPresets[i].mask;
            auto btn = std::make_unique<PillButton>(context_, std::string(kChordPresets[i].label), [this, mask] {
                context_.model.setChordMask(mask);
                redraw();
            });
            btn->setFontSize(9.4f);
            addChild(btn.get());
            presetBtns_[i] = std::move(btn);
        }
    }

    ~ChordStrip() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto mask = context_.model.chordMask();
        for (std::size_t i = 0; i < kChordPresets.size(); ++i) {
            const bool active = (mask == kChordPresets[i].mask);
            presetBtns_[i]->setActive(active);
            presetBtns_[i]->setAccent(active ? colour::prism : colour::textDim);
        }
        redraw();
    }

    void resized() override {
        const float sc = context_.scale;
        const float keysTotalW = keysAreaWidth();
        const float presetStart = keysTotalW + 10.0f * sc;
        const float availW = std::max(80.0f * sc, width() - presetStart);
        const float gap = 4.0f * sc;
        const float btnW =
            (availW - gap * static_cast<float>(kChordPresets.size() - 1)) / static_cast<float>(kChordPresets.size());
        const float btnH = height() - 6.0f * sc;
        const float y = 3.0f * sc;
        for (std::size_t i = 0; i < kChordPresets.size(); ++i) {
            presetBtns_[i]->setBounds(presetStart + static_cast<float>(i) * (btnW + gap), y, btnW, btnH);
        }
    }

    void draw(visage::Canvas& canvas) override {
        static constexpr std::array<const char*, 12> kNotes{"C",  "C#", "D",  "D#", "E",  "F",
                                                            "F#", "G",  "G#", "A",  "A#", "B"};
        static constexpr std::array<bool, 12> kSharp{false, true,  false, true,  false, false,
                                                     true,  false, true,  false, true,  false};

        const float sc = context_.scale;
        const auto& tel = context_.model.visuals();
        const std::uint16_t activeMask = tel.midiActive ? tel.activePitchMask : context_.model.chordMask();
        const float keysW = keysAreaWidth();
        const float gap = 3.0f * sc;
        const float keyW = (keysW - gap * 11.0f) / 12.0f;
        const float keyH = height() - 6.0f * sc;
        const float y = 3.0f * sc;

        for (int pc = 0; pc < 12; ++pc) {
            const float x = static_cast<float>(pc) * (keyW + gap);
            const bool lit = (activeMask & (1u << pc)) != 0u;
            const bool hov = (pc == hoveredKey_);
            const float glow = std::clamp(tel.pitchEnergy[static_cast<std::size_t>(pc)], 0.0f, 1.0f);
            const unsigned hue = colour::pitchAccent(pc);

            const unsigned bg = lit ? colour::withAlpha(hue, 0.24f + 0.34f * glow)
                                    : (hov ? colour::withAlpha(hue, 0.14f)
                                           : (kSharp[static_cast<std::size_t>(pc)] ? colour::window : colour::raised));
            hba::ui::drawPanel(canvas, sc, x, y, keyW, keyH, bg, metric::radiusSmall);

            if (lit || hov) {
                canvas.setColor(colour::withAlpha(hue, lit ? (0.72f + 0.28f * glow) : 0.45f));
                canvas.roundedRectangleBorder(x, y, keyW, keyH, metric::radiusSmall * sc, 1.15f * sc);
            }
            if (lit) {
                // Crisp luminous top prism bar on active chord pitch class
                canvas.setColor(hue);
                canvas.roundedRectangle(x + 3.0f * sc, y + 2.0f * sc, keyW - 6.0f * sc, 2.4f * sc, 1.2f * sc);
            }

            const unsigned ink = lit ? colour::text : (hov ? colour::text : colour::textDim);
            hba::ui::drawLabel(canvas, sc, kNotes[static_cast<std::size_t>(pc)], x, y + 2.0f * sc, keyW,
                               keyH - 2.0f * sc, ink, visage::Font::kCenter, 10.4f,
                               lit ? Weight::semibold : Weight::medium);
        }
    }

    void mouseMove(const visage::MouseEvent& e) override {
        const int idx = keyAt(e.position.x);
        if (idx != hoveredKey_) {
            hoveredKey_ = idx;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        if (hoveredKey_ >= 0) {
            hoveredKey_ = -1;
            redraw();
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const int pc = keyAt(e.position.x);
        if (pc < 0)
            return;
        const std::uint16_t cur = context_.model.chordMask();
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << pc);
        std::uint16_t next = static_cast<std::uint16_t>(cur ^ bit);
        if ((next & 0x0FFFu) == 0u)
            next = bit; // always keep at least one active pitch class
        context_.model.setChordMask(next);
        redraw();
    }

  private:
    float keysAreaWidth() const { return std::min(width() * 0.56f, context_.px(690.0f)); }

    int keyAt(float x) const {
        const float keysW = keysAreaWidth();
        if (x < 0.0f || x > keysW)
            return -1;
        return std::clamp(static_cast<int>((x / keysW) * 12.0f), 0, 11);
    }

    Context& context_;
    std::array<std::unique_ptr<PillButton>, kChordPresets.size()> presetBtns_{};
    int hoveredKey_ = -1;
};

class StagePanel final : public Panel {
  public:
    explicit StagePanel(Context& ctx) : Panel(ctx) { chordStrip_ = &add<ChordStrip>(ctx); }

    void tick() override { redraw(); }

    void resized() override {
        const float pad = px(10.0f);
        const float stripH = px(kChordBarH);
        chordStrip_->setBounds(pad, height() - stripH - px(6.0f), width() - 2.0f * pad, stripH);
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const float sc = context_.scale;
        const float pad = 10.0f * sc;
        const float plotX = pad;
        const float plotY = 10.0f * sc;
        const float plotW = std::max(40.0f * sc, width() - 2.0f * pad);
        const float plotH = std::max(40.0f * sc, height() - px(kChordBarH) - 22.0f * sc);

        // Recessed obsidian spectral chamber
        hba::ui::drawPanel(canvas, sc, plotX, plotY, plotW, plotH, colour::window, metric::radiusSmall);

        const auto& tel = context_.model.visuals();
        const float subHz = context_.model.get(ParamId::subLockHz);
        const float subX = plotX + plotW * hzToNormX(subHz);
        const int engIdx = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::engineMode))), 0, 3);
        const unsigned engHue = colour::engineAccent(engIdx);

        // 1. Sub-Lock Safe Zone (emerald tint left of subLockHz crossover)
        const float subZoneW = std::max(2.0f * sc, subX - plotX);
        canvas.setColor(colour::withAlpha(colour::sub, 0.075f));
        canvas.roundedRectangle(plotX, plotY, subZoneW, plotH, metric::radiusSmall * sc);

        // 2. Subtle log-frequency vertical grid lines + crisp bottom tick labels
        static constexpr std::array<std::pair<float, const char*>, 9> kFreqTicks{{
            {50.0f, "50"},
            {100.0f, "100"},
            {250.0f, "250"},
            {500.0f, "500"},
            {1000.0f, "1k"},
            {2000.0f, "2k"},
            {4000.0f, "4k"},
            {8000.0f, "8k"},
            {16000.0f, "16k"},
        }};
        for (const auto& [hz, label] : kFreqTicks) {
            const float gx = plotX + plotW * hzToNormX(hz);
            canvas.setColor(colour::withAlpha(colour::text, 0.055f));
            canvas.fill(gx, plotY + 2.0f * sc, 1.0f * sc, plotH - 18.0f * sc);
            hba::ui::drawLabel(canvas, sc, label, gx - 16.0f * sc, plotY + plotH - 16.0f * sc, 32.0f * sc, 14.0f * sc,
                               colour::textFaint, visage::Font::kCenter, 8.8f, Weight::medium);
        }

        // Horizontal dB guide lines
        for (int g = 1; g <= 3; ++g) {
            const float gy = plotY + plotH * (static_cast<float>(g) / 4.0f);
            canvas.setColor(colour::withAlpha(colour::text, 0.035f));
            canvas.fill(plotX + 2.0f * sc, gy, plotW - 4.0f * sc, 1.0f * sc);
        }

        // 3. Glowing vertical Harmonic Chord Partial Veins (visual prism lattice!)
        const std::uint16_t liveMask = tel.midiActive ? tel.activePitchMask : context_.model.chordMask();
        const float shiftSt = context_.model.get(ParamId::shift);
        const int octOff =
            (std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::octaveShift))), 0, 4) - 2) * 12;
        const int numHarms = std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::harmonics))), 1, 12);
        const float colorAmt = context_.model.get(ParamId::color);
        const float curveTop = plotY + 28.0f * sc;
        const float curveBottom = plotY + plotH - 16.0f * sc;
        const float curveH = std::max(20.0f * sc, curveBottom - curveTop);

        for (int pc = 0; pc < 12; ++pc) {
            if ((liveMask & (1u << pc)) == 0u)
                continue;
            const unsigned pcHue = colour::pitchAccent(pc);
            const float pcGlow = 0.25f + 0.75f * std::clamp(tel.pitchEnergy[static_cast<std::size_t>(pc)], 0.0f, 1.0f);
            const float baseMidi = static_cast<float>(48 + pc + octOff) + shiftSt;
            const float f0 = 440.0f * std::pow(2.0f, (baseMidi - 69.0f) / 12.0f);
            const int showHarms = std::min(numHarms, 6);
            for (int h = 1; h <= showHarms; ++h) {
                const float fh = f0 * static_cast<float>(h);
                if (fh <= subHz || fh >= kMaxFreq)
                    continue;
                const float vx = plotX + plotW * hzToNormX(fh);
                const float alpha =
                    (h == 1 ? 0.42f : 0.22f / static_cast<float>(h)) * (0.25f + 0.75f * colorAmt) * pcGlow;
                const float beamW = (h == 1 ? 2.2f : 1.2f) * sc;
                canvas.setColor(colour::withAlpha(pcHue, std::clamp(alpha, 0.03f, 0.65f)));
                canvas.fill(vx - beamW * 0.5f, curveTop, beamW, curveH);
            }
        }

        // 4. Raw Input Spectrum silhouette (cool slate)
        drawCurve(canvas, context_, tel.inSpectrum, plotX + 2.0f * sc, curveTop, plotW - 4.0f * sc, curveH,
                  colour::withAlpha(colour::textDim, 0.52f), 0.0f, 1.0f, 0.09f, 0.0f, 1.35f);

        // 5. Colored Prism Spectrum (luminous halo + razor-crisp engine-accented stroke)
        drawCurve(canvas, context_, tel.outSpectrum, plotX + 2.0f * sc, curveTop, plotW - 4.0f * sc, curveH,
                  colour::withAlpha(engHue, 0.24f), 0.0f, 1.0f, 0.16f, 0.0f, 4.4f);
        drawCurve(canvas, context_, tel.outSpectrum, plotX + 2.0f * sc, curveTop, plotW - 4.0f * sc, curveH, engHue,
                  0.0f, 1.0f, 0.0f, 0.0f, 1.95f);

        // 6. Bark Auto-Tame Reduction Ribbon hanging cleanly below the top HUD badges
        if (context_.model.get(ParamId::tame) > 0.01f) {
            std::array<float, kSpectrumBins> invertedTame{};
            for (std::size_t i = 0; i < kSpectrumBins; ++i)
                invertedTame[i] = 1.0f - std::clamp(tel.tameReduction[i] * 0.60f, 0.0f, 0.60f);
            drawCurve(canvas, context_, invertedTame, plotX + 2.0f * sc, curveTop, plotW - 4.0f * sc, curveH * 0.78f,
                      colour::rose, 0.0f, 1.0f, 0.0f, 1.0f, 1.6f);
        }

        // 7. Sub-Lock Crossover Line + Frequency Tag
        canvas.setColor(colour::withAlpha(colour::sub, 0.85f));
        canvas.fill(subX - 0.75f * sc, plotY + 2.0f * sc, 1.5f * sc, curveBottom - plotY - 2.0f * sc);
        char subBuf[24]{};
        std::snprintf(subBuf, sizeof(subBuf), "SUB %.0f Hz", subHz);
        hba::ui::drawLabel(canvas, sc, subBuf, plotX + 8.0f * sc, plotY + 6.0f * sc, 82.0f * sc, 16.0f * sc,
                           colour::sub, visage::Font::kLeft, 9.4f, Weight::semibold);

        // 8. Top-Right Compact Telemetry HUD Badges (zero helper prose!)
        const float hudH = 18.0f * sc;
        float hx = plotX + plotW - 8.0f * sc;
        const float hy = plotY + 6.0f * sc;

        auto drawHudBadge = [&](const std::string& label, unsigned hue, bool lit, float wDesign) {
            const float bw = wDesign * sc;
            hx -= bw;
            const unsigned bg = lit ? colour::withAlpha(hue, 0.22f) : colour::withAlpha(colour::raised, 0.75f);
            hba::ui::drawPanel(canvas, sc, hx, hy, bw, hudH, bg, 4.0f);
            if (lit) {
                canvas.setColor(colour::withAlpha(hue, 0.75f));
                canvas.roundedRectangleBorder(hx, hy, bw, hudH, 4.0f * sc, 1.0f * sc);
            }
            hba::ui::drawLabel(canvas, sc, label, hx, hy, bw, hudH, lit ? hue : colour::textFaint,
                               visage::Font::kCenter, 9.2f, Weight::semibold);
            hx -= 5.0f * sc;
        };

        char tameBuf[32]{};
        std::snprintf(tameBuf, sizeof(tameBuf), "TAME -%.1f dB", std::max(0.0f, tel.tamePeakDb));
        drawHudBadge(tameBuf, colour::rose, tel.tamePeakDb > 0.35f, 84.0f);

        drawHudBadge("SHIELD", colour::tame, tel.transientAmt > 0.12f, 54.0f);
        drawHudBadge("SC", colour::harmonic, tel.sidechainActive, 34.0f);
        drawHudBadge("MIDI", colour::prism, tel.midiActive, 44.0f);
        if (tel.frozen)
            drawHudBadge("FROZEN", colour::rose, true, 56.0f);
    }

  private:
    ChordStrip* chordStrip_ = nullptr;
};

} // namespace

std::unique_ptr<visage::Frame> makeStagePanel(Context& context) { return std::make_unique<StagePanel>(context); }

} // namespace pv::ui
