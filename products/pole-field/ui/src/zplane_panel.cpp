// The main upper deck:
//   Left  — Interactive 3D Z-plane unit circle + X/Y morph surface + Z depth rail
//   Mid   — Real-time 14-pole transfer curve overlaid on live FFT spectrum + Sub Lock + Goniometer
//   Right — Lattice, Drive, and Sub Lock parameter stack
#include "panels.h"

#include "pf/cubes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace pf::ui {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kSqrtHalf = 0.70710678f;
constexpr float kLeftWidth = 408.0f;
constexpr float kRightWidth = 304.0f;
constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kSpecFloorDb = -90.0f;
constexpr float kRespMinDb = -30.0f;
constexpr float kRespMaxDb = 24.0f;
constexpr int kResponseBins = 160;

float hzToX(float hz) { return std::log(std::clamp(hz, kMinHz, kMaxHz) / kMinHz) / std::log(kMaxHz / kMinHz); }

std::string noteName(int midi) {
    if (midi < 0) return "--";
    static constexpr std::array<const char*, 12> kNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int oct = (midi / 12) - 1;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%s%d", kNames[static_cast<std::size_t>(midi % 12)], oct);
    return buf;
}

// Interactive 2D X/Y pad rendered directly over the live 14-pole Z-plane unit circle.
class ZPlanePad final : public visage::Frame {
public:
    explicit ZPlanePad(Context& context) : context_(context) {}

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::raised, metric::radiusSmall);

        const float pad = context_.px(10.0f);
        const float side = std::min(width(), height()) - 2.0f * pad;
        const float cx = width() * 0.5f;
        const float cy = height() * 0.5f;
        const float rad = side * 0.44f;
        const float line = std::max(1.0f, std::round(context_.px(metric::hairline)));

        // Subtle quadrant crosshairs + unit circle + inner r=0.5 ring
        canvas.setColor(colour::edge);
        canvas.rectangle(std::round(cx - rad - pad * 0.3f), std::round(cy), std::round((rad + pad * 0.3f) * 2.0f), line);
        canvas.rectangle(std::round(cx), std::round(cy - rad - pad * 0.3f), line, std::round((rad + pad * 0.3f) * 2.0f));

        canvas.setColor(colour::withAlpha(colour::text, 0.14f));
        canvas.ring(cx - rad, cy - rad, 2.0f * rad, line);
        canvas.setColor(colour::withAlpha(colour::text, 0.06f));
        canvas.ring(cx - 0.5f * rad, cy - 0.5f * rad, rad, line);

        // Corner labels for the selected cube (front Z=0 vs back Z=1 face; cyan custom labels on Imprint Cube)
        const Visuals& vis = context_.model.visuals();
        const int cubeIdx = static_cast<int>(context_.model.get(ParamId::cube) + 0.5f);
        const auto& cube = cubeAt(cubeIdx);
        const ImprintData imp = context_.model.imprint();
        const bool backFace = vis.effMorphZ >= 0.5f;
        const int faceBase = backFace ? 4 : 0;
        const float lw = width() * 0.48f;
        const float lh = context_.px(16.0f);
        const float inset = context_.px(8.0f);

        const auto drawCorner = [&](int q, float x, float y, visage::Font::Justification just) {
            const bool customSlot = (cubeIdx == kNumCubes - 1) && !backFace && imp.slotEnabled(q);
            const std::string label = customSlot
                                          ? context_.model.imprintSlotLabel(q)
                                          : std::string(cube.cornerNames[static_cast<std::size_t>(faceBase + q)]);
            drawLabel(canvas, context_, label, x, y, lw, lh, customSlot ? colour::sub : colour::textFaint, just,
                      type::small);
        };
        drawCorner(0, inset, inset, visage::Font::kLeft);
        drawCorner(1, width() - inset - lw, inset, visage::Font::kRight);
        drawCorner(2, inset, height() - inset - lh, visage::Font::kLeft);
        drawCorner(3, width() - inset - lw, height() - inset - lh, visage::Font::kRight);

        // Compute live conjugate poles and zeros from effective modulated state
        const int stages = kStageCounts[static_cast<std::size_t>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::stages) + 0.5f), 0, 3))];
        const int topology = std::clamp(static_cast<int>(context_.model.get(ParamId::topology) + 0.5f), 0, 2);
        const ResolvedLattice lat =
            resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ, vis.effFreq + vis.midiShiftSt,
                           vis.effRes, vis.effTransform, 0.0f, &imp, stages, topology);

        const float crossHalf = context_.px(4.2f);
        const float zeroDiam = context_.px(7.0f);
        const float stroke = context_.px(1.6f);

        for (int s = 0; s < stages; ++s) {
            const ZPlanePoint zp = stageZPoint(lat.stages[static_cast<std::size_t>(s)], vis.sampleRate);
            // Warp angle visually with power 0.45 so low/mid neuro frequencies spread clearly around the unit circle.
            const float pAng = std::pow(zp.poleTheta / kPi, 0.45f) * kPi;
            const float zAng = std::pow(zp.zeroTheta / kPi, 0.45f) * kPi;

            for (float sign : { 1.0f, -1.0f }) {
                const float zx = cx + std::cos(sign * zAng) * zp.zeroR * rad;
                const float zy = cy - std::sin(sign * zAng) * zp.zeroR * rad;
                canvas.setColor(colour::withAlpha(colour::sub, 0.85f));
                canvas.ring(zx - zeroDiam * 0.5f, zy - zeroDiam * 0.5f, zeroDiam, context_.px(1.25f));

                const float pxPos = cx + std::cos(sign * pAng) * zp.poleR * rad;
                const float pyPos = cy - std::sin(sign * pAng) * zp.poleR * rad;
                canvas.setColor(colour::poles);
                canvas.segment(pxPos - crossHalf, pyPos - crossHalf, pxPos + crossHalf, pyPos + crossHalf, stroke, true);
                canvas.segment(pxPos + crossHalf, pyPos - crossHalf, pxPos - crossHalf, pyPos + crossHalf, stroke, true);
            }
        }

        // Map 0..1 morph X/Y to inner square
        const float innerX = pad + context_.px(12.0f);
        const float innerY = pad + context_.px(16.0f);
        const float innerW = width() - 2.0f * innerX;
        const float innerH = height() - 2.0f * innerY;

        const float baseX = innerX + context_.model.get(ParamId::morphX) * innerW;
        const float baseY = innerY + context_.model.get(ParamId::morphY) * innerH;
        const float effX = innerX + vis.effMorphX * innerW;
        const float effY = innerY + vis.effMorphY * innerH;

        // Line from base point to lane-modulated live point
        if (std::hypot(effX - baseX, effY - baseY) > context_.px(2.0f)) {
            canvas.setColor(colour::withAlpha(colour::motion, 0.65f));
            canvas.segment(baseX, baseY, effX, effY, context_.px(1.3f), true);
            const float md = context_.px(7.0f);
            canvas.setColor(colour::motion);
            canvas.circle(effX - md * 0.5f, effY - md * 0.5f, md);
        }

        // Interactive base morph puck
        const float ringD = context_.px(16.0f);
        canvas.setColor(colour::withAlpha(colour::poles, 0.24f));
        canvas.circle(baseX - ringD * 0.5f, baseY - ringD * 0.5f, ringD);
        canvas.setColor(colour::text);
        canvas.ring(baseX - ringD * 0.5f, baseY - ringD * 0.5f, ringD, context_.px(1.6f));
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup()) {
            const int col = e.position.x >= width() * 0.5f ? 1 : 0;
            const int row = e.position.y >= height() * 0.5f ? 1 : 0;
            const int slot = row * 2 + col;
            static constexpr std::array<const char*, 4> kSlotLetter{"A", "B", "C", "D"};
            const std::string letter = kSlotLetter[static_cast<std::size_t>(slot)];
            const ImprintData imp = context_.model.imprint();

            visage::PopupMenu menu;
            menu.addOption(1, "Imprint WAV \u2192 Corner " + letter);
            menu.addOption(2, "Capture Live \u2192 Corner " + letter);
            menu.addOption(3, "Clear Corner " + letter).enable(imp.slotEnabled(slot));
            menu.addOption(4, "Clear All Imprints").enable(imp.hasAny());
            menu.onSelection() = [this, slot](int opt) {
                if (opt == 1)
                    context_.model.browseImprintAudioSlot(slot);
                else if (opt == 2)
                    context_.model.captureLiveImprint(slot);
                else if (opt == 3)
                    context_.model.clearImprintSlot(slot);
                else if (opt == 4)
                    context_.model.clearImprint();
            };
            menu.show(this);
            return;
        }
        if (e.repeatClickCount() >= 2) {
            context_.model.begin(ParamId::morphX);
            context_.model.begin(ParamId::morphY);
            context_.model.set(ParamId::morphX, spec(ParamId::morphX).def);
            context_.model.set(ParamId::morphY, spec(ParamId::morphY).def);
            context_.model.end(ParamId::morphX);
            context_.model.end(ParamId::morphY);
            return;
        }
        dragging_ = true;
        context_.model.begin(ParamId::morphX);
        context_.model.begin(ParamId::morphY);
        applyPosition(e.position);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragging_) applyPosition(e.position);
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (!dragging_) return;
        dragging_ = false;
        context_.model.end(ParamId::morphX);
        context_.model.end(ParamId::morphY);
    }

