// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace dt::ui {

namespace {

// Interactive Kit Slot Card (A · Main, B · Ghost, C · Double, D · Sub)
class SlotCard final : public visage::Frame, public Live {
  public:
    SlotCard(Context& ctx, int slotIndex)
        : context_(ctx), slot_(slotIndex), accent_(colour::slotAccent(slotIndex)),
          cloneBtn_(ctx, "Clone", [this] { cloneFromActive(); }) {
        cloneBtn_.setAccent(colour::textDim);
        cloneBtn_.setFontSize(8.8f);
        addChild(&cloneBtn_);
        context_.live.push_back(this);
        syncCache();
    }

    ~SlotCard() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const int active =
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::activeSlot))), 0, kKitSlots - 1);
        const auto pat = context_.model.pattern();
        const auto& s = pat.slots[static_cast<std::size_t>(slot_)];
        if (active != activeSlot_ || !(s == cached_)) {
            activeSlot_ = active;
            cached_ = s;
            redraw();
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        cloneBtn_.setBounds(width() - px(52.0f), px(6.0f), px(42.0f), px(19.0f));
    }

    void draw(visage::Canvas& canvas) override {
        syncCache();
        const float sc = context_.scale;
        const bool isActive = (activeSlot_ == slot_);
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);
        if (isActive) {
            canvas.setColor(colour::withAlpha(accent_, 0.85f));
            canvas.roundedRectangleBorder(0.0f, 0.0f, width(), height(), metric::radius * sc, 1.4f * sc);
        }

        // Header bar
        canvas.setColor(colour::withAlpha(accent_, isActive ? 0.95f : 0.45f));
        canvas.roundedRectangle(10.0f * sc, 10.0f * sc, 3.0f * sc, 12.0f * sc, 1.5f * sc);
        const std::string title(choices::activeSlot[static_cast<std::size_t>(slot_)]);
        drawLabel(canvas, context_, title, 19.0f * sc, 6.0f * sc, width() - 80.0f * sc, 18.0f * sc,
                  isActive ? colour::text : accent_, visage::Font::kLeft, 10.0f, Weight::semibold);

        // 5 horizontal offset rows
        for (int r = 0; r < 5; ++r)
            drawOffsetRow(canvas, r);
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const float sc = context_.scale;
        if (e.position.y < 30.0f * sc) {
            context_.model.begin(ParamId::activeSlot);
            context_.model.set(ParamId::activeSlot, static_cast<float>(slot_));
            context_.model.end(ParamId::activeSlot);
            context_.model.triggerAudition(slot_, 0.96f);
            redraw();
            return;
        }
        const int row = hitRow(e.position.y);
        if (row < 0)
            return;
        if (e.repeatClickCount() >= 2) {
            auto pat = context_.model.pattern();
            const auto def = defaultKitSlots()[static_cast<std::size_t>(slot_)];
            setField(pat.slots[static_cast<std::size_t>(slot_)], row, getField(def, row));
            context_.model.setPattern(pat);
            redraw();
            return;
        }
        dragRow_ = row;
        dragStartX_ = e.position.x;
        dragStartVal_ = getField(cached_, row);
        applyDrag(row, e.position.x);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (dragRow_ < 0)
            return;
        applyDrag(dragRow_, e.position.x);
    }

    void mouseUp(const visage::MouseEvent&) override {
        if (dragRow_ >= 0) {
            dragRow_ = -1;
            context_.model.triggerAudition(slot_, 0.94f);
            redraw();
        }
    }

  private:
    struct FieldMeta {
        const char* name;
        float lo;
        float hi;
        bool bipolar;
    };

    static constexpr std::array<FieldMeta, 5> kFields{{
        {"Tune", -12.0f, 12.0f, true},
        {"Length", 0.35f, 2.0f, false},
        {"Click", 0.0f, 1.6f, false},
        {"Drive", 0.0f, 1.6f, false},
        {"Level", 0.2f, 1.25f, false},
    }};

    static float getField(const KitSlot& s, int row) {
        switch (row) {
        case 0:
            return s.tuneOffsetSt;
        case 1:
            return s.lengthScale;
        case 2:
            return s.clickScale;
        case 3:
            return s.driveScale;
        default:
            return s.levelScale;
        }
    }

    static void setField(KitSlot& s, int row, float v) {
        switch (row) {
        case 0:
            s.tuneOffsetSt = std::clamp(v, -12.0f, 12.0f);
            break;
        case 1:
            s.lengthScale = std::clamp(v, 0.35f, 2.0f);
            break;
        case 2:
            s.clickScale = std::clamp(v, 0.0f, 1.6f);
            break;
        case 3:
            s.driveScale = std::clamp(v, 0.0f, 1.6f);
            break;
        default:
            s.levelScale = std::clamp(v, 0.2f, 1.25f);
            break;
        }
    }

    float rowTop(int row) const { return context_.px(33.0f + static_cast<float>(row) * 23.0f); }

    int hitRow(float y) const {
        const float sc = context_.scale;
        for (int r = 0; r < 5; ++r) {
            const float top = rowTop(r);
            if (y >= top && y <= top + 21.0f * sc)
                return r;
        }
        return -1;
    }

    void drawOffsetRow(visage::Canvas& canvas, int row) const {
        const float sc = context_.scale;
        const float x = 12.0f * sc;
        const float w = width() - 24.0f * sc;
        const float y = rowTop(row);
        const float h = 20.0f * sc;
        const auto& m = kFields[static_cast<std::size_t>(row)];
        const float val = getField(cached_, row);
        const float norm = std::clamp((val - m.lo) / (m.hi - m.lo), 0.0f, 1.0f);

        drawLabel(canvas, context_, m.name, x, y, w * 0.5f, h - 3.0f * sc,
                  dragRow_ == row ? colour::text : colour::textDim, visage::Font::kLeft, 9.6f, Weight::medium);

        char buf[24];
        if (row == 0)
            std::snprintf(buf, sizeof buf, "%+.1f st", static_cast<double>(val));
        else
            std::snprintf(buf, sizeof buf, "%.0f%%", static_cast<double>(val * 100.0f));
        drawLabel(canvas, context_, buf, x, y, w, h - 3.0f * sc, colour::text, visage::Font::kRight, 9.6f,
                  Weight::regular);

        const float bh = 2.4f * sc;
        const float by = y + h - bh;
        canvas.setColor(colour::track);
        canvas.roundedRectangle(x, by, w, bh, bh * 0.5f);
        const float origin = m.bipolar ? 0.5f : 0.0f;
        const float from = std::min(origin, norm);
        const float to = std::max(origin, norm);
        canvas.setColor(accent_);
        canvas.roundedRectangle(x + w * from, by, w * (to - from), bh, bh * 0.5f);
    }

    void applyDrag(int row, float mx) {
        const float sc = context_.scale;
        const float x = 12.0f * sc;
        const float w = std::max(20.0f * sc, width() - 24.0f * sc);
        const float norm = std::clamp((mx - x) / w, 0.0f, 1.0f);
        const auto& m = kFields[static_cast<std::size_t>(row)];
        auto pat = context_.model.pattern();
        setField(pat.slots[static_cast<std::size_t>(slot_)], row, m.lo + norm * (m.hi - m.lo));
        context_.model.setPattern(pat);
        cached_ = pat.slots[static_cast<std::size_t>(slot_)];
        redraw();
    }

    void cloneFromActive() {
        const int src =
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::activeSlot))), 0, kKitSlots - 1);
        auto pat = context_.model.pattern();
        pat.slots[static_cast<std::size_t>(slot_)] = pat.slots[static_cast<std::size_t>(src)];
        context_.model.setPattern(pat);
        syncCache();
        redraw();
    }

    void syncCache() {
        activeSlot_ =
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::activeSlot))), 0, kKitSlots - 1);
        cached_ = context_.model.pattern().slots[static_cast<std::size_t>(slot_)];
    }

    Context& context_;
    int slot_ = 0;
    unsigned accent_ = colour::sub;
    PillButton cloneBtn_;
    int activeSlot_ = 0;
    KitSlot cached_{};
    int dragRow_ = -1;
    float dragStartX_ = 0.0f;
    float dragStartVal_ = 0.0f;
};

