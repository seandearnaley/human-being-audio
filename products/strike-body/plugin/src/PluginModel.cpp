// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include <algorithm>
#include <cmath>

namespace sb {

PluginModel::PluginModel(PluginProcessor& processor)
    : processor_(processor), rng_(static_cast<std::uint32_t>(juce::Time::getHighResolutionTicks() & 0xffffffffu)) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(paramSpecs()[i].key));
    visuals_ = processor_.telemetry.read();
}

float PluginModel::get(ParamId id) const {
    const auto idx = static_cast<std::size_t>(id);
    if (params_[idx] == nullptr)
        return paramSpecs()[idx].def;
    return params_[idx]->convertFrom0to1(params_[idx]->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_)
        recordUndoBoundary();
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->setValueNotifyingHost(p->convertTo0to1(clampValue(specOf(id), real)));
}

void PluginModel::end(ParamId id) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->endChangeGesture();
}

void PluginModel::commitParam(ParamId id, float real) {
    if (auto* p = params_[static_cast<std::size_t>(id)]) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(clampValue(specOf(id), real)));
        p->endChangeGesture();
    }
}

void PluginModel::applyParamsBatch(const Params& next, const std::string& label) {
    recordUndoBoundary();
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::bypass)
            continue;
        commitParam(id, next.real(id));
    }
    if (!label.empty())
        processor_.presets.setCurrentName(label);
    applyingSnapshot_ = false;
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 1.0f);
}

void PluginModel::triggerAudition(Articulation art, float velocity) { processor_.triggerAudition(art, velocity); }

void PluginModel::runAutoAlign() {
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const AlignResult res = solveAutoAlign(processor_.currentParams(), sr);

    recordUndoBoundary();
    applyingSnapshot_ = true;
    commitParam(ParamId::bodyPhase, res.bodyPhaseDeg);
    commitParam(ParamId::shellOffsetMs, res.shellOffsetMs);
    commitParam(ParamId::shellInvert, res.shellInvert ? 1.0f : 0.0f);
    commitParam(ParamId::wireOffsetMs, res.wireOffsetMs);
    commitParam(ParamId::wireInvert, res.wireInvert ? 1.0f : 0.0f);
    applyingSnapshot_ = false;

    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 1.0f);
}