private:
    void applyPosition(visage::Point p) {
        const float pad = context_.px(10.0f);
        const float innerX = pad + context_.px(12.0f);
        const float innerY = pad + context_.px(16.0f);
        const float innerW = std::max(1.0f, width() - 2.0f * innerX);
        const float innerH = std::max(1.0f, height() - 2.0f * innerY);
        const float nx = std::clamp((p.x - innerX) / innerW, 0.0f, 1.0f);
        const float ny = std::clamp((p.y - innerY) / innerH, 0.0f, 1.0f);
        context_.model.set(ParamId::morphX, nx);
        context_.model.set(ParamId::morphY, ny);
        redraw();
    }

    Context& context_;
    bool dragging_ = false;
};

// Vertical Morph Z rail alongside the X/Y unit circle.
class ZDepthRail final : public visage::Frame {
public:
    explicit ZDepthRail(Context& context) : context_(context) {}

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::raised, metric::radiusSmall);
        const float pad = context_.px(8.0f);
        const float top = pad + context_.px(16.0f);
        const float bottom = height() - pad - context_.px(16.0f);
        const float trackH = std::max(1.0f, bottom - top);
        const float trackW = context_.px(4.0f);
        const float cx = width() * 0.5f;

        drawLabel(canvas, context_, "Z", 0.0f, pad * 0.5f, width(), context_.px(14.0f), colour::poles,
                  visage::Font::kCenter, type::small, Weight::semibold);
        drawLabel(canvas, context_, "0", 0.0f, bottom + context_.px(2.0f), width(), context_.px(14.0f),
                  colour::textFaint, visage::Font::kCenter, type::small);

        canvas.setColor(colour::track);
        canvas.roundedRectangle(cx - trackW * 0.5f, top, trackW, trackH, trackW * 0.5f);

        const float val = context_.model.get(ParamId::morphZ);
        const float eff = context_.model.visuals().effMorphZ;
        const float yBase = bottom - val * trackH;
        const float yEff = bottom - eff * trackH;

        canvas.setColor(colour::poles);
        canvas.roundedRectangle(cx - trackW * 0.5f, yBase, trackW, bottom - yBase, trackW * 0.5f);

        if (std::abs(yEff - yBase) > context_.px(2.0f)) {
            const float md = context_.px(6.0f);
            canvas.setColor(colour::motion);
            canvas.circle(cx - md * 0.5f, yEff - md * 0.5f, md);
        }

        const float thumb = context_.px(10.0f);
        canvas.setColor(colour::text);
        canvas.circle(cx - thumb * 0.5f, yBase - thumb * 0.5f, thumb);
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        if (e.repeatClickCount() >= 2) {
            context_.model.begin(ParamId::morphZ);
            context_.model.set(ParamId::morphZ, spec(ParamId::morphZ).def);
            context_.model.end(ParamId::morphZ);
            return;
        }
        dragging_ = true;
        context_.model.begin(ParamId::morphZ);
        applyY(e.position.y);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragging_) applyY(e.position.y);
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (!dragging_) return;
        dragging_ = false;
        context_.model.end(ParamId::morphZ);
    }