// Interactive 16/32-step Velocity, Slot & Ratchet Grid
class StepGrid final : public visage::Frame, public Live {
  public:
    explicit StepGrid(Context& ctx) : context_(ctx) {
        context_.live.push_back(this);
        syncCache();
    }

    ~StepGrid() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override {
        const int steps = context_.model.get(ParamId::seqSteps) >= 0.5f ? 32 : 16;
        const int cur = context_.model.visuals().currentSeqStep;
        const auto pat = context_.model.pattern();
        if (steps != stepCount_ || cur != playhead_ || !(pat == pattern_)) {
            stepCount_ = steps;
            playhead_ = cur;
            pattern_ = pat;
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        syncCache();
        const float sc = context_.scale;
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::window, metric::radiusSmall);

        const float padX = 8.0f * sc;
        const float padTop = 8.0f * sc;
        const float footerH = 26.0f * sc;
        const float barAreaH = std::max(24.0f * sc, height() - padTop - footerH - 6.0f * sc);
        const float cellW = (width() - 2.0f * padX) / static_cast<float>(stepCount_);
        const float gap = (stepCount_ > 16 ? 2.5f : 5.0f) * sc;
        const float colW = std::max(6.0f * sc, cellW - gap);

        const char slotChar[4] = {'A', 'B', 'C', 'D'};

        for (int i = 0; i < stepCount_; ++i) {
            const float cx = padX + static_cast<float>(i) * cellW + gap * 0.5f;
            const auto& st = pattern_.steps[static_cast<std::size_t>(i)];
            const bool isBeat = (i % 4 == 0);
            const bool isNow = (i == playhead_);
            const unsigned sCol = colour::slotAccent(st.slot);

            // Track background
            const unsigned bg = isNow ? colour::withAlpha(colour::text, 0.16f)
                                      : (isBeat ? colour::raised : colour::withAlpha(colour::panel, 0.75f));
            canvas.setColor(bg);
            canvas.roundedRectangle(cx, padTop, colW, barAreaH, 4.0f * sc);

            // Velocity bar
            if (st.velocity > 0) {
                const float vn = static_cast<float>(st.velocity) / 127.0f;
                const float vh = std::max(5.0f * sc, barAreaH * vn);
                canvas.setColor(colour::withAlpha(sCol, isNow ? 0.98f : 0.78f));
                canvas.roundedRectangle(cx, padTop + barAreaH - vh, colW, vh, 4.0f * sc);

                // Ratchet pips near top of active bar
                if (st.ratchet > 1) {
                    const float pip = 3.2f * sc;
                    const float totalW =
                        static_cast<float>(st.ratchet) * pip + static_cast<float>(st.ratchet - 1) * 2.0f * sc;
                    float px = cx + (colW - totalW) * 0.5f;
                    const float py = padTop + 4.0f * sc;
                    canvas.setColor(colour::text);
                    for (int r = 0; r < st.ratchet; ++r) {
                        canvas.circle(px, py, pip);
                        px += pip + 2.0f * sc;
                    }
                }
            }

            // Bottom slot badge (click to cycle A/B/C/D)
            const float fy = padTop + barAreaH + 4.0f * sc;
            const float fh = footerH - 4.0f * sc;
            canvas.setColor(st.velocity > 0 ? colour::withAlpha(sCol, 0.22f) : colour::panel);
            canvas.roundedRectangle(cx, fy, colW, fh, 3.0f * sc);
            if (st.velocity > 0) {
                canvas.setColor(colour::withAlpha(sCol, 0.55f));
                canvas.roundedRectangleBorder(cx, fy, colW, fh, 3.0f * sc, 1.0f * sc);
            }
            const std::string tag = st.velocity > 0 ? std::string(1, slotChar[st.slot & 3]) : std::to_string(i + 1);
            drawLabel(canvas, context_, tag, cx, fy, colW, fh,
                      st.velocity > 0 ? sCol : (isBeat ? colour::textDim : colour::textFaint), visage::Font::kCenter,
                      stepCount_ > 16 ? 8.4f : 9.4f, Weight::semibold);
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const int idx = hitStep(e.position.x);
        if (idx < 0)
            return;

        const float sc = context_.scale;
        const float footerTop = height() - 30.0f * sc;
        auto pat = context_.model.pattern();
        auto& st = pat.steps[static_cast<std::size_t>(idx)];

        if (e.shouldTriggerPopup()) {
            if (st.velocity == 0) {
                st.velocity = 110;
                st.slot = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::activeSlot))), 0, 3));
                st.ratchet = 2;
            } else {
                st.ratchet = static_cast<std::uint8_t>((st.ratchet % 3) + 1);
            }
            context_.model.setPattern(pat);
            pattern_ = pat;
            redraw();
            return;
        }

        if (e.position.y >= footerTop) {
            // Cycle slot A->B->C->D on badge click
            st.slot = static_cast<std::uint8_t>((st.slot + 1) % kKitSlots);
            if (st.velocity == 0)
                st.velocity = 112;
            context_.model.setPattern(pat);
            pattern_ = pat;
            redraw();
            return;
        }

        painting_ = true;
        // Toggle off if clicking a step already near the bottom or double-clicking
        if (e.repeatClickCount() >= 2) {
            st.velocity = 0;
            context_.model.setPattern(pat);
            pattern_ = pat;
            painting_ = false;
            redraw();
            return;
        }
        paintStep(idx, e.position.y, true);
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!painting_)
            return;
        const int idx = hitStep(e.position.x);
        if (idx >= 0)
            paintStep(idx, e.position.y, false);
    }

    void mouseUp(const visage::MouseEvent&) override { painting_ = false; }

  private:
    int hitStep(float mx) const {
        const float padX = context_.px(8.0f);
        const float inner = std::max(1.0f, width() - 2.0f * padX);
        const float norm = (mx - padX) / inner;
        if (norm < 0.0f || norm > 1.0f)
            return -1;
        return std::clamp(static_cast<int>(norm * static_cast<float>(stepCount_)), 0, stepCount_ - 1);
    }

    void paintStep(int idx, float my, bool allowToggleOff) {
        const float sc = context_.scale;
        const float padTop = 8.0f * sc;
        const float barAreaH = std::max(20.0f * sc, height() - padTop - 32.0f * sc);
        const float vn = std::clamp(1.0f - (my - padTop) / barAreaH, 0.0f, 1.0f);
        auto pat = context_.model.pattern();
        auto& st = pat.steps[static_cast<std::size_t>(idx)];
        if (vn < 0.08f) {
            st.velocity = 0;
        } else if (allowToggleOff && st.velocity > 0 &&
                   std::abs(vn - static_cast<float>(st.velocity) / 127.0f) < 0.06f) {
            st.velocity = 0;
        } else {
            if (st.velocity == 0) {
                st.slot = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::activeSlot))), 0, 3));
            }
            st.velocity = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(vn * 127.0f)), 16, 127));
        }
        context_.model.setPattern(pat);
        pattern_ = pat;
        redraw();
    }

    void syncCache() {
        stepCount_ = context_.model.get(ParamId::seqSteps) >= 0.5f ? 32 : 16;
        playhead_ = context_.model.visuals().currentSeqStep;
        pattern_ = context_.model.pattern();
    }

    Context& context_;
    int stepCount_ = 16;
    int playhead_ = -1;
    ComposePattern pattern_{};
    bool painting_ = false;
};

