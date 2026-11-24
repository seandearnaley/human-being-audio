// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controls.h"
#include "theme.h"

#include <visage/ui.h>

#include <memory>
#include <vector>

namespace lc::ui {

class ClassicPanel final : public visage::Frame, public Live {
  public:
    explicit ClassicPanel(Context& context);
    ~ClassicPanel() override;

    void selectTab(int tabIndex);
    int selectedTab() const { return tab_; }

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

  private:
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    Rect columnRect(float top, float height, int col) const;
    void updateVisibility();

    Context& context_;
    int tab_ = 0;
    int lastProc_ = -1;
    int lastFamily_ = -1;
    bool lastCrusher_ = false;
    bool lastComb_ = false;
    bool lastCanApply_ = false;

    // Navigation & global mode strip
    SegmentedBar tabs_;
    ParamSegmented procedure_;
    ParamSegmented mode_;
    ActionButton applyMode_;

    // Tab 0 — Cut & Recut
    std::unique_ptr<RangeSlider> bars_, amp_, pan_, pitch_;
    std::unique_ptr<ParamSlider> duty_, fillDuty_;

    // Procedure-specific controls
    std::unique_ptr<RangeSlider> repeats_;
    std::unique_ptr<ParamSlider> stutter_, stutterArea_;
    std::unique_ptr<ParamSlider> straight_, regular_, ritard_, accel_;
    std::unique_ptr<ParamSlider> sqActivity_;
    std::unique_ptr<ParamSlider> bounceChance_, ratchetChance_, climb_, halftime_;

    // Recut
    std::unique_ptr<TogglePill> attackAlign_;
    std::unique_ptr<ParamSlider> attackThreshold_, reverseChance_;

    // Classic FX
    std::unique_ptr<TogglePill> crusherOn_, combOn_;
    std::unique_ptr<RangeSlider> bits_, rate_, combDelay_;
    std::unique_ptr<ChoiceRow> combType_;
    std::unique_ptr<ParamSlider> combFeedback_;

    // Tab 1 — Impact
    std::unique_ptr<ParamSlider> impactAttack_, impactDecay_;
    std::unique_ptr<TogglePill> filterOn_, driveOn_, transientOn_;
    std::unique_ptr<ChoiceRow> filterType_, driveMode_;
    std::unique_ptr<ParamSlider> filterChance_, filterCutoff_, filterQ_, filterEnv_, filterRepeat_, filterMix_;
    std::unique_ptr<ParamSlider> driveChance_, driveDb_, driveEnv_, driveRepeat_, driveMix_;
    std::unique_ptr<ParamSlider> transChance_, transAttack_, transSustain_, transTime_, transMix_;

    // Tab 2 — Fracture
    std::unique_ptr<ParamSegmented> fractureFamily_;
    std::unique_ptr<ParamSlider> fracActivity_, grainSize_, grainDensity_, grainPos_, grainPitch_, grainPan_;
    std::unique_ptr<ParamSlider> tapeStart_, tapeEnd_;
    std::unique_ptr<ChoiceRow> tapeCurve_, tapeCycle_;
    std::unique_ptr<TogglePill> tapeAlternate_;
    std::unique_ptr<TogglePill> freqOn_;
    std::unique_ptr<ChoiceRow> freqMode_;
    std::unique_ptr<ParamSlider> freqChance_, freqRate_, freqEnv_, freqRepeat_, freqMix_;

    // Tab 3 — Resonance & Spectral
    std::unique_ptr<TogglePill> resOn_;
    std::unique_ptr<ParamSlider> resRoot_, resDecay_, resExcite_, resMix_;
    std::unique_ptr<ChoiceRow> resOrder_;
    std::unique_ptr<ParamSlider> resInt_[4];
    std::unique_ptr<ParamSlider> resWeight_[4];
    std::unique_ptr<TogglePill> specOn_, specFreeze_;
    std::unique_ptr<ParamSlider> specSmear_, specMix_;
    ActionButton applySpectral_;

    // Shared Classic bottom strip
    std::unique_ptr<ChoiceRow> subdivision_;
    std::unique_ptr<ParamSlider> seed_, fade_, swing_, accent_, trim_, wet_;
    std::unique_ptr<TogglePill> audioHold_;
    ActionButton rollSeed_, captureBtn_, dragWavBtn_, saveWavBtn_;
};

} // namespace lc::ui
