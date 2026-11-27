// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace dt::ui {

namespace {

// Column 1: 01 CORE (Nord Pure Synthesis)
class CoreColumn final : public Panel {
  public:
    explicit CoreColumn(Context& ctx)
        : Panel(ctx, "01 CORE", colour::sub), coreRow_(ctx, ParamId::coreTopology, colour::sub, "Core", false),
          phaseRow_(ctx, ParamId::startPhase, colour::sub, "Start Phase", true),
          depthRow_(ctx, ParamId::pitchDepth, colour::body, "Pitch Depth", true), harmBars_(ctx, colour::sub),
          tiltRow_(ctx, ParamId::harmTilt, colour::sub, "Tilt", true),
          tensionRow_(ctx, ParamId::tension, colour::sub, "Tension", true),
          cavHzRow_(ctx, ParamId::cavityHz, colour::sub, "Cavity Hz", true),
          cavQRow_(ctx, ParamId::cavityQ, colour::sub, "Cavity", true) {
        for (visage::Frame* c : std::initializer_list<visage::Frame*>{&coreRow_, &phaseRow_, &depthRow_, &harmBars_,
                                                                      &tiltRow_, &tensionRow_, &cavHzRow_, &cavQRow_}) {
            addChild(c);
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float x = px(12.0f);
        const float w = width() - px(24.0f);
        const float rh = px(23.0f);
        const float pitch = px(31.0f);
        float y = px(34.0f);

        coreRow_.setBounds(x, y, w, rh);
        y += pitch;
        phaseRow_.setBounds(x, y, w, rh);
        y += pitch;
        depthRow_.setBounds(x, y, w, rh);
        y += pitch + px(4.0f);

        const float barsH = px(82.0f);
        harmBars_.setBounds(x, y, w, barsH);
        y += barsH + px(12.0f);

        tiltRow_.setBounds(x, y, w, rh);
        y += pitch;
        tensionRow_.setBounds(x, y, w, rh);
        y += pitch;
        cavHzRow_.setBounds(x, y, w, rh);
        y += pitch;
        cavQRow_.setBounds(x, y, w, rh);
    }

  private:
    Row coreRow_, phaseRow_, depthRow_;
    HarmonicBars harmBars_;
    Row tiltRow_, tensionRow_, cavHzRow_, cavQRow_;
};

// Column 2: 02 IMPACT (Beater + 2-op FM + Noise + Optional WAV)
class ImpactColumn final : public Panel, public Live {
  public:
    explicit ImpactColumn(Context& ctx)
        : Panel(ctx, "02 IMPACT", colour::impact),
          beaterRow_(ctx, ParamId::beaterType, colour::impact, "Beater", false),
          hardRow_(ctx, ParamId::beaterHard, colour::impact, "Hardness", true),
          toneRow_(ctx, ParamId::beaterTone, colour::impact, "Beater Hz", true),
          fmDepthRow_(ctx, ParamId::fmDepth, colour::impact, "FM Depth", true),
          fmRatioRow_(ctx, ParamId::fmRatio, colour::impact, "FM Ratio", true),
          fmDecayRow_(ctx, ParamId::fmDecayMs, colour::impact, "FM Decay", true),
          noiseModeRow_(ctx, ParamId::noiseMode, colour::impact, "Noise Mode", false),
          noiseLvlRow_(ctx, ParamId::noiseLevel, colour::impact, "Noise", true),
          noiseHzRow_(ctx, ParamId::noiseHz, colour::impact, "Noise Hz", true),
          noiseQRow_(ctx, ParamId::noiseQ, colour::impact, "Noise Q", true),
          noiseDecRow_(ctx, ParamId::noiseDecayMs, colour::impact, "Noise ms", true),
          wavMixRow_(ctx, ParamId::clickWavMix, colour::impact, "WAV Mix", true),
          loadBtn_(ctx, "Load WAV", [this] { context_.model.browseClickSample(); }),
          alignBtn_(ctx, "Align", [this] { context_.model.alignClickSample(); }),
          clearBtn_(ctx, "\xc3\x97", [this] { context_.model.clearClickSample(); }) {
        loadBtn_.setAccent(colour::impact);
        loadBtn_.setFontSize(9.4f);
        alignBtn_.setAccent(colour::sub);
        alignBtn_.setFontSize(9.4f);
        clearBtn_.setAccent(colour::textDim);
        clearBtn_.setFontSize(10.5f);

        for (visage::Frame* c : std::initializer_list<visage::Frame*>{
                 &beaterRow_, &hardRow_, &toneRow_, &fmDepthRow_, &fmRatioRow_, &fmDecayRow_, &noiseModeRow_,
                 &noiseLvlRow_, &noiseHzRow_, &noiseQRow_, &noiseDecRow_, &wavMixRow_, &loadBtn_, &alignBtn_,
                 &clearBtn_}) {
            addChild(c);
        }
        context_.live.push_back(this);
        tick();
    }

