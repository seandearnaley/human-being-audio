// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <hba/dsp/envelope_curve.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace dt::ui {

namespace {

enum class ViewMode : int { wave = 0, fft = 1, sublink = 2 };
enum class CurveFocus : int { pitch = 0, amp = 1, both = 2 };

class StagePanel final : public visage::Frame, public Live {
  public:
    explicit StagePanel(Context& ctx)
        : context_(ctx), modeWave_(ctx, "Wave", [this] { setMode(ViewMode::wave); }),
          modeFft_(ctx, "FFT", [this] { setMode(ViewMode::fft); }),
          modeSubLink_(ctx, "Sub-Link", [this] { setMode(ViewMode::sublink); }),
          curvePitch_(ctx, "Pitch", [this] { setFocus(CurveFocus::pitch); }),
          curveAmp_(ctx, "Amp", [this] { setFocus(CurveFocus::amp); }),
          curveBoth_(ctx, "Both", [this] { setFocus(CurveFocus::both); }),
          resetCurve_(ctx, "Reset", [this] { resetActiveCurves(); }) {
        for (auto* b : {&modeWave_, &modeFft_, &modeSubLink_, &curvePitch_, &curveAmp_, &curveBoth_, &resetCurve_}) {
            b->setFontSize(9.8f);
            addChild(b);
        }
        resetCurve_.setAccent(colour::textDim);
        setMode(ViewMode::wave);
        setFocus(CurveFocus::both);
        context_.live.push_back(this);
        tick();
    }

    ~StagePanel() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto& p = context_.model.preview();
        const auto& v = context_.model.visuals();
        const auto pc = context_.model.pitchCurve();
        const auto ac = context_.model.ampCurve();
        const float tune = context_.model.get(ParamId::tuneHz);
        const float boxHz = context_.model.get(ParamId::boxCutHz);
        const float boxDb = context_.model.get(ParamId::boxCutDb);
        const bool zc = context_.model.get(ParamId::zeroCross) >= 0.5f;

        if (std::abs(p.truePeakDb - lastPeak_) > 0.02f || std::abs(p.effectiveLengthMs - lastLen_) > 0.05f ||
            std::abs(p.crestDb - lastCrest_) > 0.02f || std::abs(p.dcPercent - lastDc_) > 0.02f ||
            std::abs(tune - lastTune_) > 0.01f || std::abs(boxHz - lastBoxHz_) > 0.1f ||
            std::abs(boxDb - lastBoxDb_) > 0.05f || zc != lastZc_ || !(pc == lastPitch_) || !(ac == lastAmp_) ||
            v.scActive != lastScActive_ || std::abs(v.scCorrelation - lastScCorr_) > 0.005f || v.sounding) {
            lastPeak_ = p.truePeakDb;
            lastLen_ = p.effectiveLengthMs;
            lastCrest_ = p.crestDb;
            lastDc_ = p.dcPercent;
            lastTune_ = tune;
            lastBoxHz_ = boxHz;
            lastBoxDb_ = boxDb;
            lastZc_ = zc;
            lastPitch_ = pc;
            lastAmp_ = ac;
            lastScActive_ = v.scActive;
            lastScCorr_ = v.scCorrelation;
            fftDirty_ = true;
            redraw();
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float bh = px(22.0f);
        const float y = px(7.0f);
        float x = px(10.0f);

        modeWave_.setBounds(x, y, px(48.0f), bh);
        x += px(50.0f);
        modeFft_.setBounds(x, y, px(40.0f), bh);
        x += px(42.0f);
        modeSubLink_.setBounds(x, y, px(64.0f), bh);
        x += px(76.0f);

        curvePitch_.setBounds(x, y, px(46.0f), bh);
        x += px(48.0f);
        curveAmp_.setBounds(x, y, px(42.0f), bh);
        x += px(44.0f);
        curveBoth_.setBounds(x, y, px(44.0f), bh);
        x += px(50.0f);
        resetCurve_.setBounds(x, y, px(44.0f), bh);
    }

    void draw(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);

        // Right-aligned live numeric metrology badges in top HUD strip
        drawMetrologyBadges(canvas);