class ComposePanel final : public visage::Frame {
  public:
    explicit ComposePanel(Context& ctx)
        : context_(ctx), seqPanel_(ctx, "CONTEXT SEQUENCER", colour::impact),
          playRow_(ctx, ParamId::seqRun, colour::sub, "Play", false),
          stepsRow_(ctx, ParamId::seqSteps, colour::impact, "Steps", false),
          rateRow_(ctx, ParamId::seqRate, colour::impact, "Rate", false),
          swingRow_(ctx, ParamId::seqSwing, colour::body, "Swing", true),
          morphRow_(ctx, ParamId::slotMorph, colour::sidechain, "Morph", true),
          refSubRow_(ctx, ParamId::refBassMode, colour::sub, "Ref Sub", false),
          pat2Step_(ctx, "2-Step", [this] { loadGroove(0); }), patHalf_(ctx, "Half", [this] { loadGroove(1); }),
          patFour_(ctx, "4/4", [this] { loadGroove(2); }), patClear_(ctx, "Clear", [this] { loadGroove(3); }),
          grid_(ctx) {
        for (int i = 0; i < kKitSlots; ++i) {
            slots_[static_cast<std::size_t>(i)] = std::make_unique<SlotCard>(ctx, i);
            addChild(slots_[static_cast<std::size_t>(i)].get());
        }

        addChild(&seqPanel_);
        for (auto* b : {&pat2Step_, &patHalf_, &patFour_, &patClear_}) {
            b->setFontSize(9.4f);
            b->setAccent(colour::textDim);
            seqPanel_.addChild(b);
        }
        pat2Step_.setAccent(colour::sub);

        for (visage::Frame* c : std::initializer_list<visage::Frame*>{&playRow_, &stepsRow_, &rateRow_, &swingRow_,
                                                                      &morphRow_, &refSubRow_, &grid_}) {
            seqPanel_.addChild(c);
        }
    }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float cardsH = px(152.0f);
        const float cardW = (width() - 3.0f * gap) * 0.25f;
        for (int i = 0; i < kKitSlots; ++i) {
            slots_[static_cast<std::size_t>(i)]->setBounds(static_cast<float>(i) * (cardW + gap), 0.0f, cardW, cardsH);
        }