    ~ImpactColumn() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto name = context_.model.clickSampleName();
        if (name != wavName_) {
            wavName_ = name;
            loadBtn_.setLabel(wavName_.empty() ? "Load WAV" : wavName_.substr(0, 14));
            loadBtn_.setActive(!wavName_.empty());
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        Panel::draw(canvas);
        const float sc = context_.scale;
        drawPanel(canvas, context_, 10.0f * sc, height() - 66.0f * sc, width() - 20.0f * sc, 56.0f * sc, colour::window,
                  metric::radiusSmall);
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float x = px(12.0f);
        const float w = width() - px(24.0f);
        const float rh = px(21.0f);
        const float pitch = px(26.0f);
        float y = px(32.0f);

        for (Row* r : {&beaterRow_, &hardRow_, &toneRow_, &fmDepthRow_, &fmRatioRow_, &fmDecayRow_, &noiseModeRow_,
                       &noiseLvlRow_, &noiseHzRow_, &noiseQRow_, &noiseDecRow_}) {
            r->setBounds(x, y, w, rh);
            y += pitch;
        }

        // Click WAV dock at bottom
        const float dockY = height() - px(62.0f);
        const float dx = px(18.0f);
        const float dw = width() - px(36.0f);
        wavMixRow_.setBounds(dx, dockY, dw, px(21.0f));
        const float by = dockY + px(26.0f);
        const float bh = px(21.0f);
        const float clearW = px(24.0f);
        const float alignW = px(54.0f);
        const float loadW = dw - alignW - clearW - px(10.0f);
        loadBtn_.setBounds(dx, by, loadW, bh);
        alignBtn_.setBounds(dx + loadW + px(5.0f), by, alignW, bh);
        clearBtn_.setBounds(dx + loadW + alignW + px(10.0f), by, clearW, bh);
    }

  private:
    Row beaterRow_, hardRow_, toneRow_, fmDepthRow_, fmRatioRow_, fmDecayRow_;
    Row noiseModeRow_, noiseLvlRow_, noiseHzRow_, noiseQRow_, noiseDecRow_;
    Row wavMixRow_;
    PillButton loadBtn_, alignBtn_, clearBtn_;
    std::string wavName_;
};

// Column 3: 03 SHAPE (Phase-Native 3-Band + Transient + 8x ADAA2 + Box-Cut)
class ShapeColumn final : public Panel {
  public:
    explicit ShapeColumn(Context& ctx)
        : Panel(ctx, "03 SHAPE", colour::body), subKnob_(ctx, ParamId::subLevel, colour::sub),
          bodyKnob_(ctx, ParamId::bodyLevel, colour::body), clickKnob_(ctx, ParamId::clickLevel, colour::impact),
          atkRow_(ctx, ParamId::envAttack, colour::impact, "Attack", true),
          punchRow_(ctx, ParamId::envPunch, colour::body, "Body Env", true),
          tailRow_(ctx, ParamId::envTail, colour::sub, "Tail", true),
          shaperRow_(ctx, ParamId::shaperType, colour::body, "Shaper", false),
          glueRow_(ctx, ParamId::shaperGlue, colour::body, "Glue", true),
          boxHzRow_(ctx, ParamId::boxCutHz, colour::body, "Box Hz", true),
          boxDbRow_(ctx, ParamId::boxCutDb, colour::body, "Box Cut", true),
          boxZpRow_(ctx, ParamId::boxZeroPhase, colour::sub, "Zero-Ph Dip", false),
          airRow_(ctx, ParamId::airWidth, colour::impact, "Air Width", true) {
        for (visage::Frame* c : std::initializer_list<visage::Frame*>{&subKnob_, &bodyKnob_, &clickKnob_, &atkRow_,
                                                                      &punchRow_, &tailRow_, &shaperRow_, &glueRow_,
                                                                      &boxHzRow_, &boxDbRow_, &boxZpRow_, &airRow_}) {
            addChild(c);
        }
    }

