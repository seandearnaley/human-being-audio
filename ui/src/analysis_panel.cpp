#include "panels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::ui {

namespace {
// Region widths in design pixels; the scope takes what is left.
constexpr float kPhaseWidth = 132.0f;
constexpr float kSpectrumWidth = 440.0f;

// The text row under the phase scope and the spectrum, and the gap above it.
constexpr float kFootHeight = 14.0f;
constexpr float kFootGap = 4.0f;

constexpr float kSqrtHalf = 0.70710678f;

constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kFloorDb = -90.0f;

struct GridLine {
    float hz;
    const char* label;
};
constexpr std::array<GridLine, 3> kGrid { { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } } };

float hzToPosition(float hz) { return std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz); }

class AnalysisPanel final : public Panel {
public:
    explicit AnalysisPanel(Context& context) : Panel(context) {}

    void tick() override { redraw(); }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();

        const float pad = px(metric::pad);
        const float top = pad;
        const float contentHeight = height() - 2.0f * pad;
        const float spectrumLeft = width() - px(kSpectrumWidth);
        const float phaseLeft = spectrumLeft - px(kPhaseWidth);

        divider(canvas, phaseLeft, top, contentHeight);
        divider(canvas, spectrumLeft, top, contentHeight);

        drawScope(canvas, visuals, pad, top, phaseLeft - 2.0f * pad, contentHeight);
        drawPhase(canvas, visuals, phaseLeft + pad, top, px(kPhaseWidth) - 2.0f * pad, contentHeight);
        drawSpectrum(canvas, visuals, spectrumLeft + pad, top, px(kSpectrumWidth) - 2.0f * pad, contentHeight);
    }

private:
    float hairline() const { return std::max(1.0f, std::round(px(metric::hairline))); }

    void divider(visage::Canvas& canvas, float x, float y, float h) {
        canvas.setColor(colour::edge);
        canvas.rectangle(std::round(x), std::round(y), hairline(), std::round(h));
    }

    void verticalGuide(visage::Canvas& canvas, float x, float y, float h) {
        canvas.rectangle(std::round(x - 0.5f * hairline()), std::round(y), hairline(), std::round(h));
    }

    // drawCurve with `low`..`high` landing exactly on the rectangle's bottom and
    // top edges, and room around it so the stroke is never cut in half there.
    void curve(visage::Canvas& canvas, std::span<const float> values, float x, float y, float w, float h,
               unsigned colourArgb, float low, float high, float fillAlpha = 0.0f, float baseline = 0.0f) {
        const float margin = px(metric::stroke);
        const float extra = (high - low) * margin / h;
        drawCurve(canvas, context_, values, x, y - margin, w, h + 2.0f * margin, colourArgb, low - extra,
                  high + extra, fillAlpha, baseline);
    }

    void drawScope(visage::Canvas& canvas, const Visuals& visuals, float x, float y, float w, float h) {
        canvas.setColor(colour::track);
        canvas.rectangle(x, std::round(y + 0.5f * (h - hairline())), w, hairline());

        curve(canvas, visuals.scopeMaster, x, y, w, h, colour::master, -1.0f, 1.0f, 0.10f);
        curve(canvas, visuals.scopeGrowl, x, y, w, h, colour::growl, -1.0f, 1.0f);
        curve(canvas, visuals.scopeOut, x, y, w, h, colour::withAlpha(colour::text, 0.8f), -1.0f, 1.0f);
    }

    // A goniometer: mid up, side across, so a mono signal is a vertical line.
    void drawPhase(visage::Canvas& canvas, const Visuals& visuals, float x, float y, float w, float h) {
        const float plotHeight = h - px(kFootHeight + kFootGap);
        const float side = std::round(std::min(w, plotHeight));
        const float left = std::round(x + 0.5f * (w - side));
        const float centreX = left + 0.5f * side;
        const float centreY = y + 0.5f * side;

        canvas.setColor(colour::track);
        verticalGuide(canvas, centreX, y, side);
        canvas.segment(left, y, left + side, y + side, hairline(), false);
        canvas.segment(left, y + side, left + side, y, hairline(), false);

        // Full scale on one channel reaches halfway along a diagonal; full-scale
        // mono reaches the top edge.
        const float scale = 0.5f * side * kSqrtHalf;
        const float half = 0.5f * side;
        visage::Path trace;
        for (std::size_t i = 0; i < visuals.gonioLeft.size(); ++i) {
            const float l = visuals.gonioLeft[i];
            const float r = visuals.gonioRight[i];
            const float across = std::clamp((r - l) * kSqrtHalf * scale, -half, half);
            const float up = std::clamp((l + r) * kSqrtHalf * scale, -half, half);
            const float tx = centreX - left + across;
            const float ty = centreY - y - up;
            if (i == 0)
                trace.moveTo(tx, ty);
            else
                trace.lineTo(tx, ty);
        }
        canvas.setColor(colour::withAlpha(colour::text, 0.7f));
        canvas.stroke(trace, left, y, side, side, px(1.0f));

        // Rounded first so a value that shows as 0.00 is never drawn as alarming.
        const float correlation = std::round(std::clamp(visuals.correlation, -1.0f, 1.0f) * 100.0f) / 100.0f;
        char text[16];
        std::snprintf(text, sizeof text, "%+.2f", static_cast<double>(correlation == 0.0f ? 0.0f : correlation));
        drawLabel(canvas, context_, text, x, y + h - px(kFootHeight), w, px(kFootHeight),
                  correlation < 0.0f ? colour::alert : colour::text, visage::Font::kCenter, type::label,
                  Weight::regular);
    }

    void drawSpectrum(visage::Canvas& canvas, const Visuals& visuals, float x, float y, float w, float h) {
        const float plotHeight = h - px(kFootHeight + kFootGap);
        const float footY = y + h - px(kFootHeight);
        const float labelWidth = px(40.0f);

        for (const auto& line : kGrid) {
            const float lineX = std::round(x + w * hzToPosition(line.hz));
            canvas.setColor(colour::track);
            verticalGuide(canvas, lineX, y, plotHeight);
            drawLabel(canvas, context_, line.label, lineX - 0.5f * labelWidth, footY, labelWidth, px(kFootHeight),
                      colour::textFaint, visage::Font::kCenter, type::small);
        }

        // One curve, drawn twice and clipped at the sub low-pass frequency: the
        // two colours meet at exactly the same point on the line.
        const float subHz = std::clamp(context_.model.get(ParamId::subFreq), kMinHz, kMaxHz);
        const float split = std::round(x + w * hzToPosition(subHz));
        const std::span<const float> bands = visuals.spectrumDb;

        canvas.saveState();
        canvas.trimClampBounds(0.0f, 0.0f, split, height());
        curve(canvas, bands, x, y, w, plotHeight, colour::master, kFloorDb, 0.0f, 0.12f, kFloorDb);
        canvas.restoreState();

        canvas.saveState();
        canvas.trimClampBounds(split, 0.0f, width() - split, height());
        curve(canvas, bands, x, y, w, plotHeight, colour::growl, kFloorDb, 0.0f, 0.12f, kFloorDb);
        canvas.restoreState();
    }
};
}

std::unique_ptr<Panel> makeAnalysisPanel(Context& context) { return std::make_unique<AnalysisPanel>(context); }

}
