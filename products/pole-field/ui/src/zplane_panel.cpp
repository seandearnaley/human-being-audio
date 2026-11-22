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

        // Corner labels for the selected cube
        const int cubeIdx = static_cast<int>(context_.model.get(ParamId::cube) + 0.5f);
        const auto& cube = cubeAt(cubeIdx);
        const float lw = width() * 0.48f;
        const float lh = context_.px(16.0f);
        const float inset = context_.px(8.0f);
        drawLabel(canvas, context_, std::string(cube.cornerNames[0]), inset, inset, lw, lh, colour::textFaint,
                  visage::Font::kLeft, type::small);
        drawLabel(canvas, context_, std::string(cube.cornerNames[1]), width() - inset - lw, inset, lw, lh, colour::textFaint,
                  visage::Font::kRight, type::small);
        drawLabel(canvas, context_, std::string(cube.cornerNames[2]), inset, height() - inset - lh, lw, lh, colour::textFaint,
                  visage::Font::kLeft, type::small);
        drawLabel(canvas, context_, std::string(cube.cornerNames[3]), width() - inset - lw, height() - inset - lh, lw, lh,
                  colour::textFaint, visage::Font::kRight, type::small);

        // Compute live conjugate poles and zeros from effective modulated state
        const Visuals& vis = context_.model.visuals();
        const int stages = kStageCounts[static_cast<std::size_t>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::stages) + 0.5f), 0, 3))];
        const ImprintData imp = context_.model.imprint();
        const ResolvedLattice lat = resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ,
                                                   vis.effFreq + vis.midiShiftSt, vis.effRes, vis.effTransform,
                                                   0.0f, &imp);

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

class ZPlanePanel final : public Panel {
public:
    explicit ZPlanePanel(Context& context)
        : Panel(context),
          pad_(add<ZPlanePad>(context)),
          zRail_(add<ZDepthRail>(context)),
          cube_(add<Row>(context, ParamId::cube, colour::poles, "Cube")),
          topology_(add<Row>(context, ParamId::topology, colour::poles, "Topology")),
          stages_(add<Row>(context, ParamId::stages, colour::poles, "Poles")),
          transform_(add<Row>(context, ParamId::transform, colour::poles, "Transform", true)),
          spread_(add<Row>(context, ParamId::spread, colour::poles, "Spread", true)),
          pitchTrack_(add<Row>(context, ParamId::pitchTrack, colour::sub, "Pitch track")),
          glide_(add<Row>(context, ParamId::glide, colour::sub, "Glide", true)),
          drivePos_(add<Row>(context, ParamId::drivePos, colour::drive, "Drive pos")),
          foldType_(add<Row>(context, ParamId::foldType, colour::drive, "Shaper")),
          asym_(add<Row>(context, ParamId::asym, colour::drive, "Asym", true)),
          subLock_(add<Row>(context, ParamId::subLock, colour::sub, "Sub lock")),
          subSplit_(add<Row>(context, ParamId::subSplit, colour::sub, "Sub split", true)),
          fit_(add<Row>(context, ParamId::fit, colour::sub, "Fit", true)) {}

    void tick() override {
        pad_.redraw();
        zRail_.redraw();
        redraw();
    }