private:
    void applyY(float y) {
        const float pad = context_.px(8.0f);
        const float top = pad + context_.px(16.0f);
        const float bottom = height() - pad - context_.px(16.0f);
        const float v = std::clamp((bottom - y) / std::max(1.0f, bottom - top), 0.0f, 1.0f);
        context_.model.set(ParamId::morphZ, v);
        redraw();
    }

    Context& context_;
    bool dragging_ = false;
};

// Interactive transfer-response + FFT plot with direct-manipulation drag for Sub Split, Freq, and Resonance.
class ResponsePlot final : public visage::Frame {
  public:
    explicit ResponsePlot(Context& context) : context_(context) {}

    void draw(visage::Canvas& canvas) override {
        const Visuals& vis = context_.model.visuals();
        const float w = width();
        const float h = height();
        const float line = std::max(1.0f, std::round(context_.px(metric::hairline)));

        drawPanel(canvas, context_, 0.0f, 0.0f, w, h, colour::raised, metric::radiusSmall);

        // Sub Lock cyan shaded region + draggable crossover divider
        const bool subOn = context_.model.get(ParamId::subLock) >= 0.5f;
        const float subHz = context_.model.get(ParamId::subSplit);
        const float subX = std::round(w * hzToX(subHz));
        if (subOn) {
            canvas.setColor(colour::withAlpha(colour::sub, 0.11f));
            canvas.rectangle(line, line, std::max(0.0f, subX - line), h - 2.0f * line);
            const bool hot = hoverSub_ || dragMode_ == DragMode::subSplit;
            canvas.setColor(colour::withAlpha(colour::sub, hot ? 0.95f : 0.65f));
            const float splitW = hot ? std::max(line, context_.px(2.0f)) : line;
            canvas.rectangle(subX - 0.5f * (splitW - line), line, splitW, h - 2.0f * line);
        }

        // Frequency grid lines + 0 dB reference
        constexpr std::array<float, 5> kGridHz{60.0f, 200.0f, 1000.0f, 4000.0f, 12000.0f};
        for (float hz : kGridHz) {
            const float gx = std::round(w * hzToX(hz));
            canvas.setColor(colour::track);
            canvas.rectangle(gx, line, line, h - 2.0f * line);
        }
        const float zeroDbY = std::round(h * (1.0f - (0.0f - kRespMinDb) / (kRespMaxDb - kRespMinDb)));
        canvas.setColor(colour::withAlpha(colour::text, 0.14f));
        canvas.rectangle(line, zeroDbY, w - 2.0f * line, line);

        // Live stereo-averaged output FFT spectrum behind transfer curve
        drawCurve(canvas, context_, vis.spectrumDb, line, line, w - 2.0f * line, h - 2.0f * line,
                  colour::withAlpha(colour::text, 0.24f), kSpecFloorDb, 0.0f, 0.10f, kSpecFloorDb, 1.0f);

        // Exact analytical 14-pole transfer function evaluated at the oversampled DSP rate
        const int cubeIdx = static_cast<int>(context_.model.get(ParamId::cube) + 0.5f);
        const int stages = kStageCounts[static_cast<std::size_t>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::stages) + 0.5f), 0, 3))];
        const int topology = std::clamp(static_cast<int>(context_.model.get(ParamId::topology) + 0.5f), 0, 2);
        const ImprintData imp = context_.model.imprint();
        const float totalFreq = vis.effFreq + vis.midiShiftSt;
        const float stereoSt = vis.effSpread * 5.0f;
        const double evalSr = vis.sampleRate * static_cast<double>(std::max(1, vis.osFactor));

        const ResolvedLattice latL = resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ, totalFreq,
                                                    vis.effRes, vis.effTransform, -stereoSt, &imp, stages, topology);
        const ResolvedLattice latR = resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ, totalFreq,
                                                    vis.effRes, vis.effTransform, +stereoSt, &imp, stages, topology);

        std::array<float, kResponseBins> curveL{};
        std::array<float, kResponseBins> curveR{};
        for (int i = 0; i < kResponseBins; ++i) {
            const double frac = static_cast<double>(i) / static_cast<double>(kResponseBins - 1);
            const double hz = static_cast<double>(kMinHz) * std::pow(static_cast<double>(kMaxHz / kMinHz), frac);
            float dbL = evaluateMagnitudeDb(latL, stages, topology, hz, evalSr);
            float dbR = evaluateMagnitudeDb(latR, stages, topology, hz, evalSr);
            if (subOn && hz < static_cast<double>(subHz)) {
                const float blend =
                    static_cast<float>(std::pow(std::clamp((hz - 0.5 * subHz) / (0.5 * subHz), 0.0, 1.0), 2.0));
                dbL *= blend;
                dbR *= blend;
            }
            curveL[static_cast<std::size_t>(i)] = dbL;
            curveR[static_cast<std::size_t>(i)] = dbR;
        }

        if (stereoSt > 0.05f) {
            drawCurve(canvas, context_, curveR, line, line, w - 2.0f * line, h - 2.0f * line,
                      colour::withAlpha(colour::drive, 0.55f), kRespMinDb, kRespMaxDb, 0.0f, 0.0f, 1.2f);
        }
        drawCurve(canvas, context_, curveL, line, line, w - 2.0f * line, h - 2.0f * line, colour::poles, kRespMinDb,
                  kRespMaxDb, 0.14f, kRespMinDb, 2.0f);
    }

    void mouseMove(const visage::MouseEvent& e) override {
        const bool near = isNearSubSplit(e.position.x);
        if (near != hoverSub_) {
            hoverSub_ = near;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        if (hoverSub_) {
            hoverSub_ = false;
            redraw();
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup())
            return;
        const bool nearSub = isNearSubSplit(e.position.x);
        if (e.repeatClickCount() >= 2) {
            if (nearSub) {
                context_.model.begin(ParamId::subSplit);
                context_.model.set(ParamId::subSplit, spec(ParamId::subSplit).def);
                context_.model.end(ParamId::subSplit);
            } else {
                context_.model.begin(ParamId::freq);
                context_.model.begin(ParamId::res);
                context_.model.set(ParamId::freq, spec(ParamId::freq).def);
                context_.model.set(ParamId::res, spec(ParamId::res).def);
                context_.model.end(ParamId::freq);
                context_.model.end(ParamId::res);
            }
            redraw();
            return;
        }

        lastPos_ = e.position;
        if (nearSub) {
            dragMode_ = DragMode::subSplit;
            context_.model.begin(ParamId::subSplit);
            applySubX(e.position.x);
        } else {
            dragMode_ = DragMode::freqRes;
            freqAccum_ = context_.model.get(ParamId::freq);
            resAccum_ = context_.model.get(ParamId::res);
            context_.model.begin(ParamId::freq);
            context_.model.begin(ParamId::res);
        }
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragMode_ == DragMode::subSplit) {
            applySubX(e.position.x);
            redraw();
            return;
        }
        if (dragMode_ == DragMode::freqRes) {
            const float fine = e.isShiftDown() ? 0.1f : 1.0f;
            const float dx = e.position.x - lastPos_.x;
            const float dy = lastPos_.y - e.position.y;
            lastPos_ = e.position;
            const float w = std::max(1.0f, width());
            const float h = std::max(1.0f, height());
            freqAccum_ = clampToRange(spec(ParamId::freq), freqAccum_ + (dx / w) * 48.0f * fine);
            resAccum_ = clampToRange(spec(ParamId::res), resAccum_ + (dy / h) * 1.1f * fine);
            context_.model.set(ParamId::freq, freqAccum_);
            context_.model.set(ParamId::res, resAccum_);
            redraw();
        }
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (dragMode_ == DragMode::subSplit) {
            context_.model.end(ParamId::subSplit);
        } else if (dragMode_ == DragMode::freqRes) {
            context_.model.end(ParamId::freq);
            context_.model.end(ParamId::res);
        }
        dragMode_ = DragMode::none;
        redraw();
    }

  private:
    enum class DragMode { none, subSplit, freqRes };

    bool isNearSubSplit(float x) const {
        if (context_.model.get(ParamId::subLock) < 0.5f)
            return false;
        const float subX = width() * hzToX(context_.model.get(ParamId::subSplit));
        return std::abs(x - subX) <= context_.px(8.0f);
    }

    void applySubX(float x) {
        const float nx = std::clamp(x / std::max(1.0f, width()), 0.0f, 1.0f);
        const float hz = kMinHz * std::pow(kMaxHz / kMinHz, nx);
        context_.model.set(ParamId::subSplit, clampToRange(spec(ParamId::subSplit), hz));
    }

    Context& context_;
    DragMode dragMode_ = DragMode::none;
    bool hoverSub_ = false;
    visage::Point lastPos_{};
    float freqAccum_ = 0.0f;
    float resAccum_ = 0.0f;
};