    void draw(visage::Canvas& canvas) override {
        Panel::draw(canvas);
        const float sc = context_.scale;
        drawPanel(canvas, context_, 10.0f * sc, 32.0f * sc, width() - 20.0f * sc, 78.0f * sc, colour::window,
                  metric::radiusSmall);
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float bx = px(16.0f);
        const float bw = width() - px(32.0f);
        const float kColW = bw / 3.0f;
        const float kh = px(68.0f);
        const float ky = px(37.0f);
        subKnob_.setBounds(bx, ky, kColW, kh);
        bodyKnob_.setBounds(bx + kColW, ky, kColW, kh);
        clickKnob_.setBounds(bx + 2.0f * kColW, ky, kColW, kh);

        const float x = px(12.0f);
        const float w = width() - px(24.0f);
        const float rh = px(22.0f);
        const float pitch = px(29.5f);
        float y = px(120.0f);

        for (Row* r :
             {&atkRow_, &punchRow_, &tailRow_, &shaperRow_, &glueRow_, &boxHzRow_, &boxDbRow_, &boxZpRow_, &airRow_}) {
            r->setBounds(x, y, w, rh);
            y += pitch;
        }
    }

  private:
    Knob subKnob_, bodyKnob_, clickKnob_;
    Row atkRow_, punchRow_, tailRow_, shaperRow_, glueRow_, boxHzRow_, boxDbRow_, boxZpRow_, airRow_;
};

// Column 4: 04 MASTER · SUB-LINK
class MasterColumn final : public Panel, public Live {
  public:
    explicit MasterColumn(Context& ctx)
        : Panel(ctx, "04 MASTER", colour::sidechain),
          phaseRotRow_(ctx, ParamId::phaseRotate, colour::sidechain, "Phase Rot", true),
          dispAmtRow_(ctx, ParamId::disperseAmt, colour::sidechain, "Disperser", true),
          dispHzRow_(ctx, ParamId::disperseHz, colour::sidechain, "Disp Hz", true),
          outRow_(ctx, ParamId::outputDb, colour::text, "Output", true),
          ceilRow_(ctx, ParamId::ceilingDb, colour::alert, "Ceiling", true),
          limitRow_(ctx, ParamId::limitOn, colour::alert, "Limit", false),
          humanRow_(ctx, ParamId::humanize, colour::body, "Humanize", true),
          velRow_(ctx, ParamId::velToPunch, colour::body, "Vel Punch", true),
          matchBtn_(ctx, "Match Phase", [this] { context_.model.matchSubLinkPhase(); }),
          tuneBtn_(ctx, "Tune Root", [this] { context_.model.tuneToSubLinkRoot(); }) {
        matchBtn_.setAccent(colour::sidechain);
        matchBtn_.setFontSize(9.8f);
        tuneBtn_.setAccent(colour::sub);
        tuneBtn_.setFontSize(9.8f);

        for (visage::Frame* c :
             std::initializer_list<visage::Frame*>{&phaseRotRow_, &dispAmtRow_, &dispHzRow_, &outRow_, &ceilRow_,
                                                   &limitRow_, &humanRow_, &velRow_, &matchBtn_, &tuneBtn_}) {
            addChild(c);
        }
        context_.live.push_back(this);
        tick();
    }

