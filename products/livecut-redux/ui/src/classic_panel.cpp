// SPDX-License-Identifier: GPL-3.0-or-later
#include "classic_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>

namespace lc::ui {

namespace {
constexpr float kNavHeight = 32.0f;
constexpr float kGap = 10.0f;
constexpr float kBottomHeight = 84.0f;
constexpr float kHeaderStripH = 44.0f;
constexpr float kCardPadX = 14.0f;
} // namespace

ClassicPanel::ClassicPanel(Context& context)
    : context_(context), tabs_(
                             context, {"Cut & Recut", "Impact", "Fracture", "Resonance & Spectral"},
                             [this](int idx) { selectTab(idx); }, colour::accent),
      procedure_(context, "procedure", "Procedure", {"CutProc11", "WarpCut", "SQPusher", "Bounce"}, colour::accent),
      mode_(context, "mode", "Processing mode", {"Live", "Studio"}, colour::blue),
      applyMode_(context, "Apply Mode", [this] { context_.model.applyRequestedMode(); }),
      applySpectral_(context, "Apply while stopped", [this] { context_.model.applyRequestedMode(); }),
      rollSeed_(context, "Roll", [this] { context_.model.rollSeed(); }),
      captureBtn_(context, "Capture", [this] { context_.model.requestCapture(); }),
      dragWavBtn_(context, "Drag .wav", [this] { context_.model.requestCapture(); }),
      saveWavBtn_(context, "Save\u2026", [this] { context_.model.saveCapture(); }) {
    auto add = [this](visage::Frame* f) { addChild(f); };

    add(&tabs_);
    add(&procedure_);
    add(&mode_);
    add(&applyMode_);

    // Tab 0 — Phrase & Cut
    bars_ = std::make_unique<RangeSlider>(context, "minPhraseBars", "maxPhraseBars", "Phrase Bars", colour::accent);
    duty_ = std::make_unique<ParamSlider>(context, "duty", "Duty Cycle", colour::accent);
    fillDuty_ = std::make_unique<ParamSlider>(context, "fillDuty", "Fill Duty", colour::accent);
    amp_ = std::make_unique<RangeSlider>(context, "minAmp", "maxAmp", "Amplitude", colour::accent);
    pan_ = std::make_unique<RangeSlider>(context, "minPan", "maxPan", "Pan Spread", colour::accent);
    pitch_ = std::make_unique<RangeSlider>(context, "minPitch", "maxPitch", "Pitch Range", colour::accent);

    // Procedures
    repeats_ = std::make_unique<RangeSlider>(context, "minRepeats", "maxRepeats", "Repeats", colour::accent);
    stutter_ = std::make_unique<ParamSlider>(context, "stutterChance", "Stutter Chance", colour::accent);
    stutterArea_ = std::make_unique<ParamSlider>(context, "stutterArea", "Stutter Area", colour::accent);

    straight_ = std::make_unique<ParamSlider>(context, "straightChance", "Straight", colour::rose);
    regular_ = std::make_unique<ParamSlider>(context, "regularChance", "Regular", colour::rose);
    ritard_ = std::make_unique<ParamSlider>(context, "ritardChance", "Ritard", colour::rose);
    accel_ = std::make_unique<ParamSlider>(context, "accel", "Accel Ratio", colour::rose);

    sqActivity_ = std::make_unique<ParamSlider>(context, "activity", "SQ Activity", colour::amber);

    bounceChance_ = std::make_unique<ParamSlider>(context, "bounceChance", "Bounce", colour::rose);
    ratchetChance_ = std::make_unique<ParamSlider>(context, "ratchetChance", "Ratchet", colour::rose);
    climb_ = std::make_unique<ParamSlider>(context, "climb", "Climb", colour::rose);
    halftime_ = std::make_unique<ParamSlider>(context, "halftimeChance", "Halftime", colour::rose);

    // Recut
    attackAlign_ = std::make_unique<TogglePill>(context, "attackAlign", "Attack Align", colour::cyan);
    attackThreshold_ = std::make_unique<ParamSlider>(context, "attackThreshold", "Attack Thresh", colour::cyan);
    reverseChance_ = std::make_unique<ParamSlider>(context, "reverseChance", "Reverse Chance", colour::cyan);

    // Classic FX
    crusherOn_ = std::make_unique<TogglePill>(context, "crusher", "BitCrusher", colour::amber);
    bits_ = std::make_unique<RangeSlider>(context, "minBits", "maxBits", "Bit Depth", colour::amber);
    rate_ = std::make_unique<RangeSlider>(context, "minRate", "maxRate", "Sample Rate", colour::amber);
    combOn_ = std::make_unique<TogglePill>(context, "comb", "Comb Filter", colour::violet);
    combType_ = std::make_unique<ChoiceRow>(context, "combType", "Topology", colour::violet);
    combFeedback_ = std::make_unique<ParamSlider>(context, "combFeedback", "Feedback", colour::violet);
    combDelay_ = std::make_unique<RangeSlider>(context, "minDelay", "maxDelay", "Delay Time", colour::violet);

    // Tab 1 — Impact
    impactAttack_ = std::make_unique<ParamSlider>(context, "impactAttackMs", "Env Attack", colour::accent, true);
    impactDecay_ = std::make_unique<ParamSlider>(context, "impactDecayMs", "Env Decay", colour::accent, true);

    filterOn_ = std::make_unique<TogglePill>(context, "impactFilterEnabled", "Slice Filter", colour::blue);
    filterType_ = std::make_unique<ChoiceRow>(context, "impactFilterType", "Type", colour::blue);
    filterChance_ = std::make_unique<ParamSlider>(context, "impactFilterChance", "Chance", colour::blue);
    filterCutoff_ = std::make_unique<ParamSlider>(context, "impactFilterCutoff", "Cutoff", colour::blue);
    filterQ_ = std::make_unique<ParamSlider>(context, "impactFilterQ", "Resonance", colour::blue);
    filterEnv_ = std::make_unique<ParamSlider>(context, "impactFilterEnvOctaves", "Env Depth", colour::blue);
    filterRepeat_ = std::make_unique<ParamSlider>(context, "impactFilterRepeatOctaves", "Repeat Shift", colour::blue);
    filterMix_ = std::make_unique<ParamSlider>(context, "impactFilterMix", "Filter Mix", colour::blue);

    driveOn_ = std::make_unique<TogglePill>(context, "impactDriveEnabled", "Drive / Fold", colour::accent);
    driveMode_ = std::make_unique<ChoiceRow>(context, "impactDriveMode", "Mode", colour::accent);
    driveChance_ = std::make_unique<ParamSlider>(context, "impactDriveChance", "Chance", colour::accent);
    driveDb_ = std::make_unique<ParamSlider>(context, "impactDriveDb", "Drive", colour::accent);
    driveEnv_ = std::make_unique<ParamSlider>(context, "impactDriveEnvDb", "Env Depth", colour::accent);
    driveRepeat_ = std::make_unique<ParamSlider>(context, "impactDriveRepeatDb", "Repeat Drive", colour::accent);
    driveMix_ = std::make_unique<ParamSlider>(context, "impactDriveMix", "Drive Mix", colour::accent);

    transientOn_ = std::make_unique<TogglePill>(context, "impactTransientEnabled", "Transient Shaper", colour::emerald);
    transChance_ = std::make_unique<ParamSlider>(context, "impactTransientChance", "Chance", colour::emerald);
    transAttack_ = std::make_unique<ParamSlider>(context, "impactTransientAttackDb", "Attack", colour::emerald);
    transSustain_ = std::make_unique<ParamSlider>(context, "impactTransientSustainDb", "Sustain", colour::emerald);
    transTime_ = std::make_unique<ParamSlider>(context, "impactTransientTimeMs", "Response", colour::emerald);
    transMix_ = std::make_unique<ParamSlider>(context, "impactTransientMix", "Transient Mix", colour::emerald);

    // Tab 2 — Fracture
    fractureFamily_ =
        std::make_unique<ParamSegmented>(context, "fractureProcedure", "Reader family",
                                         std::vector<std::string>{"Classic", "GrainCut", "TapeCut"}, colour::cyan);
    fracActivity_ = std::make_unique<ParamSlider>(context, "fractureActivity", "Activity", colour::cyan);
    grainSize_ = std::make_unique<ParamSlider>(context, "fractureGrainSizeMs", "Grain Size", colour::cyan);
    grainDensity_ = std::make_unique<ParamSlider>(context, "fractureGrainDensity", "Density", colour::cyan);
    grainPos_ =
        std::make_unique<ParamSlider>(context, "fractureGrainPositionSpreadMs", "Position Spread", colour::cyan);
    grainPitch_ = std::make_unique<ParamSlider>(context, "fractureGrainPitchSpreadCents", "Pitch Spread", colour::cyan);
    grainPan_ = std::make_unique<ParamSlider>(context, "fractureGrainPanSpread", "Pan Spread", colour::cyan);

    tapeStart_ = std::make_unique<ParamSlider>(context, "fractureTapeStartSpeed", "Start Speed", colour::emerald);
    tapeEnd_ = std::make_unique<ParamSlider>(context, "fractureTapeEndSpeed", "End Speed", colour::emerald);
    tapeCurve_ = std::make_unique<ChoiceRow>(context, "fractureTapeCurve", "Curve", colour::emerald);
    tapeCycle_ = std::make_unique<ChoiceRow>(context, "fractureTapeCycle", "Cycle", colour::emerald);
    tapeAlternate_ = std::make_unique<TogglePill>(context, "fractureTapeAlternate", "Alternate Sweep", colour::emerald);

    freqOn_ = std::make_unique<TogglePill>(context, "fractureFrequencyEnabled", "Ring / Freq Shift", colour::violet);
    freqMode_ = std::make_unique<ChoiceRow>(context, "fractureFrequencyMode", "Mode", colour::violet);
    freqChance_ = std::make_unique<ParamSlider>(context, "fractureFrequencyChance", "Chance", colour::violet);
    freqRate_ = std::make_unique<ParamSlider>(context, "fractureFrequencyHz", "Rate", colour::violet);
    freqEnv_ = std::make_unique<ParamSlider>(context, "fractureFrequencyEnvHz", "Env Depth", colour::violet);
    freqRepeat_ = std::make_unique<ParamSlider>(context, "fractureFrequencyRepeatHz", "Repeat Shift", colour::violet);
    freqMix_ = std::make_unique<ParamSlider>(context, "fractureFrequencyMix", "Freq Mix", colour::violet);

    // Tab 3 — Resonance & Spectral
    resOn_ = std::make_unique<TogglePill>(context, "resonanceEnabled", "Tuned Resonator", colour::violet);
    resRoot_ = std::make_unique<ParamSlider>(context, "resonanceRootHz", "Root Pitch", colour::violet);
    resDecay_ = std::make_unique<ParamSlider>(context, "resonanceDecayMs", "Decay Time", colour::violet);
    resExcite_ = std::make_unique<ParamSlider>(context, "resonanceExcitationDb", "Excitation", colour::violet);
    resMix_ = std::make_unique<ParamSlider>(context, "resonanceMix", "Resonator Mix", colour::violet);
    resOrder_ = std::make_unique<ChoiceRow>(context, "resonanceOrder", "Rack Order", colour::violet);

    constexpr std::array<const char*, 4> intIds{"resonanceInterval1", "resonanceInterval2", "resonanceInterval3",
                                                "resonanceInterval4"};
    constexpr std::array<const char*, 4> wtIds{"resonanceWeight1", "resonanceWeight2", "resonanceWeight3",
                                               "resonanceWeight4"};
    constexpr std::array<const char*, 4> vNames{"Voice 1 Int", "Voice 2 Int", "Voice 3 Int", "Voice 4 Int"};
    constexpr std::array<const char*, 4> wNames{"Voice 1 Wt", "Voice 2 Wt", "Voice 3 Wt", "Voice 4 Wt"};
    for (int i = 0; i < 4; ++i) {
        resInt_[i] = std::make_unique<ParamSlider>(context, intIds[static_cast<std::size_t>(i)],
                                                   vNames[static_cast<std::size_t>(i)], colour::violet);
        resWeight_[i] = std::make_unique<ParamSlider>(context, wtIds[static_cast<std::size_t>(i)],
                                                      wNames[static_cast<std::size_t>(i)], colour::blue);
    }

    specOn_ = std::make_unique<TogglePill>(context, "resonanceSpectralEnabled", "Spectral Engine", colour::cyan);
    specFreeze_ = std::make_unique<TogglePill>(context, "resonanceFreeze", "Spectral Freeze", colour::cyan);
    specSmear_ = std::make_unique<ParamSlider>(context, "resonanceSmearMs", "Smear Time", colour::cyan);
    specMix_ = std::make_unique<ParamSlider>(context, "resonanceSpectralMix", "Spectral Mix", colour::cyan);

    // Shared bottom strip
    subdivision_ = std::make_unique<ChoiceRow>(context, "subdivision", "Subdiv", colour::accent);
    seed_ = std::make_unique<ParamSlider>(context, "seed", "Seed", colour::accent, true);
    fade_ = std::make_unique<ParamSlider>(context, "fade", "Fade", colour::accent, true);
    swing_ = std::make_unique<ParamSlider>(context, "swing", "Swing", colour::accent, true);
    accent_ = std::make_unique<ParamSlider>(context, "accent", "Accent", colour::accent, true);
    audioHold_ = std::make_unique<TogglePill>(context, "hold", "Audio Hold", colour::amber);
    trim_ = std::make_unique<ParamSlider>(context, "outputTrim", "Trim", colour::textDim, true);
    wet_ = std::make_unique<ParamSlider>(context, "wet", "Wet Mix", colour::accent, true);

    dragWavBtn_.setAccent(colour::cyan);
    dragWavBtn_.setOnDragOut([this] { context_.model.beginDragCaptureWav(); });
    rollSeed_.setAccent(colour::textDim);
    captureBtn_.setAccent(colour::textDim);
    saveWavBtn_.setAccent(colour::textDim);

    for (visage::Frame* child : std::initializer_list<visage::Frame*>{bars_.get(),
                                                                      duty_.get(),
                                                                      fillDuty_.get(),
                                                                      amp_.get(),
                                                                      pan_.get(),
                                                                      pitch_.get(),
                                                                      repeats_.get(),
                                                                      stutter_.get(),
                                                                      stutterArea_.get(),
                                                                      straight_.get(),
                                                                      regular_.get(),
                                                                      ritard_.get(),
                                                                      accel_.get(),
                                                                      sqActivity_.get(),
                                                                      bounceChance_.get(),
                                                                      ratchetChance_.get(),
                                                                      climb_.get(),
                                                                      halftime_.get(),
                                                                      attackAlign_.get(),
                                                                      attackThreshold_.get(),
                                                                      reverseChance_.get(),
                                                                      crusherOn_.get(),
                                                                      bits_.get(),
                                                                      rate_.get(),
                                                                      combOn_.get(),
                                                                      combType_.get(),
                                                                      combFeedback_.get(),
                                                                      combDelay_.get(),
                                                                      impactAttack_.get(),
                                                                      impactDecay_.get(),
                                                                      filterOn_.get(),
                                                                      filterType_.get(),
                                                                      filterChance_.get(),
                                                                      filterCutoff_.get(),
                                                                      filterQ_.get(),
                                                                      filterEnv_.get(),
                                                                      filterRepeat_.get(),
                                                                      filterMix_.get(),
                                                                      driveOn_.get(),
                                                                      driveMode_.get(),
                                                                      driveChance_.get(),
                                                                      driveDb_.get(),
                                                                      driveEnv_.get(),
                                                                      driveRepeat_.get(),
                                                                      driveMix_.get(),
                                                                      transientOn_.get(),
                                                                      transChance_.get(),
                                                                      transAttack_.get(),
                                                                      transSustain_.get(),
                                                                      transTime_.get(),
                                                                      transMix_.get(),
                                                                      fractureFamily_.get(),
                                                                      fracActivity_.get(),
                                                                      grainSize_.get(),
                                                                      grainDensity_.get(),
                                                                      grainPos_.get(),
                                                                      grainPitch_.get(),
                                                                      grainPan_.get(),
                                                                      tapeStart_.get(),
                                                                      tapeEnd_.get(),
                                                                      tapeCurve_.get(),
                                                                      tapeCycle_.get(),
                                                                      tapeAlternate_.get(),
                                                                      freqOn_.get(),
                                                                      freqMode_.get(),
                                                                      freqChance_.get(),
                                                                      freqRate_.get(),
                                                                      freqEnv_.get(),
                                                                      freqRepeat_.get(),
                                                                      freqMix_.get(),
                                                                      resOn_.get(),
                                                                      resRoot_.get(),
                                                                      resDecay_.get(),
                                                                      resExcite_.get(),
                                                                      resMix_.get(),
                                                                      resOrder_.get(),
                                                                      resInt_[0].get(),
                                                                      resInt_[1].get(),
                                                                      resInt_[2].get(),
                                                                      resInt_[3].get(),
                                                                      resWeight_[0].get(),
                                                                      resWeight_[1].get(),
                                                                      resWeight_[2].get(),
                                                                      resWeight_[3].get(),
                                                                      specOn_.get(),
                                                                      specFreeze_.get(),
                                                                      specSmear_.get(),
                                                                      specMix_.get(),
                                                                      &applySpectral_,
                                                                      subdivision_.get(),
                                                                      seed_.get(),
                                                                      &rollSeed_,
                                                                      fade_.get(),
                                                                      swing_.get(),
                                                                      accent_.get(),
                                                                      audioHold_.get(),
                                                                      &captureBtn_,
                                                                      &dragWavBtn_,
                                                                      &saveWavBtn_,
                                                                      trim_.get(),
                                                                      wet_.get()})
        add(child);

    updateVisibility();
    context_.live.push_back(this);
}

ClassicPanel::~ClassicPanel() { std::erase(context_.live, static_cast<Live*>(this)); }

void ClassicPanel::selectTab(int tabIndex) {
    const int clamped = std::clamp(tabIndex, 0, 3);
    if (clamped == tab_)
        return;
    tab_ = clamped;
    tabs_.setSelected(tab_);
    updateVisibility();
    resized();
    redraw();
}

ClassicPanel::Rect ClassicPanel::columnRect(float top, float h, int col) const {
    const float gap = context_.px(kGap);
    const float colW = (width() - 2.0f * gap) / 3.0f;
    return {static_cast<float>(col) * (colW + gap), top, colW, h};
}

void ClassicPanel::updateVisibility() {
    const int proc = static_cast<int>(std::lround(context_.model.getNormalised("procedure") * 3.0f));
    const int family = static_cast<int>(std::lround(context_.model.getNormalised("fractureProcedure") * 2.0f));
    const bool crusherOn = context_.model.getNormalised("crusher") >= 0.5f;
    const bool combOn = context_.model.getNormalised("comb") >= 0.5f;

    lastProc_ = proc;
    lastFamily_ = family;
    lastCrusher_ = crusherOn;
    lastComb_ = combOn;

    const bool t0 = (tab_ == 0);
    const bool t1 = (tab_ == 1);
    const bool t2 = (tab_ == 2);
    const bool t3 = (tab_ == 3);

    procedure_.setVisible(t0);

    for (visage::Frame* f : std::initializer_list<visage::Frame*>{
             bars_.get(), duty_.get(), fillDuty_.get(), amp_.get(), pan_.get(), pitch_.get(), attackAlign_.get(),
             attackThreshold_.get(), reverseChance_.get(), crusherOn_.get(), bits_.get(), rate_.get(), combOn_.get(),
             combType_.get(), combFeedback_.get(), combDelay_.get()})
        f->setVisible(t0);

    for (visage::Frame* f : std::initializer_list<visage::Frame*>{repeats_.get(), stutter_.get(), stutterArea_.get()})
        f->setVisible(t0 && proc == 0);
    for (visage::Frame* f :
         std::initializer_list<visage::Frame*>{straight_.get(), regular_.get(), ritard_.get(), accel_.get()})
        f->setVisible(t0 && proc == 1);
    sqActivity_->setVisible(t0 && proc == 2);
    for (visage::Frame* f : std::initializer_list<visage::Frame*>{bounceChance_.get(), ratchetChance_.get(),
                                                                  climb_.get(), halftime_.get()})
        f->setVisible(t0 && proc == 3);

    bits_->setDimmed(!crusherOn);
    rate_->setDimmed(!crusherOn);
    combType_->setDimmed(!combOn);
    combFeedback_->setDimmed(!combOn);
    combDelay_->setDimmed(!combOn);
    pitch_->setDimmed(family == 2); // TapeCut replaces common Pitch

    for (visage::Frame* f : std::initializer_list<visage::Frame*>{
             impactAttack_.get(), impactDecay_.get(), filterOn_.get(),    filterType_.get(),   filterChance_.get(),
             filterCutoff_.get(), filterQ_.get(),     filterEnv_.get(),   filterRepeat_.get(), filterMix_.get(),
             driveOn_.get(),      driveMode_.get(),   driveChance_.get(), driveDb_.get(),      driveEnv_.get(),
             driveRepeat_.get(),  driveMix_.get(),    transientOn_.get(), transChance_.get(),  transAttack_.get(),
             transSustain_.get(), transTime_.get(),   transMix_.get()})
        f->setVisible(t1);

    for (visage::Frame* f : std::initializer_list<visage::Frame*>{
             fractureFamily_.get(), fracActivity_.get(), grainSize_.get(), grainDensity_.get(), grainPos_.get(),
             grainPitch_.get(), grainPan_.get(), tapeStart_.get(), tapeEnd_.get(), tapeCurve_.get(), tapeCycle_.get(),
             tapeAlternate_.get(), freqOn_.get(), freqMode_.get(), freqChance_.get(), freqRate_.get(), freqEnv_.get(),
             freqRepeat_.get(), freqMix_.get()})
        f->setVisible(t2);

    const bool grainDim = (family != 1);
    const bool tapeDim = (family != 2);
    for (auto* s : {fracActivity_.get(), grainSize_.get(), grainDensity_.get(), grainPos_.get(), grainPitch_.get(),
                    grainPan_.get()})
        s->setDimmed(grainDim);
    tapeStart_->setDimmed(tapeDim);
    tapeEnd_->setDimmed(tapeDim);
    tapeCurve_->setDimmed(tapeDim);
    tapeCycle_->setDimmed(tapeDim);
    tapeAlternate_->setDimmed(tapeDim);

    for (visage::Frame* f : std::initializer_list<visage::Frame*>{
             resOn_.get(), resRoot_.get(), resDecay_.get(), resExcite_.get(), resMix_.get(), resOrder_.get(),
             resInt_[0].get(), resInt_[1].get(), resInt_[2].get(), resInt_[3].get(), resWeight_[0].get(),
             resWeight_[1].get(), resWeight_[2].get(), resWeight_[3].get(), specOn_.get(), specFreeze_.get(),
             specSmear_.get(), specMix_.get(), &applySpectral_})
        f->setVisible(t3);
}

void ClassicPanel::tick() {
    if (!isVisible())
        return;

    const int proc = static_cast<int>(std::lround(context_.model.getNormalised("procedure") * 3.0f));
    const int family = static_cast<int>(std::lround(context_.model.getNormalised("fractureProcedure") * 2.0f));
    const bool crusherOn = context_.model.getNormalised("crusher") >= 0.5f;
    const bool combOn = context_.model.getNormalised("comb") >= 0.5f;
    if (proc != lastProc_ || family != lastFamily_ || crusherOn != lastCrusher_ || combOn != lastComb_) {
        updateVisibility();
        resized();
        redraw();
    }

    const auto& v = context_.model.visuals();
    applyMode_.setEnabledState(v.canApplyMode);
    applyMode_.setActive(v.canApplyMode);
    applyMode_.setAccent(v.canApplyMode ? colour::amber : colour::textDim);
    applyMode_.setLabel(v.canApplyMode ? "Apply *" : (v.activeStudio ? "Studio Active" : "Live Active"));

    applySpectral_.setEnabledState(v.canApplyMode);
    applySpectral_.setActive(v.canApplyMode);
    applySpectral_.setAccent(v.canApplyMode ? colour::amber : colour::textDim);

    dragWavBtn_.setLabel(v.hasCapture ? "Drag .wav \u2197" : "Drag .wav");
    dragWavBtn_.setActive(v.hasCapture);
    dragWavBtn_.setDragReady(v.hasCapture);
    saveWavBtn_.setEnabledState(v.hasCapture);

    if (v.canApplyMode != lastCanApply_) {
        lastCanApply_ = v.canApplyMode;
        redraw();
    }
}

void ClassicPanel::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const float workTop = context_.px(kNavHeight + kGap);
    const float workH = height() - workTop - context_.px(kBottomHeight + kGap);
    const auto& v = context_.model.visuals();