class ZPlanePanel final : public Panel {
  public:
    explicit ZPlanePanel(Context& context)
        : Panel(context), pad_(add<ZPlanePad>(context)), zRail_(add<ZDepthRail>(context)),
          response_(add<ResponsePlot>(context)), cube_(add<Row>(context, ParamId::cube, colour::poles, "Cube")),
          topology_(add<Row>(context, ParamId::topology, colour::poles, "Topology")),
          stages_(add<Row>(context, ParamId::stages, colour::poles, "Poles")),
          transform_(add<Row>(context, ParamId::transform, colour::poles, "Transform", true)),
          spread_(add<Row>(context, ParamId::spread, colour::poles, "Spread", true)),
          stereoMode_(add<Row>(context, ParamId::stereoMode, colour::poles, "Routing")),
          pitchTrack_(add<Row>(context, ParamId::pitchTrack, colour::sub, "Pitch track")),
          glide_(add<Row>(context, ParamId::glide, colour::sub, "Glide", true)),
          fmAmount_(add<Row>(context, ParamId::fmAmount, colour::drive, "FM", true)),
          fmSource_(add<Row>(context, ParamId::fmSource, colour::drive, "FM src")),
          drivePos_(add<Row>(context, ParamId::drivePos, colour::drive, "Drive pos")),
          foldType_(add<Row>(context, ParamId::foldType, colour::drive, "Shaper")),
          asym_(add<Row>(context, ParamId::asym, colour::drive, "Asym", true)),
          subLock_(add<Row>(context, ParamId::subLock, colour::sub, "Sub lock")),
          subSplit_(add<Row>(context, ParamId::subSplit, colour::sub, "Sub split", true)),
          fit_(add<Row>(context, ParamId::fit, colour::sub, "Fit", true)) {}