        const float seqY = cardsH + gap;
        const float seqH = std::max(px(120.0f), height() - seqY);
        seqPanel_.setBounds(0.0f, seqY, width(), seqH);

        // Sequencer header controls row
        const float cy = px(32.0f);
        const float ch = px(22.0f);
        float x = px(12.0f);
        playRow_.setBounds(x, cy, px(78.0f), ch);
        x += px(90.0f);
        stepsRow_.setBounds(x, cy, px(86.0f), ch);
        x += px(98.0f);
        rateRow_.setBounds(x, cy, px(96.0f), ch);
        x += px(108.0f);
        swingRow_.setBounds(x, cy, px(126.0f), ch);
        x += px(138.0f);
        morphRow_.setBounds(x, cy, px(126.0f), ch);
        x += px(138.0f);
        refSubRow_.setBounds(x, cy, px(152.0f), ch);

        // Groove quick-fill pills right-aligned in header
        float rx = width() - px(12.0f);
        const float bw = px(52.0f);
        const float bgap = px(5.0f);
        rx -= bw;
        patClear_.setBounds(rx, cy, bw, ch);
        rx -= bw + bgap;
        patFour_.setBounds(rx, cy, bw, ch);
        rx -= bw + bgap;
        patHalf_.setBounds(rx, cy, bw, ch);
        rx -= bw + bgap;
        pat2Step_.setBounds(rx, cy, bw, ch);