    const auto drawMultiline = [&](const std::string& text, float x, float y, float w, float lineH, unsigned col) {
        std::size_t start = 0;
        float rowY = y;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::string line = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
            if (!line.empty()) {
                drawLabel(canvas, context_, line, x, rowY, w, lineH, col, visage::Font::kLeft, type::small,
                          Weight::regular);
                rowY += lineH;
            }
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
    };

    const auto drawCard = [&](const Rect& r, const std::string& title, unsigned accent = colour::accent) {
        drawPanel(canvas, context_, r.x, r.y, r.w, r.h, colour::panel, metric::radius);
        // Crisp 2px top accent hairline inside card header
        canvas.setColor(colour::withAlpha(accent, 0.55f));
        canvas.roundedRectangle(r.x + context_.px(kCardPadX), r.y + context_.px(6.0f), context_.px(24.0f),
                                context_.px(2.5f), context_.px(1.25f));
        drawLabel(canvas, context_, title, r.x + context_.px(kCardPadX), r.y + context_.px(11.0f),
                  r.w - 2.0f * context_.px(kCardPadX), context_.px(18.0f), colour::text, visage::Font::kLeft,
                  type::title, Weight::semibold);
    };

    if (tab_ == 0) {
        const Rect c0 = columnRect(workTop, workH, 0);
        const Rect c1 = columnRect(workTop, workH, 1);
        const Rect c2 = columnRect(workTop, workH, 2);

        drawCard(c0, "Phrase & Cut", colour::accent);

        constexpr std::array<const char*, 4> procTitles{"CutProc11", "WarpCut", "SQPusher", "Bounce"};
        const int proc = std::clamp(lastProc_, 0, 3);
        drawCard(c1, std::string("Procedure \u00b7 ") + procTitles[static_cast<std::size_t>(proc)], colour::rose);
        drawCard(c2, "Classic FX \u00b7 Crusher & Comb", colour::amber);

        // Recut divider & title inside column 1
        const float divY = c1.y + context_.px(222.0f);
        canvas.setColor(colour::edge);
        canvas.rectangle(c1.x + context_.px(kCardPadX), divY, c1.w - 2.0f * context_.px(kCardPadX), 1.0f * px);
        drawLabel(canvas, context_, "Recut & Attack Align", c1.x + context_.px(kCardPadX), divY + context_.px(8.0f),
                  c1.w - 2.0f * context_.px(kCardPadX), context_.px(18.0f), colour::cyan, visage::Font::kLeft,
                  type::label, Weight::semibold);

        if (!v.transformStatus.empty()) {
            drawMultiline(v.transformStatus, c1.x + context_.px(kCardPadX), c1.y + c1.h - context_.px(34.0f),
                          c1.w - 2.0f * context_.px(kCardPadX), context_.px(15.0f), colour::textDim);
        }

        // Subtle divider between BitCrusher and Comb in column 2
        const float fxDivY = c2.y + context_.px(166.0f);
        canvas.setColor(colour::edge);
        canvas.rectangle(c2.x + context_.px(kCardPadX), fxDivY, c2.w - 2.0f * context_.px(kCardPadX), 1.0f * px);

        // Helpful reader note in column 0 footer
        const std::string c0Note = lastFamily_ == 2 ? "TapeCut active \u2014 Tape speed overrides Pitch Range"
                                                    : "Structural changes land cleanly on the next bar line";
        drawLabel(canvas, context_, c0Note, c0.x + context_.px(kCardPadX), c0.y + c0.h - context_.px(28.0f),
                  c0.w - 2.0f * context_.px(kCardPadX), context_.px(20.0f), colour::textFaint, visage::Font::kLeft,
                  type::small, Weight::regular);
    } else if (tab_ == 1) {
        const float stripH = context_.px(kHeaderStripH);
        drawPanel(canvas, context_, 0.0f, workTop, width(), stripH, colour::panel, metric::radius);
        drawLabel(canvas, context_, "Shared Cut Envelope", context_.px(14.0f), workTop, context_.px(180.0f), stripH,
                  colour::text, visage::Font::kLeft, type::title, Weight::semibold);
        drawLabel(canvas, context_, "2 ms FX gates  \u00b7  Filter \u2192 Drive \u2192 Transient",
                  width() - context_.px(310.0f), workTop, context_.px(296.0f), stripH, colour::textDim,
                  visage::Font::kRight, type::small, Weight::medium);

        const float cardsTop = workTop + stripH + context_.px(kGap);
        const float cardsH = workH - stripH - context_.px(kGap);
        drawCard(columnRect(cardsTop, cardsH, 0), "Slice Filter", colour::blue);
        drawCard(columnRect(cardsTop, cardsH, 1), "ADAA Drive / Wavefold", colour::accent);
        drawCard(columnRect(cardsTop, cardsH, 2), "Stereo Transient Shaper", colour::emerald);
    } else if (tab_ == 2) {
        const float stripH = context_.px(kHeaderStripH);
        drawPanel(canvas, context_, 0.0f, workTop, width(), stripH, colour::panel, metric::radius);
        drawLabel(canvas, context_, "Reader Family", context_.px(14.0f), workTop, context_.px(110.0f), stripH,
                  colour::text, visage::Font::kLeft, type::title, Weight::semibold);
        const std::string statusLine = v.fractureStatus.empty() ? "Classic reader active" : v.fractureStatus;
        drawLabel(canvas, context_, statusLine, context_.px(440.0f), workTop, width() - context_.px(456.0f), stripH,
                  colour::textDim, visage::Font::kRight, type::small, Weight::medium);

        const float cardsTop = workTop + stripH + context_.px(kGap);
        const float cardsH = workH - stripH - context_.px(kGap);
        drawCard(columnRect(cardsTop, cardsH, 0), "GrainCut \u00b7 8-Voice Polyphase", colour::cyan);
        drawCard(columnRect(cardsTop, cardsH, 1), "TapeCut \u00b7 Speed Trajectory", colour::emerald);
        drawCard(columnRect(cardsTop, cardsH, 2), "Ring / Frequency Shifter", colour::violet);
    } else {
        const Rect c0 = columnRect(workTop, workH, 0);
        const Rect c1 = columnRect(workTop, workH, 1);
        const Rect c2 = columnRect(workTop, workH, 2);

        drawCard(c0, "Tuned Resonator", colour::violet);
        drawCard(c1, "Four Resonator Voices", colour::blue);
        drawCard(c2, "Spectral Freeze / Smear", colour::cyan);

        if (!v.resonatorStatus.empty()) {
            drawMultiline(v.resonatorStatus, c0.x + context_.px(kCardPadX), c0.y + c0.h - context_.px(44.0f),
                          c0.w - 2.0f * context_.px(kCardPadX), context_.px(16.0f), colour::textDim);
        }
        drawLabel(canvas, context_, "Retuning crossfades fixed banks smoothly over 50 ms",
                  c1.x + context_.px(kCardPadX), c1.y + c1.h - context_.px(34.0f), c1.w - 2.0f * context_.px(kCardPadX),
                  context_.px(22.0f), colour::textFaint, visage::Font::kLeft, type::small, Weight::regular);
        if (!v.spectralStatus.empty()) {
            drawMultiline(v.spectralStatus, c2.x + context_.px(kCardPadX), c2.y + context_.px(268.0f),
                          c2.w - 2.0f * context_.px(kCardPadX), context_.px(17.0f), colour::textDim);
        }
    }