    void tick() override {
        pad_.redraw();
        zRail_.redraw();
        response_.redraw();
        redraw();
    }

    void resized() override {
        const float pad = px(metric::pad);
        const float gap = px(metric::gap);
        const float panelGap = px(metric::margin);

        // Left: Z-plane pad + Z rail
        const float leftW = px(kLeftWidth) - 2.0f * pad;
        const float railW = px(32.0f);
        const float padW = leftW - railW - gap;
        const float padH = height() - 2.0f * pad;
        pad_.setBounds(pad, pad, padW, padH);
        zRail_.setBounds(pad + padW + gap, pad, railW, padH);

        // Center: Interactive transfer response plot above axis + telemetry footer
        const float midX = px(kLeftWidth) + panelGap;
        const float midW = width() - px(kLeftWidth) - px(kRightWidth) - 2.0f * panelGap;
        const float footH = px(82.0f);
        const float footGap = px(10.0f);
        const float axisH = px(16.0f);
        const float plotH = (height() - 2.0f * pad) - footH - footGap - axisH;
        response_.setBounds(midX + pad, pad, midW - 2.0f * pad, plotH);

        // Right: Parameter control column (16 compact rows across 4 logical sections)
        const float rightLeft = width() - px(kRightWidth) + pad;
        const float colW = px(kRightWidth) - 2.0f * pad;
        const float rowH = px(20.5f);
        const float rowGap = px(3.0f);
        const float step = rowH + rowGap;
        const float sectionGap = px(12.0f);

        float y = pad;
        for (Row* r : {&cube_, &topology_, &stages_, &transform_, &spread_, &stereoMode_}) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
        div1Y_ = std::round(y + (sectionGap - rowGap) * 0.5f);
        y += sectionGap;
        for (Row* r : {&pitchTrack_, &glide_, &fmAmount_, &fmSource_}) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
        div2Y_ = std::round(y + (sectionGap - rowGap) * 0.5f);
        y += sectionGap;
        for (Row* r : { &drivePos_, &foldType_, &asym_ }) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
        div3Y_ = std::round(y + (sectionGap - rowGap) * 0.5f);
        y += sectionGap;
        for (Row* r : { &subLock_, &subSplit_, &fit_ }) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
    }