void PluginModel::loadArchetype(ui::Archetype kind) {
    Params p = Params::defaults();
    std::string name = "Init Snare";

    switch (kind) {
    case ui::Archetype::snare:
        name = "Init Neuro Snare";
        p.set(ParamId::bodyHz, 185.0f);
        p.set(ParamId::bodyWave, 1.0f); // Warm 909
        p.set(ParamId::pitchDrop, 19.0f);
        p.set(ParamId::pitchCurve, 62.0f);
        p.set(ParamId::pitchTime, 11.0f);
        p.set(ParamId::bodyDecay, 150.0f);
        p.set(ParamId::bodyFm, 20.0f);
        p.set(ParamId::bodyLevel, 92.0f);
        p.set(ParamId::shellModel, 0.0f); // Snare Shell
        p.set(ParamId::malletType, 0.0f); // Wood Stick
        p.set(ParamId::shellTune, 4.0f);
        p.set(ParamId::shellTension, 50.0f);
        p.set(ParamId::strikePos, 32.0f);
        p.set(ParamId::shellDecay, 170.0f);
        p.set(ParamId::shellDamp, 44.0f);
        p.set(ParamId::shellLevel, 74.0f);
        p.set(ParamId::wireColor, 0.0f);
        p.set(ParamId::wireBuzz, 42.0f);
        p.set(ParamId::clickAmt, 72.0f);
        p.set(ParamId::wireDuck, 78.0f);
        p.set(ParamId::wireLevel, 74.0f);
        p.set(ParamId::disperserAmt, 32.0f);
        p.set(ParamId::disperserHz, 380.0f);
        p.set(ParamId::drive, 42.0f);
        p.set(ParamId::scoopAmt, 24.0f);
        p.set(ParamId::roomAmt, 16.0f);
        break;

    case ui::Archetype::kick:
        name = "Init Neuro Kick";
        p.set(ParamId::bodyHz, 50.0f);
        p.set(ParamId::bodyWave, 0.0f); // Pure Sine
        p.set(ParamId::pitchDrop, 30.0f);
        p.set(ParamId::pitchCurve, 68.0f);
        p.set(ParamId::pitchTime, 14.0f);
        p.set(ParamId::bodyDecay, 165.0f);
        p.set(ParamId::bodyFm, 10.0f);
        p.set(ParamId::bodyLevel, 100.0f);
        p.set(ParamId::shellModel, 2.0f); // Drumhead
        p.set(ParamId::malletType, 1.0f); // Hard Felt
        p.set(ParamId::shellTune, 12.0f);
        p.set(ParamId::shellTension, 38.0f);
        p.set(ParamId::strikePos, 12.0f);
        p.set(ParamId::shellDecay, 72.0f);
        p.set(ParamId::shellDamp, 78.0f);
        p.set(ParamId::shellSpread, 0.0f);
        p.set(ParamId::shellLevel, 32.0f);
        p.set(ParamId::clickAmt, 76.0f);
        p.set(ParamId::clickTone, 3400.0f);
        p.set(ParamId::wireDuck, 92.0f);
        p.set(ParamId::wireDecay, 28.0f);
        p.set(ParamId::wireWidth, 0.0f);
        p.set(ParamId::wireLevel, 12.0f);
        p.set(ParamId::disperserAmt, 38.0f);
        p.set(ParamId::disperserHz, 145.0f);
        p.set(ParamId::drive, 44.0f);
        p.set(ParamId::driveModel, 1.0f); // Tube
        p.set(ParamId::scoopAmt, 10.0f);
        p.set(ParamId::roomAmt, 0.0f);
        break;

    case ui::Archetype::clap:
        name = "Init Layered Clap";
        p.set(ParamId::bodyHz, 230.0f);
        p.set(ParamId::bodyWave, 2.0f); // Tri-Punch
        p.set(ParamId::pitchDrop, 14.0f);
        p.set(ParamId::pitchCurve, 70.0f);
        p.set(ParamId::pitchTime, 7.0f);
        p.set(ParamId::bodyDecay, 78.0f);
        p.set(ParamId::bodyLevel, 36.0f);
        p.set(ParamId::shellModel, 0.0f); // Snare Shell
        p.set(ParamId::malletType, 3.0f); // Rim Flam
        p.set(ParamId::shellTune, 7.0f);
        p.set(ParamId::shellTension, 44.0f);
        p.set(ParamId::strikePos, 66.0f);
        p.set(ParamId::shellDecay, 130.0f);
        p.set(ParamId::shellDamp, 32.0f);
        p.set(ParamId::shellSpread, 74.0f);
        p.set(ParamId::shellLevel, 68.0f);
        p.set(ParamId::wireColor, 2.0f); // Air Sizzle
        p.set(ParamId::wireBuzz, 55.0f);
        p.set(ParamId::clickAmt, 84.0f);
        p.set(ParamId::clickTone, 5600.0f);
        p.set(ParamId::wireDuck, 34.0f);
        p.set(ParamId::wireDuckMs, 6.0f);
        p.set(ParamId::wireTone, 4200.0f);
        p.set(ParamId::wireDecay, 195.0f);
        p.set(ParamId::wireWidth, 88.0f);
        p.set(ParamId::wireLevel, 94.0f);
        p.set(ParamId::disperserAmt, 24.0f);
        p.set(ParamId::disperserHz, 640.0f);
        p.set(ParamId::drive, 38.0f);
        p.set(ParamId::scoopAmt, 36.0f);
        p.set(ParamId::roomAmt, 38.0f);
        break;

    case ui::Archetype::metal:
        name = "Init Steel Anvil";
        p.set(ParamId::bodyHz, 145.0f);
        p.set(ParamId::bodyWave, 3.0f); // Sub-Square
        p.set(ParamId::pitchDrop, 12.0f);
        p.set(ParamId::bodyFm, 48.0f);
        p.set(ParamId::bodyFmRatio, 2.76f);
        p.set(ParamId::bodyDecay, 190.0f);
        p.set(ParamId::bodyLevel, 54.0f);
        p.set(ParamId::shellModel, 1.0f); // Kirchhoff Plate
        p.set(ParamId::malletType, 2.0f); // Metal Hammer
        p.set(ParamId::shellTune, 11.0f);
        p.set(ParamId::shellTension, 68.0f);
        p.set(ParamId::strikePos, 74.0f);
        p.set(ParamId::shellStretch, 22.0f);
        p.set(ParamId::shellDecay, 420.0f);
        p.set(ParamId::shellDamp, 14.0f);
        p.set(ParamId::shellSpread, 78.0f);
        p.set(ParamId::shellLevel, 96.0f);
        p.set(ParamId::wireColor, 1.0f); // Crushed Metal
        p.set(ParamId::wireBuzz, 62.0f);
        p.set(ParamId::clickAmt, 82.0f);
        p.set(ParamId::clickTone, 7600.0f);
        p.set(ParamId::wireLevel, 44.0f);
        p.set(ParamId::disperserAmt, 36.0f);
        p.set(ParamId::disperserHz, 520.0f);
        p.set(ParamId::drive, 56.0f);
        p.set(ParamId::driveModel, 2.0f); // Buchla
        p.set(ParamId::scoopAmt, 22.0f);
        p.set(ParamId::roomAmt, 32.0f);
        break;

    case ui::Archetype::tom:
        name = "Init Tension Tom";
        p.set(ParamId::bodyHz, 96.0f);
        p.set(ParamId::bodyWave, 1.0f); // Warm 909
        p.set(ParamId::pitchDrop, 12.0f);
        p.set(ParamId::pitchCurve, 44.0f);
        p.set(ParamId::pitchTime, 28.0f);
        p.set(ParamId::bodyDecay, 260.0f);
        p.set(ParamId::bodyFm, 8.0f);
        p.set(ParamId::bodyLevel, 96.0f);
        p.set(ParamId::shellModel, 2.0f); // Drumhead
        p.set(ParamId::malletType, 4.0f); // Skin Thump
        p.set(ParamId::shellTune, 0.0f);
        p.set(ParamId::shellTension, 56.0f);
        p.set(ParamId::strikePos, 24.0f);
        p.set(ParamId::shellDecay, 310.0f);
        p.set(ParamId::shellDamp, 58.0f);
        p.set(ParamId::shellSpread, 28.0f);
        p.set(ParamId::shellLevel, 64.0f);
        p.set(ParamId::clickAmt, 42.0f);
        p.set(ParamId::clickTone, 2400.0f);
        p.set(ParamId::wireLevel, 16.0f);
        p.set(ParamId::disperserAmt, 14.0f);
        p.set(ParamId::drive, 28.0f);
        p.set(ParamId::driveModel, 1.0f); // Tube
        p.set(ParamId::scoopAmt, -16.0f);
        p.set(ParamId::roomAmt, 24.0f);
        p.set(ParamId::chokeMode, 1.0f); // Poly 4x
        p.set(ParamId::keyTrack, 1.0f);  // Chromatic
        break;

    case ui::Archetype::perc:
        name = "Init Hyperpop Perc";
        p.set(ParamId::bodyHz, 295.0f);
        p.set(ParamId::bodyWave, 2.0f); // Tri-Punch
        p.set(ParamId::pitchDrop, 24.0f);
        p.set(ParamId::pitchCurve, 78.0f);
        p.set(ParamId::pitchTime, 9.0f);
        p.set(ParamId::bodyDecay, 85.0f);
        p.set(ParamId::bodyFm, 34.0f);
        p.set(ParamId::bodyFmRatio, 3.1f);
        p.set(ParamId::bodyLevel, 78.0f);
        p.set(ParamId::shellModel, 4.0f); // Hollow Pipe
        p.set(ParamId::malletType, 5.0f); // Laser Snap
        p.set(ParamId::shellTune, 9.0f);
        p.set(ParamId::shellTension, 74.0f);
        p.set(ParamId::strikePos, 52.0f);
        p.set(ParamId::shellDecay, 140.0f);
        p.set(ParamId::shellDamp, 28.0f);
        p.set(ParamId::shellSpread, 66.0f);
        p.set(ParamId::shellLevel, 88.0f);
        p.set(ParamId::wireLevel, 24.0f);
        p.set(ParamId::clickAmt, 78.0f);
        p.set(ParamId::disperserAmt, 64.0f);
        p.set(ParamId::disperserHz, 540.0f);
        p.set(ParamId::drive, 46.0f);
        p.set(ParamId::driveModel, 3.0f); // Fold
        p.set(ParamId::scoopAmt, 28.0f);
        p.set(ParamId::roomAmt, 22.0f);
        p.set(ParamId::chokeMode, 1.0f); // Poly 4x
        p.set(ParamId::keyTrack, 1.0f);  // Chromatic
        break;
    }

    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const auto align = solveAutoAlign(p, sr);
    p.set(ParamId::bodyPhase, align.bodyPhaseDeg);
    p.set(ParamId::shellOffsetMs, align.shellOffsetMs);
    p.set(ParamId::shellInvert, align.shellInvert ? 1.0f : 0.0f);
    p.set(ParamId::wireOffsetMs, align.wireOffsetMs);
    p.set(ParamId::wireInvert, align.wireInvert ? 1.0f : 0.0f);

    applyParamsBatch(p, name);
}

