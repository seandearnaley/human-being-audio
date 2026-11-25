// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginModel.h"

#include "Parameters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace livecut {

namespace {
constexpr std::array<const char*, 8> kPadProgramNames{"Cut",    "Bounce", "Glitch", "Suction",
                                                      "Filter", "Tape",   "Loops",  "Rush"};

constexpr std::array<const char*, 4> kProcedureNames{"CutProc11", "WarpCut", "SQPusher", "Bounce"};

std::string buildHeadline(const DisplayState& display, const juce::AudioProcessorValueTreeState& state, bool onPad) {
    const auto status = display.status.load(std::memory_order_relaxed);
    if (onPad) {
        const int pending = display.padPendingProgram.load(std::memory_order_relaxed);
        if (pending >= 0 && pending < 8) {
            const int active = std::clamp(display.padActiveProgram.load(std::memory_order_relaxed), 0, 7);
            const bool bar = state.getRawParameterValue(ids::padQuantize)->load() > 0.5f;
            return std::string(kPadProgramNames[static_cast<std::size_t>(active)]) + " \u2192 " +
                   kPadProgramNames[static_cast<std::size_t>(pending)] + (bar ? " on bar" : " on beat");
        }
        if (status == DisplayState::Status::startingSoon)
            return display.padEngaged.load(std::memory_order_relaxed) ? "Starting" : "Press pad or C1\u2013G1";
    }

    switch (status) {
    case DisplayState::Status::waitingForHost:
        return "Waiting for transport";
    case DisplayState::Status::barTooLong:
        return "Bar too slow (>8s)";
    case DisplayState::Status::startingSoon:
        return "Waiting for grid";
    case DisplayState::Status::filling:
        return "Filling Studio buffer";
    case DisplayState::Status::planOverflow:
        return "Plan overflow";
    case DisplayState::Status::sourceUnavailable:
        return "Source unavailable";
    case DisplayState::Status::preparationError:
        return "Preparation error";
    case DisplayState::Status::streamError:
        return "Stream error";
    case DisplayState::Status::cutting:
        break;
    }

    if (onPad) {
        const int active = std::clamp(display.padActiveProgram.load(std::memory_order_relaxed), 0, 7);
        return std::string("Pad \u00b7 ") + kPadProgramNames[static_cast<std::size_t>(active)];
    }

    const int proc =
        std::clamp(static_cast<int>(std::lround(state.getRawParameterValue(ids::procedure)->load())), 0, 3);
    const int bars = std::max(1, display.phraseBars.load(std::memory_order_relaxed));
    return std::string(kProcedureNames[static_cast<std::size_t>(proc)]) + " \u00b7 " + std::to_string(bars) +
           (bars == 1 ? " bar" : " bars");
}
} // namespace

PluginModel::PluginModel(LivecutAudioProcessor& processor) : processor_(processor) {
    for (auto* p : processor_.getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            params_.emplace(ranged->getParameterID().toStdString(), ranged);
    }
    refreshVisuals();
}

PluginModel::~PluginModel() { *alive_ = false; }

juce::RangedAudioParameter* PluginModel::findParam(std::string_view id) const {
    const auto it = params_.find(std::string(id));
    return it == params_.end() ? nullptr : it->second;
}

float PluginModel::getNormalised(std::string_view id) const {
    if (auto* p = findParam(id))
        return std::clamp(p->getValue(), 0.0f, 1.0f);
    return 0.0f;
}

float PluginModel::getReal(std::string_view id) const {
    if (auto* p = findParam(id))
        return p->convertFrom0to1(p->getValue());
    return 0.0f;
}

float PluginModel::defaultNormalised(std::string_view id) const {
    if (auto* p = findParam(id))
        return p->getDefaultValue();
    return 0.0f;
}

void PluginModel::beginGesture(std::string_view id) {
    if (auto* p = findParam(id)) {
        const std::uint64_t tag = std::hash<std::string_view>{}(id);
        processor_.pushCoalescedUndo(tag);
        p->beginChangeGesture();
    }
}

void PluginModel::setNormalised(std::string_view id, float normalised) {
    if (id == ids::padX || id == ids::padY)
        processor_.clearMidiAxisOverride();
    if (auto* p = findParam(id))
        p->setValueNotifyingHost(std::clamp(normalised, 0.0f, 1.0f));
}

void PluginModel::endGesture(std::string_view id) {
    if (auto* p = findParam(id))
        p->endChangeGesture();
}

std::string PluginModel::formatValue(std::string_view id) const {
    if (auto* p = findParam(id)) {
        const auto text = p->getCurrentValueAsText();
        const auto label = p->getLabel();
        if (label.isNotEmpty() && !text.containsIgnoreCase(label))
            return (text + " " + label).toStdString();
        return text.toStdString();
    }
    return {};
}