    // Shared Classic Bottom Strip Card
    const float botY = height() - context_.px(kBottomHeight);
    drawPanel(canvas, context_, 0.0f, botY, width(), context_.px(kBottomHeight), colour::panel, metric::radius);

    if (!v.holdStatus.empty()) {
        drawLabel(canvas, context_, v.holdStatus, context_.px(984.0f), botY + context_.px(9.0f), context_.px(258.0f),
                  context_.px(28.0f), colour::textDim, visage::Font::kRight, type::small, Weight::medium);
    }
    if (!v.captureStatus.empty()) {
        drawLabel(canvas, context_, v.captureStatus, context_.px(336.0f), botY + context_.px(47.0f),
                  context_.px(500.0f), context_.px(28.0f), colour::textDim, visage::Font::kLeft, type::small,
                  Weight::medium);
    }
}

void ClassicPanel::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float navH = px(kNavHeight);
    tabs_.setBounds(0.0f, 0.0f, px(480.0f), navH);

    // Right side of nav: [Procedure] [Live|Studio] [Apply]
    const float applyW = px(116.0f);
    const float modeW = px(136.0f);
    const float procW = px(340.0f);
    applyMode_.setBounds(width() - applyW, 0.0f, applyW, navH);
    mode_.setBounds(width() - applyW - px(8.0f) - modeW, 0.0f, modeW, navH);
    procedure_.setBounds(width() - applyW - px(8.0f) - modeW - px(12.0f) - procW, 0.0f, procW, navH);

    const float workTop = navH + px(kGap);
    const float workH = height() - workTop - px(kBottomHeight + kGap);
    const float padX = px(kCardPadX);
    const float headerOff = px(36.0f);
    const float rowGap = px(7.0f);
    const float sliderH = px(38.0f);
    const float pillH = px(26.0f);

    const auto stack = [&](const Rect& card, float startY,
                           std::initializer_list<std::pair<visage::Frame*, float>> items) {
        float y = card.y + startY;
        const float cw = card.w - 2.0f * padX;
        for (auto [f, h] : items) {
            f->setBounds(card.x + padX, y, cw, h);
            y += h + rowGap;
        }
    };

    if (tab_ == 0) {
        const Rect c0 = columnRect(workTop, workH, 0);
        const Rect c1 = columnRect(workTop, workH, 1);
        const Rect c2 = columnRect(workTop, workH, 2);

        stack(c0, headerOff,
              {
                  {bars_.get(), sliderH},
                  {duty_.get(), sliderH},
                  {fillDuty_.get(), sliderH},
                  {amp_.get(), sliderH},
                  {pan_.get(), sliderH},
                  {pitch_.get(), sliderH},
              });

        if (lastProc_ == 0)
            stack(c1, headerOff, {{repeats_.get(), sliderH}, {stutter_.get(), sliderH}, {stutterArea_.get(), sliderH}});
        else if (lastProc_ == 1)
            stack(c1, headerOff,
                  {{straight_.get(), sliderH},
                   {regular_.get(), sliderH},
                   {ritard_.get(), sliderH},
                   {accel_.get(), sliderH}});
        else if (lastProc_ == 2)
            stack(c1, headerOff, {{sqActivity_.get(), sliderH}});
        else
            stack(c1, headerOff,
                  {{bounceChance_.get(), sliderH},
                   {ratchetChance_.get(), sliderH},
                   {climb_.get(), sliderH},
                   {halftime_.get(), sliderH}});

        // Recut below divider inside c1
        stack(c1, px(254.0f),
              {
                  {attackAlign_.get(), pillH},
                  {attackThreshold_.get(), sliderH},
                  {reverseChance_.get(), sliderH},
              });

        // Classic FX in c2
        stack(c2, headerOff,
              {
                  {crusherOn_.get(), pillH},
                  {bits_.get(), sliderH},
                  {rate_.get(), sliderH},
              });
        stack(c2, px(180.0f),
              {
                  {combOn_.get(), pillH},
                  {combType_.get(), pillH},
                  {combFeedback_.get(), sliderH},
                  {combDelay_.get(), sliderH},
              });
    } else if (tab_ == 1) {
        const float stripH = px(kHeaderStripH);
        const float cy = workTop + (stripH - px(28.0f)) * 0.5f;
        impactAttack_->setBounds(px(210.0f), cy, px(210.0f), px(28.0f));
        impactDecay_->setBounds(px(434.0f), cy, px(210.0f), px(28.0f));

        const float cardsTop = workTop + stripH + px(kGap);
        const float cardsH = workH - stripH - px(kGap);
        const float sH = px(34.0f);
        const float pH = px(24.0f);
        const float g = px(5.0f);

        const auto stackTight = [&](const Rect& card, std::initializer_list<std::pair<visage::Frame*, float>> items) {
            float y = card.y + headerOff;
            const float cw = card.w - 2.0f * padX;
            for (auto [f, h] : items) {
                f->setBounds(card.x + padX, y, cw, h);
                y += h + g;
            }
        };

        stackTight(columnRect(cardsTop, cardsH, 0), {
                                                        {filterOn_.get(), pH},
                                                        {filterType_.get(), pH},
                                                        {filterChance_.get(), sH},
                                                        {filterCutoff_.get(), sH},
                                                        {filterQ_.get(), sH},
                                                        {filterEnv_.get(), sH},
                                                        {filterRepeat_.get(), sH},
                                                        {filterMix_.get(), sH},
                                                    });
        stackTight(columnRect(cardsTop, cardsH, 1), {
                                                        {driveOn_.get(), pH},
                                                        {driveMode_.get(), pH},
                                                        {driveChance_.get(), sH},
                                                        {driveDb_.get(), sH},
                                                        {driveEnv_.get(), sH},
                                                        {driveRepeat_.get(), sH},
                                                        {driveMix_.get(), sH},
                                                    });
        stackTight(columnRect(cardsTop, cardsH, 2), {
                                                        {transientOn_.get(), pH},
                                                        {transChance_.get(), sH},
                                                        {transAttack_.get(), sH},
                                                        {transSustain_.get(), sH},
                                                        {transTime_.get(), sH},
                                                        {transMix_.get(), sH},
                                                    });
    } else if (tab_ == 2) {
        const float stripH = px(kHeaderStripH);
        const float cy = workTop + (stripH - px(28.0f)) * 0.5f;
        fractureFamily_->setBounds(px(136.0f), cy, px(284.0f), px(28.0f));

        const float cardsTop = workTop + stripH + px(kGap);
        const float cardsH = workH - stripH - px(kGap);
        const float sH = px(35.0f);
        const float pH = px(25.0f);

        stack(columnRect(cardsTop, cardsH, 0), headerOff,
              {
                  {fracActivity_.get(), sH},
                  {grainSize_.get(), sH},
                  {grainDensity_.get(), sH},
                  {grainPos_.get(), sH},
                  {grainPitch_.get(), sH},
                  {grainPan_.get(), sH},
              });
        stack(columnRect(cardsTop, cardsH, 1), headerOff,
              {
                  {tapeStart_.get(), sH},
                  {tapeEnd_.get(), sH},
                  {tapeCurve_.get(), pH},
                  {tapeCycle_.get(), pH},
                  {tapeAlternate_.get(), pH},
              });
        stack(columnRect(cardsTop, cardsH, 2), headerOff,
              {
                  {freqOn_.get(), pH},
                  {freqMode_.get(), pH},
                  {freqChance_.get(), sH},
                  {freqRate_.get(), sH},
                  {freqEnv_.get(), sH},
                  {freqRepeat_.get(), sH},
                  {freqMix_.get(), sH},
              });
    } else {
        const Rect c0 = columnRect(workTop, workH, 0);
        const Rect c1 = columnRect(workTop, workH, 1);
        const Rect c2 = columnRect(workTop, workH, 2);

        stack(c0, headerOff,
              {
                  {resOn_.get(), pillH},
                  {resRoot_.get(), sliderH},
                  {resDecay_.get(), sliderH},
                  {resExcite_.get(), sliderH},
                  {resMix_.get(), sliderH},
                  {resOrder_.get(), pillH},
              });

        // 4 voices: paired Interval + Weight columns inside c1
        const float cw = c1.w - 2.0f * padX;
        const float pairGap = px(12.0f);
        const float halfW = (cw - pairGap) * 0.5f;
        float vy = c1.y + headerOff;
        for (int i = 0; i < 4; ++i) {
            resInt_[i]->setBounds(c1.x + padX, vy, halfW, sliderH);
            resWeight_[i]->setBounds(c1.x + padX + halfW + pairGap, vy, halfW, sliderH);
            vy += sliderH + px(16.0f);
        }

        stack(c2, headerOff,
              {
                  {specOn_.get(), pillH},
                  {specFreeze_.get(), pillH},
                  {specSmear_.get(), sliderH},
                  {specMix_.get(), sliderH},
                  {&applySpectral_, px(30.0f)},
              });
    }

    // Shared Classic bottom strip (2 tidy rows)
    const float botY = height() - px(kBottomHeight);
    const float r1Y = botY + px(9.0f);
    const float r2Y = botY + px(47.0f);
    const float bh = px(28.0f);

    float x = px(12.0f);
    subdivision_->setBounds(x, r1Y, px(152.0f), bh);
    x += px(160.0f);
    seed_->setBounds(x, r1Y, px(136.0f), bh);
    x += px(142.0f);
    rollSeed_.setBounds(x, r1Y, px(52.0f), bh);
    x += px(62.0f);
    fade_->setBounds(x, r1Y, px(136.0f), bh);
    x += px(144.0f);
    swing_->setBounds(x, r1Y, px(144.0f), bh);
    x += px(152.0f);
    accent_->setBounds(x, r1Y, px(144.0f), bh);
    x += px(154.0f);
    audioHold_->setBounds(x, r1Y, px(108.0f), bh);

    float x2 = px(12.0f);
    captureBtn_.setBounds(x2, r2Y, px(86.0f), bh);
    x2 += px(94.0f);
    dragWavBtn_.setBounds(x2, r2Y, px(124.0f), bh);
    x2 += px(132.0f);
    saveWavBtn_.setBounds(x2, r2Y, px(76.0f), bh);

    const float wetW = px(180.0f);
    const float trimW = px(162.0f);
    wet_->setBounds(width() - px(12.0f) - wetW, r2Y, wetW, bh);
    trim_->setBounds(width() - px(12.0f) - wetW - px(10.0f) - trimW, r2Y, trimW, bh);
}

} // namespace lc::ui