    void draw(visage::Canvas& canvas) override {
        const float gap = px(metric::margin);
        const float leftW = px(kLeftWidth);
        const float rightW = px(kRightWidth);
        const float midX = leftW + gap;
        const float midW = width() - leftW - rightW - 2.0f * gap;
        const float rightX = width() - rightW;

        drawPanel(canvas, context_, 0.0f, 0.0f, leftW, height());
        drawPanel(canvas, context_, midX, 0.0f, midW, height());
        drawPanel(canvas, context_, rightX, 0.0f, rightW, height());

        // Right panel section dividers
        const float pad = px(metric::pad);
        const float line = std::max(1.0f, std::round(px(metric::hairline)));
        canvas.setColor(colour::edge);
        canvas.rectangle(rightX + pad, div1Y_, rightW - 2.0f * pad, line);
        canvas.rectangle(rightX + pad, div2Y_, rightW - 2.0f * pad, line);
        canvas.rectangle(rightX + pad, div3Y_, rightW - 2.0f * pad, line);

        drawCenterChrome(canvas, midX + pad, pad, midW - 2.0f * pad, height() - 2.0f * pad);
    }

private:
  void drawCenterChrome(visage::Canvas& canvas, float x, float y, float w, float h) {
      const Visuals& vis = context_.model.visuals();
      const float footH = px(82.0f);
      const float footGap = px(10.0f);
      const float respH = h - footH - footGap;
      const float axisH = px(16.0f);
      const float plotH = respH - axisH;
      const float line = std::max(1.0f, std::round(px(metric::hairline)));

      // Frequency axis labels under ResponsePlot
      struct GridMark {
          float hz;
          const char* label;
      };
      constexpr std::array<GridMark, 5> kMarks{
          {{60.0f, "60"}, {200.0f, "200"}, {1000.0f, "1k"}, {4000.0f, "4k"}, {12000.0f, "12k"}}};
      for (const auto& m : kMarks) {
          const float gx = std::round(x + w * hzToX(m.hz));
          drawLabel(canvas, context_, m.label, gx - px(20.0f), y + plotH + px(2.0f), px(40.0f), axisH,
                    colour::textFaint, visage::Font::kCenter, type::small);
      }

      const int cubeIdx = static_cast<int>(context_.model.get(ParamId::cube) + 0.5f);
      const int stages = kStageCounts[static_cast<std::size_t>(
          std::clamp(static_cast<int>(context_.model.get(ParamId::stages) + 0.5f), 0, 3))];
      const int topology = std::clamp(static_cast<int>(context_.model.get(ParamId::topology) + 0.5f), 0, 2);
      const bool subOn = context_.model.get(ParamId::subLock) >= 0.5f;
      const float subHz = context_.model.get(ParamId::subSplit);

      // Lower telemetry strip: Stereo Goniometer + Correlation + Lattice/FM/Env Readout
      const float footY = y + respH + footGap;
      drawPanel(canvas, context_, x, footY, w, footH, colour::raised, metric::radiusSmall);

      const float gSide = footH - px(14.0f);
      const float gX = x + px(12.0f);
      const float gY = footY + px(7.0f);
      const float gCx = gX + gSide * 0.5f;

      canvas.setColor(colour::track);
      canvas.rectangle(std::round(gCx), gY, line, gSide);
      canvas.segment(gX, gY, gX + gSide, gY + gSide, line, false);
      canvas.segment(gX, gY + gSide, gX + gSide, gY, line, false);

      const float scale = 0.5f * gSide * kSqrtHalf;
      const float half = 0.5f * gSide;
      visage::Path trace;
      for (std::size_t i = 0; i < vis.gonioLeft.size(); ++i) {
          const float l = vis.gonioLeft[i];
          const float r = vis.gonioRight[i];
          const float across = std::clamp((r - l) * kSqrtHalf * scale, -half, half);
          const float up = std::clamp((l + r) * kSqrtHalf * scale, -half, half);
          const float tx = half + across;
          const float ty = half - up;
          if (i == 0)
              trace.moveTo(tx, ty);
          else
              trace.lineTo(tx, ty);
      }
      canvas.setColor(colour::withAlpha(colour::text, 0.72f));
      canvas.stroke(trace, gX, gY, gSide, gSide, px(1.0f));

      // Correlation & Live Pitch/Lattice/FM/Env Status
      const float infoX = gX + gSide + px(16.0f);
      const float infoW = x + w - infoX - px(12.0f);
      const float corr = std::round(std::clamp(vis.correlation, -1.0f, 1.0f) * 100.0f) / 100.0f;
      char buf[64];
      std::snprintf(buf, sizeof buf, "Phase  %+.2f", static_cast<double>(corr == 0.0f ? 0.0f : corr));
      drawLabel(canvas, context_, buf, infoX, footY + px(11.0f), infoW * 0.52f, px(18.0f),
                corr < 0.0f ? colour::alert : colour::text, visage::Font::kLeft, type::label);

      const std::string topoName(choices::topology[static_cast<std::size_t>(topology)]);
      std::snprintf(buf, sizeof buf, "%dP  %s", stages * 2, topoName.c_str());
      drawLabel(canvas, context_, buf, infoX + infoW * 0.52f, footY + px(11.0f), infoW * 0.48f, px(18.0f),
                colour::poles, visage::Font::kRight, type::label);

      const bool fmActive = vis.effFmAmount > 0.01f;
      if (vis.activeNote >= 0)
          std::snprintf(buf, sizeof buf, "MIDI  %s (%+.1f st)", noteName(vis.activeNote).c_str(),
                        static_cast<double>(vis.midiShiftSt));
      else
          std::snprintf(buf, sizeof buf, "Cube  %s", std::string(cubeAt(cubeIdx).name).c_str());
      drawLabel(canvas, context_, buf, infoX, footY + px(33.0f), fmActive ? infoW * 0.66f : infoW, px(18.0f),
                vis.activeNote >= 0 ? colour::sub : colour::textDim, visage::Font::kLeft, type::label);

      if (fmActive) {
          std::snprintf(buf, sizeof buf, "FM  %.0f%%", static_cast<double>(vis.effFmAmount * 100.0f));
          drawLabel(canvas, context_, buf, infoX + infoW * 0.66f, footY + px(33.0f), infoW * 0.34f, px(18.0f),
                    colour::drive, visage::Font::kRight, type::small);
      }

      std::snprintf(buf, sizeof buf, "Sub  %s",
                    subOn ? formatValue(spec(ParamId::subSplit), subHz).c_str() : "Full band");
      drawLabel(canvas, context_, buf, infoX, footY + px(55.0f), infoW * 0.52f, px(16.0f),
                subOn ? colour::sub : colour::textFaint, visage::Font::kLeft, type::small);

      // Slim live envelope meter bar on right half of line 3
      const float envLabelW = px(28.0f);
      const float envStartX = infoX + infoW * 0.54f;
      drawLabel(canvas, context_, "Env", envStartX, footY + px(55.0f), envLabelW, px(16.0f), colour::textFaint,
                visage::Font::kLeft, type::small);
      const float barX = envStartX + envLabelW + px(4.0f);
      const float barW = std::max(px(16.0f), infoX + infoW - barX);
      const float barH = px(4.0f);
      const float barY = footY + px(55.0f) + (px(16.0f) - barH) * 0.5f;
      canvas.setColor(colour::track);
      canvas.roundedRectangle(barX, barY, barW, barH, barH * 0.5f);
      const float envFill = barW * std::clamp(vis.envLevel, 0.0f, 1.0f);
      if (envFill > 0.5f) {
          canvas.setColor(colour::sub);
          canvas.roundedRectangle(barX, barY, envFill, barH, barH * 0.5f);
      }
  }

    ZPlanePad& pad_;
    ZDepthRail& zRail_;
    ResponsePlot& response_;
    Row& cube_;
    Row& topology_;
    Row& stages_;
    Row& transform_;
    Row& spread_;
    Row& stereoMode_;
    Row& pitchTrack_;
    Row& glide_;
    Row& fmAmount_;
    Row& fmSource_;
    Row& drivePos_;
    Row& foldType_;
    Row& asym_;
    Row& subLock_;
    Row& subSplit_;
    Row& fit_;
    float div1Y_ = 0.0f;
    float div2Y_ = 0.0f;
    float div3Y_ = 0.0f;
};
}

std::unique_ptr<Panel> makeZPlanePanel(Context& context) { return std::make_unique<ZPlanePanel>(context); }

}