std::vector<std::string> PluginModel::choices(std::string_view id) const {
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(findParam(id))) {
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(choice->choices.size()));
        for (const auto& s : choice->choices)
            out.push_back(s.toStdString());
        return out;
    }
    return {};
}

bool PluginModel::isBool(std::string_view id) const {
    if (auto* p = findParam(id))
        return p->isBoolean();
    return false;
}

std::vector<lc::ui::PresetEntry> PluginModel::presets() const {
    const auto& entries = processor_.getPresets().all();
    std::vector<lc::ui::PresetEntry> out;
    out.reserve(entries.size());
    for (const auto& e : entries)
        out.push_back({e.name, e.category, e.category != "User"});
    return out;
}

std::string PluginModel::presetName() const { return processor_.getPresets().currentName(); }

void PluginModel::loadPreset(int index) {
    processor_.pushUndoSnapshot();
    processor_.getPresets().load(index);
    processor_.applyRequestedMode();
}

void PluginModel::stepPreset(int delta) {
    processor_.pushUndoSnapshot();
    processor_.getPresets().step(delta);
    processor_.applyRequestedMode();
}

void PluginModel::savePreset() {
    auto dir = Presets::userDirectory();
    dir.createDirectory();
    const auto suggested = dir.getChildFile(juce::String(presetName()) + ".json");
    fileChooser_ = std::make_unique<juce::FileChooser>("Save Livecut Redux Preset", suggested, "*.json");
    const auto alive = alive_;
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, alive](const juce::FileChooser& chooser) {
                                  if (!*alive)
                                      return;
                                  const auto file = chooser.getResult();
                                  if (file != juce::File{})
                                      processor_.getPresets().saveToFile(file);
                              });
}

bool PluginModel::canUndo() const { return processor_.canUndo(); }
bool PluginModel::canRedo() const { return processor_.canRedo(); }
void PluginModel::undo() { processor_.undo(); }
void PluginModel::redo() { processor_.redo(); }
bool PluginModel::isSlotB() const { return processor_.isSlotB(); }
void PluginModel::toggleAB() { processor_.toggleAB(); }
void PluginModel::copyAB() { processor_.copyAB(); }
void PluginModel::rollSeed() { processor_.rollSeed(); }

void PluginModel::requestCapture() { processor_.getCaptureService().requestCapture(); }

void PluginModel::saveCapture() {
    auto& cap = processor_.getCaptureService();
    if (!cap.hasPhraseReady())
        return;
    const auto dir = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    const auto suggested = dir.getChildFile("Livecut-Phrase.wav");
    fileChooser_ = std::make_unique<juce::FileChooser>("Save Captured Phrase", suggested, "*.wav");
    const auto alive = alive_;
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, alive](const juce::FileChooser& chooser) {
                                  if (!*alive)
                                      return;
                                  const auto file = chooser.getResult();
                                  if (file != juce::File{})
                                      processor_.getCaptureService().requestSave(file);
                              });
}