        const PlotRect pr = plotRect();
        drawPanel(canvas, context_, pr.x, pr.y, pr.w, pr.h, colour::window, metric::radiusSmall);

        // Subtle grid
        drawGrid(canvas, pr);

        if (mode_ == ViewMode::fft) {
            drawFftPlot(canvas, pr);
        } else if (mode_ == ViewMode::sublink) {
            drawSubLinkPlot(canvas, pr);
        } else {
            drawWavePlot(canvas, pr);
        }

        // Interactive Pitch & Amp splines (over Wave and Sub-Link), neatly inset from legend & tick bands
        if (mode_ != ViewMode::fft) {
            const PlotRect cr = curveRect(pr);
            if (focus_ == CurveFocus::amp || focus_ == CurveFocus::both)
                drawSpline(canvas, cr, context_.model.ampCurve(), colour::impact, true);
            if (focus_ == CurveFocus::pitch || focus_ == CurveFocus::both)
                drawSpline(canvas, cr, context_.model.pitchCurve(), colour::body, false);
        }

        // Crisp subtle legend dots at top-left inside plot (names only, no prose)
        const float lx = pr.x + 10.0f * sc;
        const float ly = pr.y + 7.0f * sc;
        if (mode_ == ViewMode::wave) {
            drawKeyDot(canvas, lx, ly, colour::sub, "SUB");
            drawKeyDot(canvas, lx + 46.0f * sc, ly, colour::body, "BODY");
            drawKeyDot(canvas, lx + 102.0f * sc, ly, colour::impact, "CLICK");
            drawKeyDot(canvas, lx + 158.0f * sc, ly, colour::text, "SUM");
        } else if (mode_ == ViewMode::sublink) {
            drawKeyDot(canvas, lx, ly, colour::sub, "KICK");
            drawKeyDot(canvas, lx + 50.0f * sc, ly, colour::sidechain, "SIDECHAIN");
            drawKeyDot(canvas, lx + 132.0f * sc, ly, colour::text, "SUM");
        } else {
            drawKeyDot(canvas, lx, ly, colour::sub, "SPECTRUM");
            drawKeyDot(canvas, lx + 82.0f * sc, ly, colour::body, "BOX-CUT");
        }
    }

    void mouseMove(const visage::MouseEvent& e) override {
        if (mode_ == ViewMode::fft)
            return;
        const Hit h = hitSpline(e.position.x, e.position.y);
        if (h.kind != hover_.kind || h.isAmp != hover_.isAmp || h.index != hover_.index) {
            hover_ = h;
            redraw();
        }
    }