void PluginModel::mutateSound() {
    Params p = processor_.currentParams();
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);

    auto jitterMul = [&](ParamId id, float pct) {
        const auto& s = specOf(id);
        p.set(id, clampValue(s, p.real(id) * (1.0f + uni(rng_) * pct)));
    };
    auto jitterAdd = [&](ParamId id, float delta) {
        const auto& s = specOf(id);
        p.set(id, clampValue(s, p.real(id) + uni(rng_) * delta));
    };

    jitterMul(ParamId::bodyHz, 0.08f);
    jitterAdd(ParamId::pitchDrop, 3.5f);
    jitterAdd(ParamId::pitchCurve, 14.0f);
    jitterMul(ParamId::pitchTime, 0.18f);
    jitterAdd(ParamId::bodyFm, 12.0f);
    jitterAdd(ParamId::shellTune, 3.0f);
    jitterAdd(ParamId::shellTension, 15.0f);
    jitterAdd(ParamId::strikePos, 18.0f);
    jitterAdd(ParamId::shellStretch, 14.0f);
    jitterAdd(ParamId::shellDamp, 12.0f);
    jitterMul(ParamId::wireTone, 0.16f);
    jitterMul(ParamId::clickTone, 0.16f);
    jitterAdd(ParamId::wireBuzz, 16.0f);
    jitterMul(ParamId::disperserHz, 0.18f);
    jitterAdd(ParamId::scoopAmt, 14.0f);
    jitterAdd(ParamId::roomAmt, 8.0f);

    if (uni(rng_) > 0.55f) {
        std::uniform_int_distribution<int> malletDist(0, 5);
        p.set(ParamId::malletType, static_cast<float>(malletDist(rng_)));
    }
    if (uni(rng_) > 0.62f) {
        std::uniform_int_distribution<int> waveDist(0, 3);
        p.set(ParamId::bodyWave, static_cast<float>(waveDist(rng_)));
    }

    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const auto align = solveAutoAlign(p, sr);
    p.set(ParamId::bodyPhase, align.bodyPhaseDeg);
    p.set(ParamId::shellOffsetMs, align.shellOffsetMs);
    p.set(ParamId::shellInvert, align.shellInvert ? 1.0f : 0.0f);
    p.set(ParamId::wireOffsetMs, align.wireOffsetMs);
    p.set(ParamId::wireInvert, align.wireInvert ? 1.0f : 0.0f);

    std::string base = processor_.presets.currentName();
    if (base.find(" (Mut)") == std::string::npos)
        base += " (Mut)";
    applyParamsBatch(p, base);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.presets.load(index);
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 0.96f);
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.presets.step(delta);
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 0.96f);
}