void PluginModel::beginDragCaptureWav() {
    const auto file = processor_.getCaptureService().prepareDragWav(juce::String(presetName()));
    if (!file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

void PluginModel::applyRequestedMode() { processor_.applyRequestedMode(); }

void PluginModel::refreshVisuals() {
    const auto& d = processor_.getDisplayState();
    const auto& state = processor_.getParameterState();
    const bool onPad = state.getRawParameterValue(ids::surface)->load() < 0.5f;

    const auto status = d.status.load(std::memory_order_relaxed);
    visuals_.statusCode = static_cast<int>(status);
    visuals_.cutting = (status == DisplayState::Status::cutting);
    visuals_.waitingHost = (status == DisplayState::Status::waitingForHost);
    visuals_.bpm = d.bpm.load(std::memory_order_relaxed);
    visuals_.beatsPerBar = 4.0;
    visuals_.beat = d.beat.load(std::memory_order_relaxed);
    visuals_.unitsPerBar = std::max(1, d.unitsPerBar.load(std::memory_order_relaxed));
    visuals_.phraseBars = std::max(1, d.phraseBars.load(std::memory_order_relaxed));
    visuals_.blockStart = d.blockStart.load(std::memory_order_relaxed);
    visuals_.blockEnd = d.blockEnd.load(std::memory_order_relaxed);
    visuals_.positionInPhrase = d.position.load(std::memory_order_relaxed);

    visuals_.padActiveProgram = d.padActiveProgram.load(std::memory_order_relaxed);
    visuals_.padPendingProgram = d.padPendingProgram.load(std::memory_order_relaxed);
    visuals_.padEngaged = d.padEngaged.load(std::memory_order_relaxed);
    visuals_.midiPadMask = d.midiPadMask.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < lc::ui::VisualState::kWaveformBins; ++i) {
        visuals_.waveform[i] = d.waveform[i].load(std::memory_order_relaxed);
        visuals_.sliceMarks[i] = d.sliceMarks[i].load(std::memory_order_relaxed) > 0.5f ? 1u : 0u;
    }
    visuals_.outputPeakL = d.outputPeakL.load(std::memory_order_relaxed);
    visuals_.outputPeakR = d.outputPeakR.load(std::memory_order_relaxed);
    visuals_.limiterGrDb = d.limiterGrDb.load(std::memory_order_relaxed);

    visuals_.headline = buildHeadline(d, state, onPad);
    visuals_.hasCapture = processor_.getCaptureService().hasPhraseReady();
    visuals_.captureStatus = processor_.getCaptureService().statusText().toStdString();
    visuals_.holdStatus = processor_.holdStatusText().toStdString();
    visuals_.transformStatus = processor_.transformStatusText().toStdString();

    // Fracture summary
    switch (d.fractureState.load(std::memory_order_relaxed)) {
    case engine::FractureState::off:
        visuals_.fractureStatus = "Classic reader; Ring / Shift available";
        break;
    case engine::FractureState::waiting:
        visuals_.fractureStatus = "Waiting for Grain history";
        break;
    case engine::FractureState::playing:
        visuals_.fractureStatus =
            d.fractureFamily.load(std::memory_order_relaxed) == engine::FractureProcedure::grainCut
                ? ("GrainCut \u00b7 " + std::to_string(d.fractureVoices.load(std::memory_order_relaxed)) + " voices")
                : ("TapeCut \u00b7 " +
                   juce::String(d.fractureTapeSpeed.load(std::memory_order_relaxed), 2).toStdString() + "x");
        break;
    case engine::FractureState::ended:
        visuals_.fractureStatus = "Cut ended; waiting for next cut";
        break;
    case engine::FractureState::sourceLost:
        visuals_.fractureStatus = "Source lost";
        break;
    case engine::FractureState::transformBudgetExhausted:
        visuals_.fractureStatus = "Transform budget exhausted";
        break;
    case engine::FractureState::capacityError:
        visuals_.fractureStatus = "Voice capacity limit";
        break;
    case engine::FractureState::contextUnavailable:
        visuals_.fractureStatus = "Source context unavailable";
        break;
    }

    // Resonator summary
    switch (d.resonatorState.load(std::memory_order_relaxed)) {
    case engine::Resonator::State::ready:
        visuals_.resonatorStatus =
            d.resonatorRetuning.load(std::memory_order_relaxed) ? "Retuning (50 ms crossfade)" : "Resonator ready";
        break;
    case engine::Resonator::State::invalidWeights:
        visuals_.resonatorStatus = "All weights zero (silent wet)";
        break;
    case engine::Resonator::State::invalidParameters:
        visuals_.resonatorStatus = "Invalid tuning";
        break;
    case engine::Resonator::State::unsupportedRate:
        visuals_.resonatorStatus = "Unsupported sample rate";
        break;
    }

    // Mode & Spectral summary
    const auto modeState = processor_.modeState();
    const bool reqStudio = state.getRawParameterValue(ids::mode)->load() > 0.5f;
    const bool reqSpectral = state.getRawParameterValue(ids::resonanceSpectralEnabled)->load() > 0.5f;
    const bool fault = d.spectralFault.load(std::memory_order_relaxed) != spectral::Fault::none;

    visuals_.activeStudio = (processor_.activeMode() == ProcessingMode::studio);
    visuals_.activeSpectral = processor_.activeSpectral();
    visuals_.canApplyMode = modeState != LivecutAudioProcessor::ModeState::unprepared &&
                            modeState != LivecutAudioProcessor::ModeState::pendingPlaying &&
                            modeState != LivecutAudioProcessor::ModeState::pendingHostChange &&
                            (reqStudio != visuals_.activeStudio || reqSpectral != visuals_.activeSpectral ||
                             modeState != LivecutAudioProcessor::ModeState::active || fault);
    visuals_.modePending = visuals_.canApplyMode;

    const auto modeLabel = [](bool studio) { return studio ? "Studio" : "Live"; };
    std::string spec = std::string("Active: ") + modeLabel(visuals_.activeStudio) +
                       (visuals_.activeSpectral ? " / Spectral on\n" : " / Spectral off\n") +
                       "Requested: " + modeLabel(reqStudio) + (reqSpectral ? " / Spectral on\n" : " / Spectral off\n") +
                       processor_.spectralStatusText().toStdString();
    visuals_.spectralStatus = std::move(spec);
}

} // namespace livecut