    void mouseExit(const visage::MouseEvent&) override {
        if (hover_.kind != HitKind::none) {
            hover_ = {};
            redraw();
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (mode_ == ViewMode::fft)
            return;
        const PlotRect pr = plotRect();
        if (!pr.contains(e.position.x, e.position.y))
            return;

        const Hit h = hitSpline(e.position.x, e.position.y);

        if (e.shouldTriggerPopup()) {
            if (h.kind == HitKind::node) {
                auto curve = h.isAmp ? context_.model.ampCurve() : context_.model.pitchCurve();
                if (h.index > 0 && h.index < curve.count - 1) {
                    hba::dsp::removeEnvelopeNode(curve, h.index);
                    commitCurve(h.isAmp, curve);
                } else {
                    auto& seg = curve.nodes[static_cast<std::size_t>(h.index)].seg;
                    seg = static_cast<hba::dsp::EnvSegment>((static_cast<int>(seg) + 1) % 3);
                    commitCurve(h.isAmp, curve);
                }
            }
            return;
        }

        if (e.repeatClickCount() >= 2) {
            if (h.kind == HitKind::node && h.index > 0) {
                auto curve = h.isAmp ? context_.model.ampCurve() : context_.model.pitchCurve();
                if (h.index < curve.count - 1) {
                    hba::dsp::removeEnvelopeNode(curve, h.index);
                    commitCurve(h.isAmp, curve);
                }
                return;
            }
            // Insert new node on focused curve (or closer curve when Both)
            const PlotRect cr = curveRect(pr);
            const float u = std::clamp((e.position.x - cr.x) / std::max(1.0f, cr.w), 0.005f, 0.995f);
            const float v = std::clamp(1.0f - (e.position.y - cr.y) / std::max(1.0f, cr.h), 0.0f, 1.0f);
            bool targetAmp = (focus_ == CurveFocus::amp);
            if (focus_ == CurveFocus::both) {
                const float py = static_cast<float>(hba::dsp::evaluateEnvelope(context_.model.pitchCurve(), u));
                const float ay = static_cast<float>(hba::dsp::evaluateEnvelope(context_.model.ampCurve(), u));
                targetAmp = std::abs(ay - v) < std::abs(py - v);
            }
            auto curve = targetAmp ? context_.model.ampCurve() : context_.model.pitchCurve();
            const int inserted =
                hba::dsp::insertEnvelopeNode(curve, u, v, targetAmp ? -0.15f : -0.45f,
                                             targetAmp ? hba::dsp::EnvSegment::smooth : hba::dsp::EnvSegment::exp);
            commitCurve(targetAmp, curve);
            if (inserted >= 0) {
                drag_ = {HitKind::node, targetAmp, inserted};
                dragStartY_ = e.position.y;
                dragStartTension_ = curve.nodes[static_cast<std::size_t>(inserted)].tension;
            }
            return;
        }

        if (h.kind != HitKind::none) {
            drag_ = h;
            dragStartY_ = e.position.y;
            const auto curve = h.isAmp ? context_.model.ampCurve() : context_.model.pitchCurve();
            if (h.index >= 0 && h.index < curve.count)
                dragStartTension_ = curve.nodes[static_cast<std::size_t>(h.index)].tension;
            redraw();
        }
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (drag_.kind == HitKind::none)
            return;
        const PlotRect cr = curveRect(plotRect());
        auto curve = drag_.isAmp ? context_.model.ampCurve() : context_.model.pitchCurve();
        if (drag_.kind == HitKind::node && drag_.index >= 0 && drag_.index < curve.count) {
            const float u = std::clamp((e.position.x - cr.x) / std::max(1.0f, cr.w), 0.0f, 1.0f);
            const float v = std::clamp(1.0f - (e.position.y - cr.y) / std::max(1.0f, cr.h), 0.0f, 1.0f);
            hba::dsp::moveEnvelopeNode(curve, drag_.index, u, v);
            commitCurve(drag_.isAmp, curve);
        } else if (drag_.kind == HitKind::segment && drag_.index >= 0 && drag_.index < curve.count - 1) {
            const float dy = (e.position.y - dragStartY_) / std::max(24.0f, cr.h * 0.45f);
            const auto& a = curve.nodes[static_cast<std::size_t>(drag_.index)];
            const auto& b = curve.nodes[static_cast<std::size_t>(drag_.index + 1)];
            const float sign = (b.y < a.y) ? -1.0f : 1.0f;
            curve.nodes[static_cast<std::size_t>(drag_.index)].tension =
                std::clamp(dragStartTension_ + sign * dy, -1.0f, 1.0f);
            commitCurve(drag_.isAmp, curve);
        }
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (drag_.kind != HitKind::none) {
            drag_ = {};
            redraw();
        }
    }

  private:
    struct PlotRect {
        float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    enum class HitKind { none, node, segment };
    struct Hit {
        HitKind kind = HitKind::none;
        bool isAmp = false;
        int index = -1;
    };

    PlotRect plotRect() const {
        const float sc = context_.scale;
        return {10.0f * sc, 35.0f * sc, std::max(40.0f * sc, width() - 20.0f * sc),
                std::max(40.0f * sc, height() - 45.0f * sc)};
    }

    PlotRect curveRect(const PlotRect& pr) const {
        const float sc = context_.scale;
        const float top = 24.0f * sc;
        const float bot = 18.0f * sc;
        const float side = 8.0f * sc;
        return {pr.x + side, pr.y + top, std::max(24.0f * sc, pr.w - 2.0f * side),
                std::max(24.0f * sc, pr.h - top - bot)};
    }

    void setMode(ViewMode m) {
        mode_ = m;
        modeWave_.setAccent(m == ViewMode::wave ? colour::sub : colour::textDim);
        modeFft_.setAccent(m == ViewMode::fft ? colour::impact : colour::textDim);
        modeSubLink_.setAccent(m == ViewMode::sublink ? colour::sidechain : colour::textDim);
        modeWave_.setActive(m == ViewMode::wave);
        modeFft_.setActive(m == ViewMode::fft);
        modeSubLink_.setActive(m == ViewMode::sublink);
        const bool showCurveCtrls = (m != ViewMode::fft);
        curvePitch_.setVisible(showCurveCtrls);
        curveAmp_.setVisible(showCurveCtrls);
        curveBoth_.setVisible(showCurveCtrls);
        resetCurve_.setVisible(showCurveCtrls);
        redraw();
    }

    void setFocus(CurveFocus f) {
        focus_ = f;
        curvePitch_.setAccent(f == CurveFocus::pitch ? colour::body : colour::textDim);
        curveAmp_.setAccent(f == CurveFocus::amp ? colour::impact : colour::textDim);
        curveBoth_.setAccent(f == CurveFocus::both ? colour::text : colour::textDim);
        curvePitch_.setActive(f == CurveFocus::pitch);
        curveAmp_.setActive(f == CurveFocus::amp);
        curveBoth_.setActive(f == CurveFocus::both);
        redraw();
    }

    void resetActiveCurves() {
        if (focus_ == CurveFocus::pitch || focus_ == CurveFocus::both)
            context_.model.setPitchCurve(hba::dsp::defaultKickPitchCurve());
        if (focus_ == CurveFocus::amp || focus_ == CurveFocus::both)
            context_.model.setAmpCurve(hba::dsp::defaultKickAmpCurve());
        redraw();
    }

    void commitCurve(bool isAmp, const hba::dsp::EnvelopeCurve& c) {
        if (isAmp)
            context_.model.setAmpCurve(c);
        else
            context_.model.setPitchCurve(c);
        redraw();
    }

    Hit hitSpline(float mx, float my) const {
        const PlotRect pr = curveRect(plotRect());
        const float nodeRad = context_.px(11.0f);
        const float segRad = context_.px(9.0f);

        auto testCurve = [&](bool isAmp, const hba::dsp::EnvelopeCurve& c) -> Hit {
            for (int i = 0; i < c.count; ++i) {
                const auto& n = c.nodes[static_cast<std::size_t>(i)];
                const float nx = pr.x + n.t * pr.w;
                const float ny = pr.y + (1.0f - n.y) * pr.h;
                const float dx = mx - nx;
                const float dy = my - ny;
                if (dx * dx + dy * dy <= nodeRad * nodeRad)
                    return {HitKind::node, isAmp, i};
            }
            for (int i = 0; i < c.count - 1; ++i) {
                const auto& a = c.nodes[static_cast<std::size_t>(i)];
                const auto& b = c.nodes[static_cast<std::size_t>(i + 1)];
                const float midT = 0.5f * (a.t + b.t);
                const float midY = static_cast<float>(hba::dsp::evaluateEnvelope(c, midT));
                const float sx = pr.x + midT * pr.w;
                const float sy = pr.y + (1.0f - midY) * pr.h;
                const float dx = mx - sx;
                const float dy = my - sy;
                if (dx * dx + dy * dy <= segRad * segRad)
                    return {HitKind::segment, isAmp, i};
            }
            return {};
        };

        if (focus_ == CurveFocus::pitch || focus_ == CurveFocus::both) {
            if (auto h = testCurve(false, context_.model.pitchCurve()); h.kind != HitKind::none)
                return h;
        }
        if (focus_ == CurveFocus::amp || focus_ == CurveFocus::both) {
            if (auto h = testCurve(true, context_.model.ampCurve()); h.kind != HitKind::none)
                return h;
        }
        return {};
    }

    void drawKeyDot(visage::Canvas& canvas, float x, float y, unsigned argb, const char* label) const {
        const float sc = context_.scale;
        canvas.setColor(argb);
        canvas.circle(x, y + 3.0f * sc, 5.0f * sc);
        drawLabel(canvas, context_, label, x + 8.0f * sc, y, 70.0f * sc, 11.0f * sc, colour::textDim,
                  visage::Font::kLeft, 8.6f, Weight::semibold);
    }

    void drawBadge(visage::Canvas& canvas, float& rightX, float y, float h, const std::string& text,
                   unsigned accent) const {
        const float sc = context_.scale;
        const float w = std::max(64.0f, static_cast<float>(text.size()) * 6.4f + 16.0f) * sc;
        rightX -= w;
        canvas.setColor(colour::raised);
        canvas.roundedRectangle(rightX, y, w, h, 4.0f * sc);
        canvas.setColor(colour::withAlpha(accent, 0.36f));
        canvas.roundedRectangleBorder(rightX, y, w, h, 4.0f * sc, 1.0f * sc);
        drawLabel(canvas, context_, text, rightX + 4.0f * sc, y, w - 8.0f * sc, h, accent, visage::Font::kCenter, 9.4f,
                  Weight::semibold);
        rightX -= 6.0f * sc;
    }

    void drawMetrologyBadges(visage::Canvas& canvas) const {
        const float sc = context_.scale;
        const auto& p = context_.model.preview();
        const auto& v = context_.model.visuals();
        float rx = width() - 10.0f * sc;
        const float y = 7.0f * sc;
        const float h = 22.0f * sc;

        char buf[48];
        if (v.scActive) {
            std::snprintf(buf, sizeof buf, "SC %+.2f", static_cast<double>(v.scCorrelation));
            drawBadge(canvas, rx, y, h, buf, v.scCorrelation >= 0.55f ? colour::sub : colour::sidechain);
        }

        std::snprintf(buf, sizeof buf, "DC %.1f%%", static_cast<double>(p.dcPercent));
        drawBadge(canvas, rx, y, h, buf, p.dcPercent > 4.0f ? colour::alert : colour::textDim);

        std::snprintf(buf, sizeof buf, "CREST %.1f dB", static_cast<double>(p.crestDb));
        drawBadge(canvas, rx, y, h, buf, colour::body);

        std::snprintf(buf, sizeof buf, "%.0f ms", static_cast<double>(p.effectiveLengthMs));
        drawBadge(canvas, rx, y, h, buf, colour::impact);

        const float tuneHz = context_.model.get(ParamId::tuneHz);
        std::snprintf(buf, sizeof buf, "%.1f Hz \xc2\xb7 %s", static_cast<double>(tuneHz),
                      noteNameForHz(tuneHz).c_str());
        drawBadge(canvas, rx, y, h, buf, colour::sub);
    }

    void drawGrid(visage::Canvas& canvas, const PlotRect& pr) const {
        const float sc = context_.scale;
        const auto& p = context_.model.preview();

        // Horizontal datum lines
        canvas.setColor(colour::withAlpha(colour::edge, 0.55f));
        canvas.rectangle(pr.x, pr.y + pr.h * 0.25f, pr.w, 1.0f * sc);
        canvas.rectangle(pr.x, pr.y + pr.h * 0.75f, pr.w, 1.0f * sc);
        canvas.setColor(colour::withAlpha(colour::textDim, 0.26f));
        canvas.rectangle(pr.x, pr.y + pr.h * 0.50f, pr.w, 1.0f * sc);

        // Vertical time (or log-Hz) ticks
        for (int i = 1; i <= 4; ++i) {
            const float u = static_cast<float>(i) * 0.25f;
            const float gx = pr.x + u * pr.w;
            if (i < 4) {
                canvas.setColor(colour::withAlpha(colour::edge, 0.55f));
                canvas.rectangle(gx, pr.y, 1.0f * sc, pr.h);
            }
            if (mode_ != ViewMode::fft) {
                char msBuf[20];
                std::snprintf(msBuf, sizeof msBuf, "%.0fms", static_cast<double>(p.effectiveLengthMs * u));
                drawLabel(canvas, context_, msBuf, gx - 48.0f * sc, pr.y + pr.h - 14.0f * sc, 40.0f * sc, 12.0f * sc,
                          colour::textFaint, visage::Font::kRight, 8.6f, Weight::medium);
            }
        }
    }

    void drawWavePlot(visage::Canvas& canvas, const PlotRect& pr) const {
        const float sc = context_.scale;
        const auto& p = context_.model.preview();
        const PlotRect cr = curveRect(pr);

        drawCurve(canvas, context_, p.subWave, cr.x, cr.y, cr.w, cr.h, colour::sub, -1.08f, 1.08f, 0.16f, 0.0f, 1.2f);
        drawCurve(canvas, context_, p.bodyWave, cr.x, cr.y, cr.w, cr.h, colour::body, -1.08f, 1.08f, 0.10f, 0.0f, 1.1f);
        drawCurve(canvas, context_, p.clickWave, cr.x, cr.y, cr.w, cr.h, colour::impact, -1.08f, 1.08f, 0.0f, 0.0f,
                  1.1f);
        drawCurve(canvas, context_, p.sumWave, cr.x, cr.y, cr.w, cr.h, colour::text, -1.08f, 1.08f, 0.0f, 0.0f, 1.8f);

        // Zero-X terminal phase lock indicator
        if (context_.model.get(ParamId::zeroCross) >= 0.5f) {
            const float zx = cr.x + std::clamp(p.zeroCrossNorm, 0.05f, 0.995f) * cr.w;
            canvas.setColor(colour::withAlpha(colour::sub, 0.65f));
            canvas.rectangle(zx, cr.y, 1.2f * sc, cr.h);
            const float r = 4.2f * sc;
            canvas.setColor(colour::sub);
            canvas.circle(zx - r, cr.y + cr.h * 0.5f - r, 2.0f * r);
        }
    }

    void drawSubLinkPlot(visage::Canvas& canvas, const PlotRect& pr) const {
        const float sc = context_.scale;
        const auto& v = context_.model.visuals();
        const float padY = 14.0f * sc;
        const float wy = pr.y + padY;
        const float wh = std::max(10.0f * sc, pr.h - 2.0f * padY);

        if (v.scActive) {
            drawCurve(canvas, context_, v.scScope, pr.x, wy, pr.w, wh, colour::sidechain, -1.1f, 1.1f, 0.18f, 0.0f,
                      1.4f);
            drawCurve(canvas, context_, v.kickScope, pr.x, wy, pr.w, wh, colour::sub, -1.1f, 1.1f, 0.12f, 0.0f, 1.3f);
            drawCurve(canvas, context_, v.sumScope, pr.x, wy, pr.w, wh, colour::text, -1.1f, 1.1f, 0.0f, 0.0f, 1.8f);
        } else {
            drawCurve(canvas, context_, context_.model.preview().sumWave, pr.x, wy, pr.w, wh, colour::sub, -1.1f, 1.1f,
                      0.14f, 0.0f, 1.6f);
            drawLabel(canvas, context_, "NO SIDECHAIN", pr.x, pr.y + pr.h * 0.5f - 10.0f * sc, pr.w, 20.0f * sc,
                      colour::withAlpha(colour::sidechain, 0.65f), visage::Font::kCenter, 10.4f, Weight::semibold);
        }
    }

    void drawFftPlot(visage::Canvas& canvas, const PlotRect& pr) const {
        if (fftDirty_) {
            computeFft();
            fftDirty_ = false;
        }
        const float sc = context_.scale;
        const float padY = 18.0f * sc;
        const float fy = pr.y + padY;
        const float fh = std::max(10.0f * sc, pr.h - 2.0f * padY);

        // Log-Hz vertical grid markers: 50, 100, 300, 1k, 4k, 10k
        constexpr std::array<float, 6> kFreqs{50.0f, 100.0f, 300.0f, 1000.0f, 4000.0f, 10000.0f};
        constexpr std::array<const char*, 6> kLabels{"50", "100", "300", "1k", "4k", "10k"};
        const float logMin = std::log(25.0f);
        const float logMax = std::log(14000.0f);

        for (std::size_t i = 0; i < kFreqs.size(); ++i) {
            const float u = (std::log(kFreqs[i]) - logMin) / (logMax - logMin);
            const float gx = pr.x + u * pr.w;
            canvas.setColor(colour::withAlpha(colour::edge, 0.60f));
            canvas.rectangle(gx, pr.y, 1.0f * sc, pr.h);
            drawLabel(canvas, context_, kLabels[i], gx + 3.0f * sc, pr.y + pr.h - 14.0f * sc, 34.0f * sc, 12.0f * sc,
                      colour::textFaint, visage::Font::kLeft, 8.6f, Weight::medium);
        }

        drawCurve(canvas, context_, fftBins_, pr.x, fy, pr.w, fh, colour::sub, 0.0f, 1.0f, 0.24f, 0.0f, 1.8f);

        // Fundamental Tune vertical hairline
        const float tuneHz = std::clamp(context_.model.get(ParamId::tuneHz), 28.0f, 120.0f);
        const float tu = std::clamp((std::log(tuneHz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
        canvas.setColor(colour::withAlpha(colour::impact, 0.80f));
        canvas.rectangle(pr.x + tu * pr.w, pr.y + 4.0f * sc, 1.4f * sc, pr.h - 8.0f * sc);

        // Box-Cut notch marker
        const float boxDb = context_.model.get(ParamId::boxCutDb);
        if (boxDb > 0.2f) {
            const float boxHz = context_.model.get(ParamId::boxCutHz);
            const float bu = std::clamp((std::log(boxHz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
            const float bx = pr.x + bu * pr.w;
            const float dip = std::clamp(boxDb / 18.0f, 0.0f, 1.0f) * fh * 0.45f;
            canvas.setColor(colour::withAlpha(colour::body, 0.72f));
            canvas.rectangle(bx - 1.0f * sc, fy, 2.0f * sc, dip);
            canvas.circle(bx - 3.5f * sc, fy + dip - 3.5f * sc, 7.0f * sc);
        }
    }

    void drawSpline(visage::Canvas& canvas, const PlotRect& pr, const hba::dsp::EnvelopeCurve& c, unsigned argb,
                    bool isAmp) const {
        const float sc = context_.scale;
        constexpr int kPts = 128;
        std::array<float, kPts> curvePts{};
        for (int i = 0; i < kPts; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(kPts - 1);
            curvePts[static_cast<std::size_t>(i)] = static_cast<float>(hba::dsp::evaluateEnvelope(c, u));
        }

        // Glow + crisp line
        drawCurve(canvas, context_, curvePts, pr.x, pr.y, pr.w, pr.h, colour::withAlpha(argb, 0.24f), 0.0f, 1.0f, 0.0f,
                  0.0f, 4.4f);
        drawCurve(canvas, context_, curvePts, pr.x, pr.y, pr.w, pr.h, argb, 0.0f, 1.0f, 0.0f, 0.0f, 1.9f);

        // Segment tension midpoint handles
        for (int i = 0; i < c.count - 1; ++i) {
            const auto& a = c.nodes[static_cast<std::size_t>(i)];
            const auto& b = c.nodes[static_cast<std::size_t>(i + 1)];
            const float midT = 0.5f * (a.t + b.t);
            const float midY = static_cast<float>(hba::dsp::evaluateEnvelope(c, midT));
            const float sx = pr.x + midT * pr.w;
            const float sy = pr.y + (1.0f - midY) * pr.h;
            const bool hot = (hover_.kind == HitKind::segment && hover_.isAmp == isAmp && hover_.index == i) ||
                             (drag_.kind == HitKind::segment && drag_.isAmp == isAmp && drag_.index == i);
            const float r = (hot ? 4.4f : 2.8f) * sc;
            canvas.setColor(colour::window);
            canvas.circle(sx - r, sy - r, 2.0f * r);
            canvas.setColor(colour::withAlpha(argb, hot ? 1.0f : 0.72f));
            canvas.ring(sx - r, sy - r, 2.0f * r, 1.4f * sc);
        }

        // Knot handles
        for (int i = 0; i < c.count; ++i) {
            const auto& n = c.nodes[static_cast<std::size_t>(i)];
            const float nx = pr.x + n.t * pr.w;
            const float ny = pr.y + (1.0f - n.y) * pr.h;
            const bool hot = (hover_.kind == HitKind::node && hover_.isAmp == isAmp && hover_.index == i) ||
                             (drag_.kind == HitKind::node && drag_.isAmp == isAmp && drag_.index == i);
            const float outerR = (hot ? 7.6f : 5.6f) * sc;
            const float innerR = (hot ? 4.2f : 3.2f) * sc;
            canvas.setColor(colour::withAlpha(argb, hot ? 0.36f : 0.22f));
            canvas.circle(nx - outerR, ny - outerR, 2.0f * outerR);
            canvas.setColor(colour::window);
            canvas.circle(nx - innerR - 1.0f * sc, ny - innerR - 1.0f * sc, 2.0f * (innerR + 1.0f * sc));
            canvas.setColor(hot ? colour::text : argb);
            canvas.circle(nx - innerR, ny - innerR, 2.0f * innerR);
        }
    }

    void computeFft() const {
        const auto& p = context_.model.preview();
        const double durationSec = std::max(0.04, static_cast<double>(p.effectiveLengthMs) * 0.001);
        const double effFs = static_cast<double>(kScopePoints) / durationSec;
        const double logMin = std::log(25.0);
        const double logMax = std::log(14000.0);

        float maxDb = -60.0f;
        std::array<float, kFftCols> rawDb{};
        for (int col = 0; col < kFftCols; ++col) {
            const double u = static_cast<double>(col) / static_cast<double>(kFftCols - 1);
            const double hz = std::exp(logMin + u * (logMax - logMin));
            const double w = 2.0 * std::numbers::pi * hz / effFs;
            double re = 0.0;
            double im = 0.0;
            for (int n = 0; n < kScopePoints; ++n) {
                const double win = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / (kScopePoints - 1));
                const double s = static_cast<double>(p.sumWave[static_cast<std::size_t>(n)]) * win;
                re += s * std::cos(w * n);
                im -= s * std::sin(w * n);
            }
            const double mag = std::sqrt(re * re + im * im) / static_cast<double>(kScopePoints);
            const float db = static_cast<float>(20.0 * std::log10(std::max(mag, 1.0e-5)));
            rawDb[static_cast<std::size_t>(col)] = db;
            maxDb = std::max(maxDb, db);
        }

        const float ceilDb = std::max(maxDb, -12.0f);
        const float floorDb = ceilDb - 54.0f;
        for (int col = 0; col < kFftCols; ++col) {
            fftBins_[static_cast<std::size_t>(col)] =
                std::clamp((rawDb[static_cast<std::size_t>(col)] - floorDb) / (ceilDb - floorDb), 0.0f, 1.0f);
        }
    }

    static constexpr int kFftCols = 96;

    Context& context_;
    ViewMode mode_ = ViewMode::wave;
    CurveFocus focus_ = CurveFocus::both;

    PillButton modeWave_, modeFft_, modeSubLink_;
    PillButton curvePitch_, curveAmp_, curveBoth_, resetCurve_;

    Hit hover_{};
    Hit drag_{};
    float dragStartY_ = 0.0f;
    float dragStartTension_ = 0.0f;

    float lastPeak_ = -999.0f;
    float lastLen_ = -1.0f;
    float lastCrest_ = -1.0f;
    float lastDc_ = -1.0f;
    float lastTune_ = -1.0f;
    float lastBoxHz_ = -1.0f;
    float lastBoxDb_ = -1.0f;
    bool lastZc_ = true;
    bool lastScActive_ = false;
    float lastScCorr_ = -2.0f;
    hba::dsp::EnvelopeCurve lastPitch_{};
    hba::dsp::EnvelopeCurve lastAmp_{};

    mutable bool fftDirty_ = true;
    mutable std::array<float, kFftCols> fftBins_{};
};

} // namespace

std::unique_ptr<visage::Frame> makeStagePanel(Context& context) { return std::make_unique<StagePanel>(context); }

} // namespace dt::ui