void PluginModel::startBounce() {
    OneShotRequest req;
    req.params = processor_.currentParams();
    req.articulation = Articulation::main;
    req.velocity = 1.0f;
    req.pitchRatio = 1.0f;
    req.sampleRate = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    req.oversampling = 8;
    req.normalizePeak = true;
    processor_.bouncer.start(req, juce::String(processor_.presets.currentName()));
}

void PluginModel::dragBouncedWav() {
    const auto file = processor_.bouncer.file();
    if (dragSource_ == nullptr || !file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : paramSpecs()[i].def;
    }
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(snapshot.params[i]));
    }
    if (!snapshot.presetName.empty())
        processor_.presets.setCurrentName(snapshot.presetName);
    applyingSnapshot_ = false;
    processor_.bouncer.invalidate();
}

void PluginModel::recordUndoBoundary() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
}

void PluginModel::undo() {
    if (const auto target = processor_.history.undo(captureSnapshot()))
        applySnapshot(*target);
}

void PluginModel::redo() {
    if (const auto target = processor_.history.redo(captureSnapshot()))
        applySnapshot(*target);
}

void PluginModel::toggleAB() { applySnapshot(processor_.history.toggleAB(captureSnapshot())); }

void PluginModel::copyAB() { processor_.history.copyActiveToOther(captureSnapshot()); }

} // namespace sb