    void resized() override {
        const float pad = px(metric::pad);
        const float gap = px(metric::gap);

        // Left: Z-plane pad + Z rail
        const float leftW = px(kLeftWidth) - 2.0f * pad;
        const float railW = px(32.0f);
        const float padW = leftW - railW - gap;
        const float padH = height() - 2.0f * pad;
        pad_.setBounds(pad, pad, padW, padH);
        zRail_.setBounds(pad + padW + gap, pad, railW, padH);

        // Right: Parameter control column
        const float rightLeft = width() - px(kRightWidth) + pad;
        const float colW = px(kRightWidth) - 2.0f * pad;
        const float rowH = px(metric::row);
        const float step = rowH + px(6.0f);
        const float sectionGap = px(12.0f);

        float y = pad;
        for (Row* r : { &cube_, &topology_, &stages_, &transform_, &spread_, &pitchTrack_, &glide_ }) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
        y += sectionGap;
        for (Row* r : { &drivePos_, &foldType_, &asym_ }) {
            r->setBounds(rightLeft, y, colW, rowH);
            y += step;
        }
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
        const float rowStep = px(metric::row + 6.0f);
        const float div1 = std::round(pad + 7.0f * rowStep + px(3.0f));
        const float div2 = std::round(div1 + px(9.0f) + 3.0f * rowStep + px(3.0f));
        canvas.setColor(colour::edge);
        canvas.rectangle(rightX + pad, div1, rightW - 2.0f * pad, line);
        canvas.rectangle(rightX + pad, div2, rightW - 2.0f * pad, line);

        drawCenterDisplay(canvas, midX + pad, pad, midW - 2.0f * pad, height() - 2.0f * pad);
    }

private:
    void drawCenterDisplay(visage::Canvas& canvas, float x, float y, float w, float h) {
        const Visuals& vis = context_.model.visuals();
        const float footH = px(82.0f);
        const float footGap = px(10.0f);
        const float respH = h - footH - footGap;
        const float axisH = px(16.0f);
        const float plotH = respH - axisH;
        const float line = std::max(1.0f, std::round(px(metric::hairline)));

        // Plot backdrop
        drawPanel(canvas, context_, x, y, w, plotH, colour::raised, metric::radiusSmall);

        // Sub Lock cyan shaded region
        const bool subOn = context_.model.get(ParamId::subLock) >= 0.5f;
        const float subHz = context_.model.get(ParamId::subSplit);
        const float subX = std::round(x + w * hzToX(subHz));
        if (subOn) {
            canvas.setColor(colour::withAlpha(colour::sub, 0.11f));
            canvas.rectangle(x + line, y + line, std::max(0.0f, subX - x - line), plotH - 2.0f * line);
            canvas.setColor(colour::withAlpha(colour::sub, 0.65f));
            canvas.rectangle(subX, y + line, line, plotH - 2.0f * line);
        }

        // Frequency grid lines + 0 dB axis
        struct GridMark { float hz; const char* label; };
        constexpr std::array<GridMark, 5> kMarks { {
            { 60.0f, "60" }, { 200.0f, "200" }, { 1000.0f, "1k" }, { 4000.0f, "4k" }, { 12000.0f, "12k" }
        } };
        for (const auto& m : kMarks) {
            const float gx = std::round(x + w * hzToX(m.hz));
            canvas.setColor(colour::track);
            canvas.rectangle(gx, y + line, line, plotH - 2.0f * line);
            drawLabel(canvas, context_, m.label, gx - px(20.0f), y + plotH + px(2.0f), px(40.0f), axisH,
                      colour::textFaint, visage::Font::kCenter, type::small);
        }
        const float zeroDbY = std::round(y + plotH * (1.0f - (0.0f - kRespMinDb) / (kRespMaxDb - kRespMinDb)));
        canvas.setColor(colour::withAlpha(colour::text, 0.14f));
        canvas.rectangle(x + line, zeroDbY, w - 2.0f * line, line);

        // Live output FFT spectrum behind transfer curve
        drawCurve(canvas, context_, vis.spectrumDb, x + line, y + line, w - 2.0f * line, plotH - 2.0f * line,
                  colour::withAlpha(colour::text, 0.24f), kSpecFloorDb, 0.0f, 0.10f, kSpecFloorDb, 1.0f);

        // Exact analytical 14-pole transfer function (L + stereo-diverged R ghost)
        const int cubeIdx = static_cast<int>(context_.model.get(ParamId::cube) + 0.5f);
        const int stages = kStageCounts[static_cast<std::size_t>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::stages) + 0.5f), 0, 3))];
        const int topology = std::clamp(static_cast<int>(context_.model.get(ParamId::topology) + 0.5f), 0, 2);
        const ImprintData imp = context_.model.imprint();
        const float totalFreq = vis.effFreq + vis.midiShiftSt;
        const float stereoSt = vis.effSpread * 5.0f;

        const ResolvedLattice latL = resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ,
                                                    totalFreq, vis.effRes, vis.effTransform, -stereoSt, &imp);
        const ResolvedLattice latR = resolveLattice(cubeIdx, vis.effMorphX, vis.effMorphY, vis.effMorphZ,
                                                    totalFreq, vis.effRes, vis.effTransform, +stereoSt, &imp);

        std::array<float, kResponseBins> curveL {};
        std::array<float, kResponseBins> curveR {};
        for (int i = 0; i < kResponseBins; ++i) {
            const double frac = static_cast<double>(i) / static_cast<double>(kResponseBins - 1);
            const double hz = static_cast<double>(kMinHz) * std::pow(static_cast<double>(kMaxHz / kMinHz), frac);
            float dbL = evaluateMagnitudeDb(latL, stages, topology, hz, vis.sampleRate);
            float dbR = evaluateMagnitudeDb(latR, stages, topology, hz, vis.sampleRate);
            if (subOn && hz < static_cast<double>(subHz)) {
                const float blend = static_cast<float>(std::pow(std::clamp((hz - 0.5 * subHz) / (0.5 * subHz), 0.0, 1.0), 2.0));
                dbL *= blend;
                dbR *= blend;
            }
            curveL[static_cast<std::size_t>(i)] = dbL;
            curveR[static_cast<std::size_t>(i)] = dbR;
        }

        if (stereoSt > 0.05f) {
            drawCurve(canvas, context_, curveR, x + line, y + line, w - 2.0f * line, plotH - 2.0f * line,
                      colour::withAlpha(colour::drive, 0.55f), kRespMinDb, kRespMaxDb, 0.0f, 0.0f, 1.2f);
        }
        drawCurve(canvas, context_, curveL, x + line, y + line, w - 2.0f * line, plotH - 2.0f * line,
                  colour::poles, kRespMinDb, kRespMaxDb, 0.14f, kRespMinDb, 2.0f);

        // Lower telemetry strip: Stereo Goniometer + Correlation + Lattice Readout
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
            if (i == 0) trace.moveTo(tx, ty);
            else trace.lineTo(tx, ty);
        }
        canvas.setColor(colour::withAlpha(colour::text, 0.72f));
        canvas.stroke(trace, gX, gY, gSide, gSide, px(1.0f));

        // Correlation & Live Pitch/Lattice Status
        const float infoX = gX + gSide + px(16.0f);
        const float infoW = x + w - infoX - px(12.0f);
        const float corr = std::round(std::clamp(vis.correlation, -1.0f, 1.0f) * 100.0f) / 100.0f;
        char buf[64];
        std::snprintf(buf, sizeof buf, "Phase  %+.2f", static_cast<double>(corr == 0.0f ? 0.0f : corr));
        drawLabel(canvas, context_, buf, infoX, footY + px(12.0f), infoW * 0.5f, px(18.0f),
                  corr < 0.0f ? colour::alert : colour::text, visage::Font::kLeft, type::label);

        const std::string topoName(choices::topology[static_cast<std::size_t>(topology)]);
        std::snprintf(buf, sizeof buf, "%dP  %s", stages * 2, topoName.c_str());
        drawLabel(canvas, context_, buf, infoX + infoW * 0.5f, footY + px(12.0f), infoW * 0.5f, px(18.0f),
                  colour::poles, visage::Font::kRight, type::label);

        if (vis.activeNote >= 0)
            std::snprintf(buf, sizeof buf, "MIDI  %s (%+.1f st)", noteName(vis.activeNote).c_str(),
                          static_cast<double>(vis.midiShiftSt));
        else
            std::snprintf(buf, sizeof buf, "Cube  %s", std::string(cubeAt(cubeIdx).name).c_str());
        drawLabel(canvas, context_, buf, infoX, footY + px(34.0f), infoW, px(18.0f),
                  vis.activeNote >= 0 ? colour::sub : colour::textDim, visage::Font::kLeft, type::label);

        std::snprintf(buf, sizeof buf, "Sub  %s",
                      subOn ? formatValue(spec(ParamId::subSplit), subHz).c_str() : "Full band");
        drawLabel(canvas, context_, buf, infoX, footY + px(54.0f), infoW, px(18.0f),
                  subOn ? colour::sub : colour::textFaint, visage::Font::kLeft, type::small);
    }

    ZPlanePad& pad_;
    ZDepthRail& zRail_;
    Row& cube_;
    Row& topology_;
    Row& stages_;
    Row& transform_;
    Row& spread_;
    Row& pitchTrack_;
    Row& glide_;
    Row& drivePos_;
    Row& foldType_;
    Row& asym_;
    Row& subLock_;
    Row& subSplit_;
    Row& fit_;
};

}

std::unique_ptr<Panel> makeZPlanePanel(Context& context) { return std::make_unique<ZPlanePanel>(context); }

}