    ~MasterColumn() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const auto& v = context_.model.visuals();
        if (v.scActive != scActive_ || std::abs(v.scCorrelation - scCorr_) > 0.01f ||
            std::abs(v.scRootHz - scRoot_) > 0.2f) {
            scActive_ = v.scActive;
            scCorr_ = v.scCorrelation;
            scRoot_ = v.scRootHz;
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        Panel::draw(canvas);
        const float sc = context_.scale;
        const float cardX = 10.0f * sc;
        const float cardW = width() - 20.0f * sc;
        const float cardH = 112.0f * sc;
        const float cardY = height() - cardH - 10.0f * sc;
        drawPanel(canvas, context_, cardX, cardY, cardW, cardH, colour::window, metric::radiusSmall);

        // Header + live correlation & root readout
        canvas.setColor(colour::withAlpha(colour::sidechain, 0.85f));
        canvas.roundedRectangle(cardX + 8.0f * sc, cardY + 9.0f * sc, 3.0f * sc, 11.0f * sc, 1.5f * sc);
        drawLabel(canvas, context_, "SUB-LINK", cardX + 16.0f * sc, cardY + 6.0f * sc, 80.0f * sc, 16.0f * sc,
                  colour::sidechain, visage::Font::kLeft, 9.4f, Weight::semibold);

        char rootBuf[32];
        if (scActive_ && scRoot_ >= 24.0f) {
            std::snprintf(rootBuf, sizeof rootBuf, "%.1f Hz \xc2\xb7 %s", static_cast<double>(scRoot_),
                          noteNameForHz(scRoot_).c_str());
        } else {
            std::snprintf(rootBuf, sizeof rootBuf, "SC OFF");
        }
        drawLabel(canvas, context_, rootBuf, cardX + 94.0f * sc, cardY + 6.0f * sc, cardW - 104.0f * sc, 16.0f * sc,
                  scActive_ ? colour::sub : colour::textFaint, visage::Font::kRight, 9.4f, Weight::semibold);

        // Bipolar correlation meter (-1 .. +1)
        const float mx = cardX + 10.0f * sc;
        const float mw = cardW - 20.0f * sc;
        const float my = cardY + 34.0f * sc;
        const float mh = 8.0f * sc;
        canvas.setColor(colour::track);
        canvas.roundedRectangle(mx, my, mw, mh, 4.0f * sc);
        const float center = mx + mw * 0.5f;
        canvas.setColor(colour::edge);
        canvas.rectangle(center, my - 2.0f * sc, 1.0f * sc, mh + 4.0f * sc);

        if (scActive_) {
            const float clamped = std::clamp(scCorr_, -1.0f, 1.0f);
            const float span = std::abs(clamped) * mw * 0.5f;
            const unsigned barCol = clamped >= 0.45f ? colour::sub : (clamped >= 0.0f ? colour::body : colour::alert);
            canvas.setColor(barCol);
            canvas.roundedRectangle(clamped >= 0.0f ? center : center - span, my, span, mh, 4.0f * sc);
        }

        char corrBuf[24];
        std::snprintf(corrBuf, sizeof corrBuf, "CORR %+.2f", static_cast<double>(scActive_ ? scCorr_ : 0.0f));
        drawLabel(canvas, context_, corrBuf, mx, my + 12.0f * sc, mw, 15.0f * sc,
                  scActive_ ? colour::text : colour::textFaint, visage::Font::kCenter, 9.2f, Weight::medium);
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float x = px(12.0f);
        const float w = width() - px(24.0f);
        const float rh = px(22.0f);
        const float pitch = px(29.0f);
        float y = px(34.0f);

        for (Row* r :
             {&phaseRotRow_, &dispAmtRow_, &dispHzRow_, &outRow_, &ceilRow_, &limitRow_, &humanRow_, &velRow_}) {
            r->setBounds(x, y, w, rh);
            y += pitch;
        }

        const float cardX = px(20.0f);
        const float cardW = width() - px(40.0f);
        const float btnH = px(26.0f);
        const float btnY = height() - px(46.0f);
        const float gap = px(8.0f);
        const float bw = (cardW - gap) * 0.5f;
        matchBtn_.setBounds(cardX, btnY, bw, btnH);
        tuneBtn_.setBounds(cardX + bw + gap, btnY, bw, btnH);
    }

  private:
    Row phaseRotRow_, dispAmtRow_, dispHzRow_, outRow_, ceilRow_, limitRow_, humanRow_, velRow_;
    PillButton matchBtn_, tuneBtn_;
    bool scActive_ = false;
    float scCorr_ = 0.0f;
    float scRoot_ = 0.0f;
};

class AnatomyPanel final : public visage::Frame {
  public:
    explicit AnatomyPanel(Context& ctx) : context_(ctx), core_(ctx), impact_(ctx), shape_(ctx), master_(ctx) {
        addChild(&core_);
        addChild(&impact_);
        addChild(&shape_);
        addChild(&master_);
    }

    void resized() override {
        const float gap = context_.px(metric::gap);
        const float colW = (width() - 3.0f * gap) * 0.25f;
        core_.setBounds(0.0f, 0.0f, colW, height());
        impact_.setBounds(colW + gap, 0.0f, colW, height());
        shape_.setBounds(2.0f * (colW + gap), 0.0f, colW, height());
        master_.setBounds(3.0f * (colW + gap), 0.0f, colW, height());
    }

  private:
    Context& context_;
    CoreColumn core_;
    ImpactColumn impact_;
    ShapeColumn shape_;
    MasterColumn master_;
};

} // namespace

std::unique_ptr<visage::Frame> makeAnatomyPanel(Context& context) { return std::make_unique<AnatomyPanel>(context); }

} // namespace dt::ui