        const float gy = px(62.0f);
        grid_.setBounds(px(10.0f), gy, width() - px(20.0f), std::max(px(60.0f), seqH - gy - px(10.0f)));
    }

  private:
    void loadGroove(int preset) {
        auto pat = context_.model.pattern();
        for (auto& st : pat.steps)
            st = {0, 0, 1};
        if (preset == 0) {
            pat = defaultComposePattern();
            pat.slots = context_.model.pattern().slots;
        } else if (preset == 1) { // Halftime 140
            pat.steps[0] = {124, 0, 1};
            pat.steps[7] = {74, 1, 1};
            pat.steps[10] = {108, 3, 1};
            pat.steps[16] = {124, 0, 1};
            pat.steps[23] = {70, 1, 1};
            pat.steps[28] = {104, 2, 2};
        } else if (preset == 2) { // 4/4 Techno
            for (int i = 0; i < 32; i += 4)
                pat.steps[static_cast<std::size_t>(i)] = {120, 0, 1};
            pat.steps[15] = {86, 1, 1};
            pat.steps[30] = {98, 2, 2};
        }
        context_.model.setPattern(pat);
    }

    Context& context_;
    std::array<std::unique_ptr<SlotCard>, kKitSlots> slots_;
    Panel seqPanel_;
    Row playRow_, stepsRow_, rateRow_, swingRow_, morphRow_, refSubRow_;
    PillButton pat2Step_, patHalf_, patFour_, patClear_;
    StepGrid grid_;
};

} // namespace

std::unique_ptr<visage::Frame> makeComposePanel(Context& context) { return std::make_unique<ComposePanel>(context); }

} // namespace dt::ui
