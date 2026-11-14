// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginEditor.h"

#include "Parameters.h"
#include "ui/AboutSheet.h"
#include "ui/Controls.h"
#include "ui/PadPage.h"
#include "ui/PhraseRuler.h"
#include "ui/RangeSlider.h"

#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace ids = livecut::ids;
namespace ui = livecut::ui;

namespace
{
// The Classic surface's grid. It is four bands, a margin apart and a margin from every edge: the header, the
// tape, a page of cards, and a strip of controls that every page shares. Cards sit in equal columns.
constexpr int margin = 20;         // around the editor, and between its bands
constexpr int gap = 12;            // between cards, and between the groups in a band
constexpr int sectionPadding = 12; // inside a card, at its sides
constexpr int rowPadding = 8;      // and above and below its rows, which carry space of their own
constexpr int rowHeight = 28;      // a card's title, a switch, a drop-down or a button
constexpr int sliderRowHeight = 40;
constexpr int rowGap = 4;
constexpr int noteHeight = 18;     // a line of status or advice
constexpr float cardRadius = 8.0f;

constexpr int headerHeight = 36, headerControlHeight = 30;
constexpr int wordmarkWidth = 96;
constexpr int tabPadding = 10, tabGap = 4;
constexpr int procedureWidth = 84; // each procedure's segment of the switch
constexpr int tapeHeight = 32;
constexpr int holdWidth = 126;
constexpr int stripHeight = sliderRowHeight + rowHeight + sliderRowHeight;
constexpr int pageTop = margin + headerHeight + margin + tapeHeight + margin;
constexpr int pageHeight = LivecutAudioProcessorEditor::baseHeight - pageTop - margin - stripHeight - margin;

// The card across the top of the Impact and Fracture pages: a row of controls over a line of advice.
constexpr int sharedCardHeight = 2 * rowPadding + sliderRowHeight + rowGap + noteHeight;

struct ProcedureInfo
{
    const char* name;
    const char* description;
};

constexpr std::array procedures {
    ProcedureInfo { "CutProc11", "Odd-length cuts, repeated, with a stutter to close each phrase" },
    ProcedureInfo { "WarpCut", "Straight slices and rolls that speed up or slow down" },
    ProcedureInfo { "SQPusher", "Quaver and semiquaver cuts, with a Squarepusher fill on each phrase's last bar" },
    ProcedureInfo { "Bounce", "Straight hits and triplet bounces, a ratchet roll that climbs to close each phrase, and the odd halftime bar" },
};

// One of `count` equal columns across `area`, `gap` apart.
juce::Rectangle<int> columnOf (juce::Rectangle<int> area, int index, int count)
{
    const auto width = (area.getWidth() - (count - 1) * gap) / count;
    return area.withX (area.getX() + index * (width + gap)).withWidth (width);
}

// What a card holds, inside its padding.
juce::Rectangle<int> inside (juce::Rectangle<int> card)
{
    return card.reduced (sectionPadding, rowPadding);
}

void fillCard (juce::Graphics& g, juce::Rectangle<int> card)
{
    g.setColour (ui::palette::well);
    g.fillRoundedRectangle (card.toFloat(), cardRadius);
}

// A card's title, on its first row.
void drawTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> card)
{
    g.setColour (ui::palette::ink);
    g.setFont (ui::uiFont (14.0f, ui::weight::semibold));
    g.drawText (text, inside (card).removeFromTop (rowHeight), juce::Justification::centredLeft);
}

// A line or two of status or advice: small and muted.
void makeNote (juce::Label& label, const char* id, const char* title, juce::Justification justification = juce::Justification::centredLeft)
{
    label.setComponentID (id);
    label.setTitle (title);
    label.setFont (ui::uiFont (12.0f));
    label.setColour (juce::Label::textColourId, ui::palette::inkMuted);
    label.setJustificationType (justification);
    label.setBorderSize ({});
    label.setMinimumHorizontalScale (1.0f);
}

// Stacks rows down a card, top to bottom.
struct Column
{
    juce::Rectangle<int> area;

    void place (juce::Component& component, int height)
    {
        component.setBounds (area.removeFromTop (height));
        area.removeFromTop (rowGap);
    }

    // Leaves a row for something painted there, such as the card's title.
    void skip (int height) { area.removeFromTop (height + rowGap); }
};

// The three cards across a page, each ready to stack rows inside its padding.
std::array<Column, 3> cardColumns (juce::Rectangle<int> area)
{
    return { Column { inside (columnOf (area, 0, 3)) },
             Column { inside (columnOf (area, 1, 3)) },
             Column { inside (columnOf (area, 2, 3)) } };
}

// One parameter's control on a page of cards, and the card it stacks in: -1 is the card across the page's top.
struct Row
{
    std::unique_ptr<juce::Component> control;
    int column, height;
};

// A drop-down for a choice, a switch for a boolean and a slider for anything else.
Row makeRow (juce::AudioProcessorValueTreeState& state, const char* id, const char* caption, const char* help,
             int column, const char* choiceCaption = "")
{
    auto* parameter = state.getParameter (id);
    jassert (parameter != nullptr);
    std::unique_ptr<juce::Component> control;
    auto height = rowHeight;

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
    {
        control = std::make_unique<ui::ChoiceRow> (*choice, choiceCaption);
    }
    else if (parameter->isBoolean())
    {
        control = std::make_unique<ui::PowerSwitch> (*parameter, caption);
    }
    else
    {
        control = std::make_unique<ui::SliderRow> (*parameter, caption);
        height = sliderRowHeight;
    }

    control->setTitle (parameter->getName (80));

    for (auto* child : control->getChildren())
    {
        child->setTitle (parameter->getName (80));

        if (auto* tooltip = dynamic_cast<juce::SettableTooltipClient*> (child))
            tooltip->setTooltip (help);
    }

    return { std::move (control), column, height };
}
} // namespace

namespace
{
// The Impact effects: the envelope they share across the top, and a card for each.
class ImpactPage final : public juce::Component
{
public:
    explicit ImpactPage (juce::AudioProcessorValueTreeState& state)
    {
        setComponentID ("impactPage");
        const auto add = [&] (const char* id, const char* caption, const char* tip, int column)
        {
            rows.push_back (makeRow (state, id, caption, tip, column));
            addAndMakeVisible (*rows.back().control);
        };
        add (ids::impactAttackMs, "Env attack", "Shared cut envelope attack, 0 to 25 ms. Restarts on each cut/repeat.", -1);
        add (ids::impactDecayMs, "Env decay", "Shared cut envelope decay to -60 dB, 5 to 1000 ms.", -1);
        add (ids::impactFilterEnabled, "Slice filter", "Enable the cut-aware stereo filter. Changes apply at the next planned block; FX gates ramp over 2 ms.", 0);
        add (ids::impactFilterType, "Filter type", "Low-pass, band-pass or high-pass filter.", 0);
        add (ids::impactFilterChance, "Chance", "Deterministic probability per cut, independent of cutter randomness.", 0);
        add (ids::impactFilterCutoff, "Cutoff", "Base cutoff in Hz; also limited to 45% of sample rate.", 0);
        add (ids::impactFilterQ, "Resonance", "Filter Q from 0.5 to 8; high values emphasize the cutoff.", 0);
        add (ids::impactFilterEnvOctaves, "Env depth", "Cut envelope modulation, -6 to +6 octaves.", 0);
        add (ids::impactFilterRepeatOctaves, "Repeat", "Cutoff offset from first to last repeat, -4 to +4 octaves. A single repeat starts at zero.", 0);
        add (ids::impactFilterMix, "Filter mix", "Filter amount; 0% is neutral after the gate ramp settles.", 0);
        add (ids::impactDriveEnabled, "Drive / fold", "Enable first-order ADAA waveshaping. No added buffered latency; active processing adds half-sample phase and high-frequency coloration.", 1);
        add (ids::impactDriveMode, "Drive mode", "Soft saturation or wavefolding. Enabled Drive at 0 dB is still nonlinear.", 1);
        add (ids::impactDriveChance, "Chance", "Deterministic probability per cut, independent of cutter randomness.", 1);
        add (ids::impactDriveDb, "Drive", "Input drive from 0 to 30 dB. Enabled 0 dB is not neutral; use power off or mix 0% for neutral output.", 1);
        add (ids::impactDriveEnvDb, "Env depth", "Drive offset from the shared cut envelope, -24 to +24 dB.", 1);
        add (ids::impactDriveRepeatDb, "Repeat", "Drive offset from first to last repeat, -24 to +24 dB.", 1);
        add (ids::impactDriveMix, "Drive mix", "Drive amount; 0% is neutral after the gate ramp settles.", 1);
        add (ids::impactTransientEnabled, "Transient", "Enable stereo-linked transient shaping. Changes apply at the next planned block.", 2);
        add (ids::impactTransientChance, "Chance", "Deterministic probability per cut, independent of cutter randomness.", 2);
        add (ids::impactTransientAttackDb, "Attack", "Transient gain, -18 to +18 dB. Attack and sustain both 0 dB are neutral.", 2);
        add (ids::impactTransientSustainDb, "Sustain", "Body gain, -18 to +18 dB. Attack and sustain both 0 dB are neutral.", 2);
        add (ids::impactTransientTimeMs, "Response", "Stereo-linked detector response, 10 to 200 ms.", 2);
        add (ids::impactTransientMix, "Transient mix", "Transient effect amount; 0% is neutral after the gate ramp settles.", 2);
        makeNote (help, "impactHelp", "Impact timing and signal path");
        help.setText ("Next planned block  |  2 ms FX gates  |  Filter > Drive > Transient", juce::dontSendNotification);
        addAndMakeVisible (help);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto shared = inside (area.removeFromTop (sharedCardHeight));
        area.removeFromTop (gap);
        auto columns = cardColumns (area);
        help.setBounds (shared.removeFromBottom (noteHeight));

        // Across the top the envelope's two sliders sit over the second and third cards, beside its name.
        std::size_t sharedColumn = 1;

        for (auto& row : rows)
        {
            if (row.column < 0)
                row.control->setBounds (columns[sharedColumn++].area.withY (shared.getY()).withHeight (row.height));
            else
                columns[static_cast<std::size_t> (row.column)].place (*row.control, row.height);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const auto shared = area.removeFromTop (sharedCardHeight);
        area.removeFromTop (gap);
        fillCard (g, shared);

        for (int column = 0; column < 3; ++column)
            fillCard (g, columnOf (area, column, 3));

        // The name shares its sliders' caption line.
        g.setColour (ui::palette::ink);
        g.setFont (ui::uiFont (14.0f, ui::weight::semibold));
        g.drawText ("Filter / drive envelope", inside (shared).removeFromTop (ui::SliderRow::captionHeight), juce::Justification::centredLeft);
    }

private:
    std::vector<Row> rows;
    juce::Label help;
};
// Resonator tuning and the prepared spectral suffix share one page.
class ResonancePage final : public juce::Component
{
public:
    explicit ResonancePage (LivecutAudioProcessor& processor) : owner (processor)
    {
        auto& state = owner.getParameterState();
        setComponentID ("resonancePage");
        const auto add = [&] (const char* id, const char* caption, const char* tip, int column)
        {
            rows.push_back (makeRow (state, id, caption, tip, column));
            addAndMakeVisible (*rows.back().control);
        };
        add (ids::resonanceEnabled, "Resonator", "Enable four tuned resonances. Controls apply at the next planned block; tuning changes crossfade over 50 ms.", 0);
        add (ids::resonanceRootHz, "Root", "Root frequency, 40 to 800 Hz. Four voices use semitone offsets from this frequency.", 0);
        add (ids::resonanceDecayMs, "Decay", "Pole decay time to -60 dB, 40 to 5000 ms. Excitation and source content affect the heard tail.", 0);
        add (ids::resonanceExcitationDb, "Excitation", "Resonator input gain, -12 to +12 dB. There is no hidden limiter.", 0);
        add (ids::resonanceMix, "Resonator mix", "Local resonator blend, with 2 ms gate smoothing. Zero is neutral after settling.", 0);
        add (ids::resonanceOrder, "Effect order", "Three fixed orders for Impact, Resonator and Frequency. Changes begin on a bar boundary with a 5 ms crossfade; Spectral always follows them.", 0);
        add (ids::resonanceInterval1, "Voice 1 interval", "Semitone offset from root, -12 to +24. Retuning crossfades fixed banks over 50 ms.", 1);
        add (ids::resonanceWeight1, "Voice 1 weight", "Relative voice weight. Weights normalize together; all four at zero explicitly disable resonator wet output.", 1);
        add (ids::resonanceInterval2, "Voice 2 interval", "Semitone offset from root, -12 to +24. Retuning crossfades fixed banks over 50 ms.", 1);
        add (ids::resonanceWeight2, "Voice 2 weight", "Relative voice weight. Weights normalize together; all four at zero explicitly disable resonator wet output.", 1);
        add (ids::resonanceInterval3, "Voice 3 interval", "Semitone offset from root, -12 to +24. Retuning crossfades fixed banks over 50 ms.", 1);
        add (ids::resonanceWeight3, "Voice 3 weight", "Relative voice weight. Weights normalize together; all four at zero explicitly disable resonator wet output.", 1);
        add (ids::resonanceInterval4, "Voice 4 interval", "Semitone offset from root, -12 to +24. Retuning crossfades fixed banks over 50 ms.", 1);
        add (ids::resonanceWeight4, "Voice 4 weight", "Relative voice weight. Weights normalize together; all four at zero explicitly disable resonator wet output.", 1);
        add (ids::resonanceSpectralEnabled, "Spectral engine", "Requested prepared configuration. Apply while stopped. Adds 8192 samples at 48 kHz; mix zero and host bypass retain this delay.", 2);
        add (ids::resonanceFreeze, "Freeze", "Hold magnitudes and continue phase evolution from the next eligible full analysis frame. This is spectral freeze, separate from Audio Hold.", 2);
        add (ids::resonanceSmearMs, "Smear", "Magnitude smoothing time constant, 0 to 2000 ms. Zero disables smear; it is not an amplitude decay guarantee.", 2);
        add (ids::resonanceSpectralMix, "Spectral mix", "Blend the pre-spectral processed signal with spectral output. While warming, the spectral contribution is explicitly silent.", 2);
        apply.setComponentID ("applySpectral");
        apply.setTitle ("Apply requested Mode and Spectral configuration while stopped");
        apply.setTooltip ("Stop the host, then apply the requested Live / Studio mode and Spectral engine together, or replace a faulted prepared stream.");
        apply.onClick = [this] { owner.applyRequestedMode(); refresh(); };
        addAndMakeVisible (apply);
        makeNote (status, "spectralStatus", "Spectral readiness and latency", juce::Justification::topLeft);
        makeNote (tuningStatus, "resonatorStatus", "Resonator validity and effect order", juce::Justification::topLeft);
        makeNote (help, "resonanceHelp", "Resonance signal path instructions");
        help.setText ("Crusher / Comb precede this rack. Spectral follows all three orders. Hold and Capture follow Spectral.", juce::dontSendNotification);

        for (auto* note : { &status, &tuningStatus, &help })
            addAndMakeVisible (note);
    }
    void refresh()
    {
        const auto& display = owner.getDisplayState();
        juce::String tuning;
        switch (display.resonatorState.load())
        {
            case livecut::engine::Resonator::State::ready:
                tuning = display.resonatorRetuning.load() ? "Retuning: 50 ms crossfade" : "Resonator tuning ready";
                if (display.resonatorPending.load()) tuning += "; latest tuning queued";
                break;
            case livecut::engine::Resonator::State::invalidWeights: tuning = "All weights zero: resonator wet is silent"; break;
            case livecut::engine::Resonator::State::invalidParameters: tuning = "Invalid tuning: resonator wet is silent"; break;
            case livecut::engine::Resonator::State::unsupportedRate: tuning = "Unsupported resonator sample rate"; break;
        }
        if (display.effectRouteTransition.load()) tuning += "\nEffect order crossfading";
        else if (display.effectRoute.load() != display.requestedEffectRoute.load()) tuning += "\nEffect order pending next bar";
        tuningStatus.setText (tuning, juce::dontSendNotification);
        const auto state = owner.modeState();
        const auto& parameters = owner.getParameterState();
        const auto requestedSpectral = parameters.getRawParameterValue (ids::resonanceSpectralEnabled)->load() > .5f;
        const auto requestedMode = parameters.getRawParameterValue (ids::mode)->load() > .5f
            ? livecut::ProcessingMode::studio : livecut::ProcessingMode::live;
        const auto fault = display.spectralFault.load() != livecut::spectral::Fault::none;
        const auto modeName = [] (livecut::ProcessingMode mode) { return mode == livecut::ProcessingMode::studio ? "Studio" : "Live"; };
        juce::String summary = juce::String ("Requested: ") + modeName (requestedMode) + (requestedSpectral ? " / Spectral on" : " / Spectral off");
        if (state == LivecutAudioProcessor::ModeState::unprepared)
            summary += "\nActive: not prepared";
        else if (state == LivecutAudioProcessor::ModeState::pendingHostChange)
            summary += "\nActive: host configuration changing";
        else if (state == LivecutAudioProcessor::ModeState::error)
            summary += "\nActive: configuration error";
        else
            summary += juce::String ("\nActive: ") + modeName (owner.activeMode()) + (owner.activeSpectral() ? " / Spectral on" : " / Spectral off");
        const auto configurationStatus = !fault && (state != LivecutAudioProcessor::ModeState::active || requestedMode != owner.activeMode())
            ? owner.modeStatusText() : owner.spectralStatusText();
        status.setText (summary + "\n" + configurationStatus, juce::dontSendNotification);
        const auto canApply = state != LivecutAudioProcessor::ModeState::unprepared
            && state != LivecutAudioProcessor::ModeState::pendingPlaying
            && state != LivecutAudioProcessor::ModeState::pendingHostChange
            && (requestedMode != owner.activeMode() || requestedSpectral != owner.activeSpectral()
                || state != LivecutAudioProcessor::ModeState::active || fault);
        apply.setEnabled (canApply);
        apply.setAlpha (canApply ? 1.0f : .5f);
    }
    void resized() override
    {
        auto columns = cardColumns (cards());

        for (auto& column : columns)
            column.skip (rowHeight); // its title

        for (auto& row : rows)
            columns[static_cast<std::size_t> (row.column)].place (*row.control, row.height);

        // Under the rows: what the resonator is doing, then the spectral engine's Apply button and its state.
        tuningStatus.setBounds (columns[0].area.withTrimmedTop (rowGap));
        columns[2].place (apply, rowHeight);
        status.setBounds (columns[2].area.withTrimmedTop (rowGap));
        help.setBounds (getLocalBounds().removeFromBottom (noteHeight));
    }

    void paint (juce::Graphics& g) override
    {
        const std::array titles { "Tuned resonator", "Four voices", "Spectral Freeze / Smear" };

        for (int column = 0; column < 3; ++column)
        {
            const auto card = columnOf (cards(), column, 3);
            fillCard (g, card);
            drawTitle (g, titles[static_cast<std::size_t> (column)], card);
        }
    }
private:
    // The cards stop short of the page's foot, which holds a line of advice.
    juce::Rectangle<int> cards() const { return getLocalBounds().withTrimmedBottom (noteHeight + rowGap); }

    LivecutAudioProcessor& owner;
    std::vector<Row> rows;
    juce::TextButton apply { "Apply while stopped" };
    juce::Label status, help, tuningStatus;
};
// A fixed three-column reader/FX page; it does not own audio or a second AD clock.
class FracturePage final : public juce::Component
{
public:
    explicit FracturePage (juce::AudioProcessorValueTreeState& state)
        : familyAttachment (*state.getParameter (ids::fractureProcedure), [this] (float value)
          {
              family = static_cast<livecut::engine::FractureProcedure> (juce::jlimit (0, 2, juce::roundToInt (value)));
              updateEnabled();
              if (onFamilyChange)
                  onFamilyChange();
          })
    {
        setComponentID ("fracturePage");
        const auto add = [&] (const char* id, const char* caption, const char* tip, int column)
        {
            rows.push_back (makeRow (state, id, caption, tip, column, column < 0 ? "Family" : ""));
            addAndMakeVisible (*rows.back().control);
        };
        add (ids::fractureProcedure, "Family", "Classic retains the original four procedures. GrainCut and TapeCut replace the wet source reader. Family changes apply at the next bar.", -1);
        add (ids::fractureActivity, "Activity", "Grain outer rhythm: 0%, 50%, 100% select 1, 2, 4 cuts per beat. Intermediate values choose adjacent densities deterministically.", 0);
        add (ids::fractureGrainSizeMs, "Size", "Grain lifetime, 10 to 160 ms. Complete source history is required; otherwise wet audio waits explicitly.", 0);
        add (ids::fractureGrainDensity, "Density", "Overlap density from 0.5 to 4. Low density intentionally leaves gaps. Fixed eight-voice storage.", 0);
        add (ids::fractureGrainPositionSpreadMs, "Position spread", "Source scatter within a fixed complete pool, 0 to 250 ms. Missing source is never replaced with Classic audio.", 0);
        add (ids::fractureGrainPitchSpreadCents, "Pitch spread", "Seeded grain pitch variation, 0 to 1200 cents around common Pitch. Bounded antialiasing resampling reduces aliasing; it is not alias-free.", 0);
        add (ids::fractureGrainPanSpread, "Pan spread", "Seeded per-grain pan variation around common Pan, clamped to the stereo range.", 0);
        add (ids::fractureTapeStartSpeed, "Start speed", "Tape speed from 0 to 1x. Speed also changes pitch and replaces the common Pitch controls. At 0x the reader is silent.", 1);
        add (ids::fractureTapeEndSpeed, "End speed", "Target tape speed from 0 to 1x. Both 0x endpoints produce silence; 1x to 1x retains unit-rate playback.", 1);
        add (ids::fractureTapeCurve, "Tape curve", "Linear or smooth speed trajectory. Normal forward playback keeps source position and speed continuous through tempo changes.", 1);
        add (ids::fractureTapeCycle, "Tape cycle", "Outer tape gesture: half, one, two or four quarter-note beats, shortened at bar/phrase end.", 1);
        add (ids::fractureTapeAlternate, "Alternate sweep", "Swap start/end speed on alternate outer cuts. This does not reverse source audio.", 1);
        add (ids::fractureFrequencyEnabled, "Ring / shift", "Enable the post-Impact frequency effect for any reader family. No fixed buffered latency; frequency shift has nonlinear phase and is not dry-phase-aligned.", 2);
        add (ids::fractureFrequencyMode, "Frequency mode", "Ring retains both sidebands and uses absolute Hz. Frequency Shift selects a signed sideband. Shifted content can fold at Nyquist; DC is outside the quadrature guarantee.", 2);
        add (ids::fractureFrequencyChance, "Chance", "Independent deterministic effect probability once per admitted outer cut, not per internal grain.", 2);
        add (ids::fractureFrequencyHz, "Rate", "Signed -2000 to +2000 Hz; Ring uses absolute rate, Frequency Shift preserves sign. Settled combined 0 Hz is dry.", 2);
        add (ids::fractureFrequencyEnvHz, "Env depth", "Hz offset driven by the existing Impact cut attack/decay clock. Edit AD timing on the Impact page; internal grains do not retrigger it.", 2);
        add (ids::fractureFrequencyRepeatHz, "Repeat", "Hz offset from first to last outer cut in the block. A single cut uses zero progression.", 2);
        add (ids::fractureFrequencyMix, "Frequency mix", "Frequency amount. Off or 0% is exact dry after a 2 ms transition. IIR phase coloration can affect mixed audio.", 2);
        makeNote (status, "fractureStatus", "Active reader and source status");
        makeNote (help, "fractureHelp", "Reader timing and shared envelope instructions");
        addAndMakeVisible (status);
        addAndMakeVisible (help);
        familyAttachment.sendInitialUpdate();
    }

    std::function<void()> onFamilyChange;
    livecut::engine::FractureProcedure requestedFamily() const { return family; }

    void refresh (const livecut::DisplayState& display)
    {
        const auto main = ui::PhraseRuler::Snapshot::of (display);
        if (main.status != livecut::DisplayState::Status::cutting)
        {
            status.setText (ui::PhraseRuler::describe (main), juce::dontSendNotification);
            return;
        }
        const auto active = display.fractureFamily.load();
        juce::String text;
        switch (display.fractureState.load())
        {
            case livecut::engine::FractureState::off: text = "Classic reader; Ring / Shift available"; break;
            case livecut::engine::FractureState::waiting: text = "Waiting for Grain history; wet silent"; break;
            case livecut::engine::FractureState::playing:
                text = active == livecut::engine::FractureProcedure::grainCut
                    ? "GrainCut playing | voices " + juce::String (display.fractureVoices.load())
                    : "TapeCut playing | speed " + juce::String (display.fractureTapeSpeed.load(), 2) + "x";
                break;
            case livecut::engine::FractureState::ended: text = "Cut ended; waiting for next cut"; break;
            case livecut::engine::FractureState::sourceLost: text = "Source lost; wet silent"; break;
            case livecut::engine::FractureState::transformBudgetExhausted: text = "Transform budget exhausted; wet silent"; break;
            case livecut::engine::FractureState::capacityError: text = "Voice capacity error; wet silent"; break;
            case livecut::engine::FractureState::contextUnavailable: text = "Source context unavailable; wet silent"; break;
        }
        if (active != family)
            text = "Pending next bar: " + familyName (family) + " | " + text;
        status.setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto shared = inside (area.removeFromTop (sharedCardHeight));
        area.removeFromTop (gap);
        auto columns = cardColumns (area);
        columns[0].skip (rowHeight); // GrainCut's title
        columns[1].skip (rowHeight); // TapeCut's

        // Across the top: the family in the first column, what the reader is doing beside it, and advice beneath.
        auto top = shared.removeFromTop (rowHeight);
        help.setBounds (shared.removeFromBottom (noteHeight));

        for (auto& row : rows)
        {
            if (row.column < 0)
                row.control->setBounds (top.removeFromLeft (columns[0].area.getWidth()));
            else
                columns[static_cast<std::size_t> (row.column)].place (*row.control, row.height);
        }

        status.setBounds (top.withLeft (columns[1].area.getX()));
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        fillCard (g, area.removeFromTop (sharedCardHeight));
        area.removeFromTop (gap);

        for (int column = 0; column < 3; ++column)
            fillCard (g, columnOf (area, column, 3));

        drawTitle (g, "GrainCut", columnOf (area, 0, 3));
        drawTitle (g, "TapeCut", columnOf (area, 1, 3));
    }

private:
    static juce::String familyName (livecut::engine::FractureProcedure value)
    {
        switch (value)
        {
            case livecut::engine::FractureProcedure::classic: return "Classic";
            case livecut::engine::FractureProcedure::grainCut: return "GrainCut";
            case livecut::engine::FractureProcedure::tapeCut: return "TapeCut";
        }
        jassertfalse;
        return {};
    }

    void updateEnabled()
    {
        for (auto& row : rows)
        {
            if (row.column == 0)
                row.control->setEnabled (family == livecut::engine::FractureProcedure::grainCut);
            if (row.column == 1)
                row.control->setEnabled (family == livecut::engine::FractureProcedure::tapeCut);
        }
        help.setText (family == livecut::engine::FractureProcedure::tapeCut
            ? "Tape speed replaces Pattern pitch. Family: next bar; other settings: next block. Frequency Env uses Impact AD."
            : "Family: next bar; other settings: next block. Frequency Env uses the cut AD timing on the Impact page.", juce::dontSendNotification);
    }

    std::vector<Row> rows;
    juce::Label status, help;
    livecut::engine::FractureProcedure family = livecut::engine::FractureProcedure::classic;
    juce::ParameterAttachment familyAttachment;
};
} // namespace

class LivecutAudioProcessorEditor::Panel final : public juce::Component
{
public:
    explicit Panel (juce::AudioProcessorValueTreeState& state, livecut::CaptureService& captureService,
                    LivecutAudioProcessor& processor)
        : procedureSwitch (choice (state, ids::procedure)),
          subdivision (choice (state, ids::subdivision), "Subdivision"),
          phraseBars (parameter (state, ids::minPhraseBars), parameter (state, ids::maxPhraseBars), "Bars"),
          seed (parameter (state, ids::seed), "Seed"),
          fade (parameter (state, ids::fade), "Fade"),
          duty (parameter (state, ids::duty), "Duty"),
          fillDuty (parameter (state, ids::fillDuty), "Fill duty"),
          amp (parameter (state, ids::minAmp), parameter (state, ids::maxAmp), "Amp"),
          pan (parameter (state, ids::minPan), parameter (state, ids::maxPan), "Pan"),
          pitch (parameter (state, ids::minPitch), parameter (state, ids::maxPitch), "Pitch"),
          repeats (parameter (state, ids::minRepeats), parameter (state, ids::maxRepeats), "Repeats"),
          stutter (parameter (state, ids::stutterChance), "Stutter"),
          stutterArea (parameter (state, ids::stutterArea), "Stutter area"),
          straight (parameter (state, ids::straightChance), "Straight"),
          regular (parameter (state, ids::regularChance), "Regular"),
          ritard (parameter (state, ids::ritardChance), "Ritard"),
          accel (parameter (state, ids::accel), "Accel"),
          activity (parameter (state, ids::activity), "Activity"),
          bounce (parameter (state, ids::bounceChance), "Bounce"),
          ratchet (parameter (state, ids::ratchetChance), "Ratchet"),
          climb (parameter (state, ids::climb), "Climb"),
          halftime (parameter (state, ids::halftimeChance), "Halftime"),
          crusher (parameter (state, ids::crusher), "Crusher"),
          bits (parameter (state, ids::minBits), parameter (state, ids::maxBits), "Bits"),
          rate (parameter (state, ids::minRate), parameter (state, ids::maxRate), "Rate"),
          comb (parameter (state, ids::comb), "Comb"),
          combType (choice (state, ids::combType), {}),
          combFeedback (parameter (state, ids::combFeedback), "Feedback"),
          delay (parameter (state, ids::minDelay), parameter (state, ids::maxDelay), "Delay"),
          wet (parameter (state, ids::wet), "Wet"),
          trim (parameter (state, ids::outputTrim), "Trim"),
          swing (parameter (state, ids::swing), "Swing"),
          accent (parameter (state, ids::accent), "Accent"),
          hold (parameter (state, ids::hold), "Audio Hold"),
          processingMode (choice (state, ids::mode), "Requested"),
          attackAlign (parameter (state, ids::attackAlign), "Attack align"),
          attackThreshold (parameter (state, ids::attackThreshold), "Attack threshold"),
          reverseChance (parameter (state, ids::reverseChance), "Reverse chance"),
          impactControls (state),
          fractureControls (state),
          resonanceControls (processor),
          captures (captureService),
          hostProcessor (processor),
          padPage (state),
          surfaceAttachment (parameter (state, ids::surface), [this] (float value)
          {
              showSurface (value > 0.5f ? livecut::Surface::classic : livecut::Surface::pad);
          })
    {
        for (auto* component : std::initializer_list<juce::Component*> {
                 &procedureSwitch, &ruler, &subdivision, &phraseBars, &seed, &fade, &duty, &fillDuty,
                 &amp, &pan, &pitch, &crusher, &bits, &rate, &comb, &combType, &combFeedback, &delay,
                 &wet, &trim, &captureButton, &saveButton, &captureStatus, &swing, &accent, &hold, &holdStatus,
                 &patternTab, &recutTab, &impactTab, &fractureTab, &resonanceTab })
            addAndMakeVisible (component);

        for (auto* component : std::initializer_list<juce::Component*> {
                 &processingMode, &attackAlign, &attackThreshold, &reverseChance,
                 &applyMode, &modeSummary, &modeStatus, &modeHelp, &transformStatus })
            addChildComponent (component);

        addChildComponent (impactControls);
        addChildComponent (fractureControls);
        addChildComponent (resonanceControls);
        addChildComponent (padPage);
        addAndMakeVisible (wordmark);
        wordmark.onClick = [this] { about.open(); };
        about.onClosed = [this] { wordmark.grabKeyboardFocus(); };
        addChildComponent (about);
        addAndMakeVisible (surfaceButton);
        surfaceButton.setComponentID ("surface");
        surfaceButton.setTitle ("Surface");
        surfaceButton.onClick = [this]
        {
            surfaceAttachment.setValueAsCompleteGesture (surface == livecut::Surface::pad ? 1.0f : 0.0f);
        };
        resonanceTab.setComponentID ("resonanceTab");
        resonanceTab.setTitle ("Resonance and spectral controls");
        resonanceTab.setTooltip ("Show tuned resonator, effect order and Spectral Freeze / Smear");
        resonanceTab.onClick = [this] { showPage (Page::resonance); };
        fractureTab.setComponentID ("fractureTab");
        fractureTab.setTitle ("Fracture reader and frequency controls");
        fractureTab.setTooltip ("Show Classic / GrainCut / TapeCut families and Ring / Frequency Shift");
        fractureTab.onClick = [this] { showPage (Page::fracture); };
        fractureControls.onFamilyChange = [this] { showProcedure (activeProcedure); };
        impactTab.setComponentID ("impactTab");
        impactTab.setTitle ("Impact effects controls");
        impactTab.setTooltip ("Show Slice Filter, Drive / Wavefold and Transient controls");
        impactTab.onClick = [this] { showPage (Page::impact); };

        patternTab.setComponentID ("patternTab");
        recutTab.setComponentID ("recutTab");
        patternTab.setTitle ("Pattern controls");
        recutTab.setTitle ("Recut and Studio controls");
        patternTab.setTooltip ("Show phrase, cut and procedure controls");
        recutTab.setTooltip ("Show processing mode, attack alignment and reverse controls");
        patternTab.onClick = [this] { showPage (Page::pattern); };
        recutTab.onClick = [this] { showPage (Page::recut); };

        processingMode.setTitle ("Requested processing mode");
        for (auto* child : processingMode.getChildren())
            if (auto* box = dynamic_cast<juce::ComboBox*> (child))
            {
                box->setTitle ("Requested processing mode");
                box->setTooltip ("Live is immediate. Studio adds 500 ms of monitoring delay. Apply changes while stopped.");
            }
        attackAlign.setTitle ("Attack align");
        for (auto* child : attackAlign.getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
                toggle->setTooltip ("Start eligible slices near detected attacks, linked across stereo. Without a qualifying complete aligned slice, both Attack Align and Reverse are ineligible.");
        attackThreshold.setTitle ("Attack threshold");
        reverseChance.setTitle ("Reverse chance");
        attackThreshold.setTooltip ("Minimum detected energy rise, 3 to 24 dB. Higher values select stronger attacks.");
        const juce::String reverseHelp = "Probability of reversing an eligible slice; 100% means every eligible slice. With Attack Align on, reverse requires a qualifying complete aligned slice. With Attack Align off, reverse can use a complete nominal slice.";
        reverseChance.setTooltip (reverseHelp);
        for (auto* child : reverseChance.getChildren())
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
                slider->setTooltip (reverseHelp);
        applyMode.setComponentID ("applyMode");
        applyMode.setTitle ("Apply requested Mode and Spectral configuration while stopped");
        applyMode.setTooltip ("Stop playback, then apply the requested Live / Studio mode and Spectral engine together, or replace a faulted prepared stream.");
        applyMode.onClick = [this] { hostProcessor.applyRequestedMode(); refreshRecut(); };
        makeNote (modeSummary, "modeSummary", "Active processing mode and latency");
        makeNote (modeStatus, "modeStatus", "Processing mode status", juce::Justification::topLeft);
        makeNote (modeHelp, "modeHelp", "Processing mode instructions", juce::Justification::bottomLeft);
        makeNote (transformStatus, "transformStatus", "Attack and reverse status", juce::Justification::topLeft);
        modeHelp.setText ("Studio adds 500 ms of delay so more slices can use attack alignment and reverse. Stop your host, choose a mode, then Apply.", juce::dontSendNotification);

        for (auto* component : std::initializer_list<juce::Component*> { &repeats, &stutter, &stutterArea })
            cutProc11Controls.addAndMakeVisible (component);

        for (auto* component : std::initializer_list<juce::Component*> { &straight, &regular, &ritard, &accel })
            warpCutControls.addAndMakeVisible (component);

        sqPusherControls.addAndMakeVisible (activity);

        for (auto* component : std::initializer_list<juce::Component*> { &bounce, &ratchet, &climb, &halftime })
            bounceControls.addAndMakeVisible (component);

        for (auto* controls : { &cutProc11Controls, &warpCutControls, &sqPusherControls, &bounceControls })
            addChildComponent (controls);

        makeNote (holdStatus, "holdStatus", "Audio Hold status");
        swing.setTooltip ("50% is straight; 75% delays each paired eighth. Changes apply at the next bar.");
        accent.setTooltip ("Deterministic amplitude variation, attenuation only, up to 25%");
        captureButton.setComponentID ("capture");
        saveButton.setComponentID ("saveCapture");
        makeNote (captureStatus, "captureStatus", "Capture status");
        wet.setTooltip ("Linear dry/wet mix, smoothed over 20 ms");
        trim.setTooltip ("Output gain from -24 to +24 dB, smoothed over 20 ms");
        captureButton.setTooltip ("Keep the last fully completed phrase, including Wet and Trim");
        saveButton.setTooltip ("Save the captured phrase as stereo 32-bit float WAV");
        captureButton.onClick = [this] { captures.requestCapture(); refreshCapture(); };
        saveButton.onClick = [this] { chooseSaveFile(); };
        refreshCapture();

        procedureSwitch.onChange = [this] (int index) { showProcedure (index); };

        // A switched-off effect's controls stay adjustable but read as inactive.
        const auto dim = [] (bool on, std::initializer_list<juce::Component*> controls)
        {
            for (auto* control : controls)
                control->setAlpha (on ? 1.0f : 0.5f);
        };
        crusher.onChange = [this, dim] (bool on) { dim (on, { &bits, &rate }); };
        comb.onChange = [this, dim] (bool on) { dim (on, { &combType, &combFeedback, &delay }); };
        dim (crusher.isOn(), { &bits, &rate });
        dim (comb.isOn(), { &combType, &combFeedback, &delay });
        showProcedure (static_cast<int> (std::lround (state.getRawParameterValue (ids::procedure)->load())));
        showPage (Page::pattern);
        surfaceAttachment.sendInitialUpdate();
    }

    // The Pad surface shows the ruler and the pad page; Classic shows the tabs
    // and their pages. The Classic controls stay instantiated either way.
    void showSurface (livecut::Surface next)
    {
        surface = next;
        const auto classic = next == livecut::Surface::classic;
        for (auto* component : std::initializer_list<juce::Component*> {
                 &procedureSwitch, &patternTab, &recutTab, &impactTab, &fractureTab, &resonanceTab,
                 &wet, &trim, &captureButton, &saveButton, &captureStatus, &swing, &accent, &hold, &holdStatus })
            component->setVisible (classic);
        padPage.setVisible (! classic);
        surfaceButton.setButtonText (classic ? "Pad" : "Classic");
        surfaceButton.setTooltip (classic ? "Play the pad" : "Show the Classic pages");

        // Before the panel is in the editor its controls can't find the look-and-feel that sizes them, and the
        // editor lays it out as soon as it is.
        if (getParentComponent() != nullptr)
            resized();

        showPage (selectedPage);
        refreshRuler (hostProcessor.getDisplayState());
    }

    void showProcedure (int index)
    {
        const auto pattern = surface == livecut::Surface::classic && selectedPage == Page::pattern;
        activeProcedure = juce::jlimit (0, static_cast<int> (procedures.size()) - 1, index);
        cutProc11Controls.setVisible (pattern && activeProcedure == 0);
        warpCutControls.setVisible (pattern && activeProcedure == 1);
        sqPusherControls.setVisible (pattern && activeProcedure == 2);
        bounceControls.setVisible (pattern && activeProcedure == 3);
        const auto family = fractureControls.requestedFamily();
        const auto classic = family == livecut::engine::FractureProcedure::classic;
        const auto tape = family == livecut::engine::FractureProcedure::tapeCut;
        procedureSwitch.setEnabled (classic);
        procedureSwitch.setAlpha (classic ? 1.0f : 0.5f);
        for (auto* controls : { &cutProc11Controls, &warpCutControls, &sqPusherControls, &bounceControls })
            controls->setEnabled (classic);
        pitch.setEnabled (! tape);
        pitch.setAlpha (classic && activeProcedure == static_cast<int> (livecut::engine::Procedure::bounce) ? 0.5f : 1.0f);
        pitch.setTitle (tape ? "Pitch disabled: TapeCut uses speed" : "Pitch");
        pitch.getSlider().setTitle (pitch.getTitle());
        pitch.getSlider().setTooltip (tape ? "TapeCut uses Start / End speed on the Fracture page instead of common Pitch." : "Common cut pitch; TapeCut uses its speed controls instead.");
        for (auto* child : pitch.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText().startsWith ("Pitch"))
                label->setText (tape ? "Pitch: use Tape" : "Pitch", juce::dontSendNotification);
        repaint();
    }

    enum class Page { pattern, recut, impact, fracture, resonance };

    void showPage (Page page)
    {
        selectedPage = page;
        const auto classic = surface == livecut::Surface::classic;
        for (auto* component : std::initializer_list<juce::Component*> {
                 &subdivision, &phraseBars, &seed, &fade, &duty, &fillDuty, &amp, &pan, &pitch })
            component->setVisible (classic && page == Page::pattern);
        for (auto* component : std::initializer_list<juce::Component*> {
                 &processingMode, &attackAlign, &attackThreshold, &reverseChance,
                 &applyMode, &modeSummary, &modeStatus, &modeHelp, &transformStatus })
            component->setVisible (classic && page == Page::recut);
        for (auto* component : std::initializer_list<juce::Component*> {
                 &crusher, &bits, &rate, &comb, &combType, &combFeedback, &delay })
            component->setVisible (classic && (page == Page::pattern || page == Page::recut));
        impactControls.setVisible (classic && page == Page::impact);
        fractureControls.setVisible (classic && page == Page::fracture);
        resonanceControls.setVisible (classic && page == Page::resonance);
        patternTab.setToggleState (page == Page::pattern, juce::dontSendNotification);
        recutTab.setToggleState (page == Page::recut, juce::dontSendNotification);
        impactTab.setToggleState (page == Page::impact, juce::dontSendNotification);
        fractureTab.setToggleState (page == Page::fracture, juce::dontSendNotification);
        resonanceTab.setToggleState (page == Page::resonance, juce::dontSendNotification);
        showProcedure (activeProcedure);
    }

    void refreshRecut()
    {
        const auto modeState = hostProcessor.modeState();
        const auto active = hostProcessor.activeMode();
        const auto requested = hostProcessor.getParameterState().getRawParameterValue (ids::mode)->load() > 0.5f
                                   ? livecut::ProcessingMode::studio : livecut::ProcessingMode::live;
        juce::String summary;
        if (modeState == LivecutAudioProcessor::ModeState::unprepared)
            summary = "Active: not prepared";
        else if (modeState == LivecutAudioProcessor::ModeState::error)
            summary = "Mode error; see status";
        else if (modeState == LivecutAudioProcessor::ModeState::pendingHostChange)
            summary = "Updating host latency...";
        else
        {
            summary = "Active: ";
            summary += active == livecut::ProcessingMode::studio ? "Studio" : "Live";
            const auto latency = hostProcessor.reportedLatencySamples();
            summary += " | " + juce::String (latency) + " samples";
        }
        modeSummary.setText (summary, juce::dontSendNotification);
        modeStatus.setText (hostProcessor.modeStatusText(), juce::dontSendNotification);
        transformStatus.setText (hostProcessor.transformStatusText(), juce::dontSendNotification);
        const auto canApply = modeState != LivecutAudioProcessor::ModeState::unprepared
                              && modeState != LivecutAudioProcessor::ModeState::pendingPlaying
                              && modeState != LivecutAudioProcessor::ModeState::pendingHostChange
                              && (requested != active || modeState != LivecutAudioProcessor::ModeState::active
                                  || (hostProcessor.getParameterState().getRawParameterValue (ids::resonanceSpectralEnabled)->load() > .5f) != hostProcessor.activeSpectral()
                                  || hostProcessor.getDisplayState().spectralFault.load() != livecut::spectral::Fault::none);
        applyMode.setEnabled (canApply);
        applyMode.setAlpha (canApply ? 1.0f : 0.5f);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::palette::backdrop);

        if (surface != livecut::Surface::classic || (selectedPage != Page::pattern && selectedPage != Page::recut))
            return;

        fillCard (g, crusherSection);
        fillCard (g, combSection);

        if (selectedPage == Page::recut)
        {
            fillCard (g, modeSection);
            fillCard (g, transformSection);
            drawTitle (g, "Processing mode", modeSection);
            drawTitle (g, "Attack & reverse", transformSection);
            return;
        }

        for (const auto& section : { phraseSection, cutsSection, procedureSection })
            fillCard (g, section);

        const auto& info = procedures[static_cast<std::size_t> (activeProcedure)];
        const auto family = fractureControls.requestedFamily();
        const auto classic = family == livecut::engine::FractureProcedure::classic;
        drawTitle (g, "Phrase", phraseSection);
        drawTitle (g, "Cuts", cutsSection);
        drawTitle (g, classic ? info.name : family == livecut::engine::FractureProcedure::grainCut ? "GrainCut override" : "TapeCut override", procedureSection);
        g.setColour (ui::palette::inkMuted);
        g.setFont (ui::uiFont (12.0f));
        g.drawFittedText (classic ? info.description : "Classic procedure controls are inactive. Edit the reader on the Fracture page.",
                          procedureNote, juce::Justification::topLeft, 3, 1.0f);
    }

    void resized() override
    {
        const auto classic = surface == livecut::Surface::classic;
        const juce::Rectangle<int> band (margin, margin, baseWidth - 2 * margin, headerHeight);
        wordmark.setBounds (band.withWidth (wordmarkWidth));
        about.setBounds (getLocalBounds());

        // The header's controls sit on its centre line. From the right on Classic: the surface switch, the
        // procedures, and a tab for each page, each as wide as its name.
        auto header = band.withSizeKeepingCentre (band.getWidth(), headerControlHeight);
        const auto fitted = [this] (juce::TextButton& button)
        {
            const auto font = getLookAndFeel().getTextButtonFont (button, headerControlHeight);
            return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, button.getButtonText()))) + 2 * tabPadding;
        };
        surfaceButton.setBounds (header.removeFromRight (classic ? fitted (surfaceButton) : wordmarkWidth));
        header.removeFromRight (gap);
        procedureSwitch.setBounds (header.removeFromRight (static_cast<int> (procedures.size()) * procedureWidth));
        header.removeFromRight (gap - tabGap);

        for (auto* tab : { &resonanceTab, &fractureTab, &impactTab, &recutTab, &patternTab })
        {
            header.removeFromRight (tabGap);
            tab->setBounds (header.removeFromRight (fitted (*tab)));
        }

        // On the pad the tape runs along the header, centred between the wordmark and the switch, which are the
        // same width. Classic gives it a band of its own.
        ruler.setBounds (classic ? band.translated (0, headerHeight + margin).withHeight (tapeHeight)
                                 : juce::Rectangle<int> (600, tapeHeight).withCentre (band.getCentre()));
        padPage.setBounds (band.withTop (band.getBottom() + margin).withBottom (baseHeight - margin));

        const juce::Rectangle<int> page (margin, pageTop, band.getWidth(), pageHeight);

        for (auto* controls : std::initializer_list<juce::Component*> { &impactControls, &fractureControls, &resonanceControls })
            controls->setBounds (page);

        // Pattern and Recut have their own cards, above Crusher and Comb, which the two pages share.
        auto upper = page;
        const auto lower = upper.removeFromBottom (2 * rowPadding + rowHeight + 2 * (rowGap + sliderRowHeight));
        upper.removeFromBottom (gap);
        phraseSection = columnOf (upper, 0, 3);
        cutsSection = columnOf (upper, 1, 3);
        procedureSection = columnOf (upper, 2, 3);
        modeSection = columnOf (upper, 0, 2);
        transformSection = columnOf (upper, 1, 2);
        crusherSection = columnOf (lower, 0, 2);
        combSection = columnOf (lower, 1, 2);

        Column phrase { inside (phraseSection) };
        phrase.skip (rowHeight);
        phrase.place (subdivision, rowHeight);

        for (auto* row : std::initializer_list<juce::Component*> { &phraseBars, &seed, &fade })
            phrase.place (*row, sliderRowHeight);

        Column cuts { inside (cutsSection) };
        cuts.skip (rowHeight);

        for (auto* row : std::initializer_list<juce::Component*> { &duty, &fillDuty, &amp, &pan, &pitch })
            cuts.place (*row, sliderRowHeight);

        // What the procedure does, in up to three lines under its name, then its own controls.
        Column procedure { inside (procedureSection) };
        procedure.skip (rowHeight);
        procedureNote = procedure.area.removeFromTop (3 * noteHeight - rowGap);
        procedure.area.removeFromTop (rowGap);

        for (auto* controls : { &cutProc11Controls, &warpCutControls, &sqPusherControls, &bounceControls })
        {
            controls->setBounds (procedure.area);
            Column column { controls->getLocalBounds() };

            for (auto* row : controls->getChildren())
                column.place (*row, sliderRowHeight);
        }

        // The mode to ask for and the button that applies it share a row; the advice sits at the foot of the card.
        Column mode { inside (modeSection) };
        mode.skip (rowHeight);
        auto request = mode.area.removeFromTop (rowHeight);
        applyMode.setBounds (request.removeFromRight (140));
        request.removeFromRight (gap);
        processingMode.setBounds (request);
        mode.area.removeFromTop (rowGap);
        mode.place (modeSummary, rowHeight);
        modeHelp.setBounds (mode.area.removeFromBottom (3 * noteHeight));
        modeStatus.setBounds (mode.area);

        Column transform { inside (transformSection) };
        transform.skip (rowHeight);
        transform.place (attackAlign, rowHeight);
        transform.place (attackThreshold, sliderRowHeight);
        transform.place (reverseChance, sliderRowHeight);
        transform.area.removeFromTop (rowGap);
        transformStatus.setBounds (transform.area);

        Column crush { inside (crusherSection) };
        crush.place (crusher, rowHeight);
        crush.place (bits, sliderRowHeight);
        crush.place (rate, sliderRowHeight);

        // Comb's switch and its type share the card's first row.
        Column combed { inside (combSection) };
        auto combHeader = combed.area.removeFromTop (rowHeight);
        combType.setBounds (combHeader.removeFromRight (160));
        comb.setBounds (combHeader.withTrimmedRight (gap));
        combed.area.removeFromTop (rowGap);
        combed.place (combFeedback, sliderRowHeight);
        combed.place (delay, sliderRowHeight);

        // The strip: Wet, Trim, Swing and Accent on the left; capture and hold on the right, each with its status.
        auto strip = page.withY (page.getBottom() + margin).withHeight (stripHeight);
        const auto upperRow = strip.removeFromTop (sliderRowHeight), lowerRow = strip.removeFromBottom (sliderRowHeight);
        const auto pair = [] (juce::Rectangle<int> row, juce::Component& first, juce::Component& second)
        {
            first.setBounds (columnOf (row, 0, 2));
            second.setBounds (columnOf (row, 1, 2));
        };
        pair (columnOf (upperRow, 0, 2), wet, trim);
        pair (columnOf (lowerRow, 0, 2), swing, accent);
        pair (columnOf (upperRow, 1, 2).withSizeKeepingCentre (columnOf (upperRow, 1, 2).getWidth(), rowHeight), captureButton, saveButton);
        captureStatus.setBounds (columnOf (strip, 1, 2));
        auto holdRow = columnOf (lowerRow, 1, 2);
        hold.setBounds (holdRow.removeFromLeft (holdWidth).withSizeKeepingCentre (holdWidth, rowHeight));
        holdStatus.setBounds (holdRow);
    }

    void refreshFracture() { fractureControls.refresh (hostProcessor.getDisplayState()); resonanceControls.refresh(); }

    // The pad page and the ruler's headline follow the display; Classic shows the plain status.
    void refreshRuler (const livecut::DisplayState& display)
    {
        const auto snapshot = ui::PhraseRuler::Snapshot::of (display);

        // The wordmark makes its cut again as each block starts on the tape.
        if (snapshot.status == livecut::DisplayState::Status::cutting && shownBlock.has_value() && ! juce::exactlyEqual (*shownBlock, snapshot.blockStart))
            wordmark.cut();

        shownBlock = snapshot.blockStart;

        if (surface == livecut::Surface::pad)
        {
            padPage.refresh (display);
            ruler.setHeadline (padPage.headline (display, ui::PhraseRuler::describe (snapshot)));
            ruler.setAccent (ui::programColour (static_cast<int> (padPage.playingProgram (display))));
        }
        else
        {
            ruler.setHeadline ({});
            ruler.setAccent (ui::palette::accent);
        }

        ruler.show (snapshot);
    }

    void refreshCapture()
    {
        captureButton.setEnabled (! captures.isBusy() && captures.rolling().hasLatest());
        saveButton.setEnabled (! captures.isBusy() && captures.captured() != nullptr);
        captureButton.setAlpha (captureButton.isEnabled() ? 1.0f : 0.5f);
        saveButton.setAlpha (saveButton.isEnabled() ? 1.0f : 0.5f);
        captureStatus.setText (captures.statusText(), juce::dontSendNotification);
    }

    void showHoldStatus (const juce::String& text) { holdStatus.setText (text, juce::dontSendNotification); }

    ui::ProcedureSwitch procedureSwitch;
    ui::PhraseRuler ruler;

private:
    void chooseSaveFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save captured phrase", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Livecut Redux phrase.wav"), "*.wav");
        const juce::Component::SafePointer<Panel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe] (const juce::FileChooser& dialog)
        {
            if (safe == nullptr)
                return;
            const auto target = dialog.getResult();
            if (target == juce::File())
                return;
            safe->captures.requestSave (target);
            safe->refreshCapture();
        });
    }

    static juce::RangedAudioParameter& parameter (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        auto* found = state.getParameter (id);
        jassert (found != nullptr);
        return *found;
    }

    static juce::AudioParameterChoice& choice (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        auto* found = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
        jassert (found != nullptr);
        return *found;
    }

    ui::ChoiceRow subdivision;
    ui::RangeSlider phraseBars;
    ui::SliderRow seed, fade, duty, fillDuty;
    ui::RangeSlider amp, pan, pitch, repeats;
    ui::SliderRow stutter, stutterArea, straight, regular, ritard, accel, activity;
    ui::SliderRow bounce, ratchet, climb, halftime;
    ui::PowerSwitch crusher;
    ui::RangeSlider bits, rate;
    ui::PowerSwitch comb;
    ui::ChoiceRow combType;
    ui::SliderRow combFeedback;
    ui::RangeSlider delay;
    ui::SliderRow wet, trim, swing, accent;
    ui::PowerSwitch hold;
    ui::ChoiceRow processingMode;
    ui::PowerSwitch attackAlign;
    ui::SliderRow attackThreshold, reverseChance;
    juce::TextButton patternTab { "Pattern" }, recutTab { "Recut" }, impactTab { "Impact" }, fractureTab { "Fracture" }, resonanceTab { "Resonance" }, applyMode { "Apply while stopped" };
    juce::Label modeSummary, modeStatus, modeHelp, transformStatus;
    juce::Label holdStatus;
    ImpactPage impactControls;
    FracturePage fractureControls;
    ResonancePage resonanceControls;
    livecut::CaptureService& captures;
    LivecutAudioProcessor& hostProcessor;
    juce::TextButton captureButton { "Capture last phrase" }, saveButton { "Save WAV..." };
    juce::Label captureStatus;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Component cutProc11Controls, warpCutControls, sqPusherControls, bounceControls;
    juce::Rectangle<int> phraseSection, cutsSection, procedureSection, procedureNote, crusherSection, combSection;
    juce::Rectangle<int> modeSection, transformSection;
    int activeProcedure = 0;
    Page selectedPage = Page::pattern;
    ui::PadPage padPage;
    ui::Wordmark wordmark;
    std::optional<double> shownBlock; // where the block the tape last showed began
    ui::AboutSheet about;
    juce::TextButton surfaceButton;
    juce::ParameterAttachment surfaceAttachment;
    livecut::Surface surface = livecut::Surface::pad;
};

LivecutAudioProcessorEditor::LivecutAudioProcessorEditor (LivecutAudioProcessor& p)
    : AudioProcessorEditor (p),
      livecutProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    panel = std::make_unique<Panel> (p.getParameterState(), p.getCaptureService(), p);
    addAndMakeVisible (*panel);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (baseWidth) / baseHeight);
    setSize (baseWidth, baseHeight);

    refreshDisplay();
    startTimerHz (30);
}

LivecutAudioProcessorEditor::~LivecutAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LivecutAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (livecut::ui::palette::backdrop);
}

void LivecutAudioProcessorEditor::resized()
{
    panel->setBounds (0, 0, baseWidth, baseHeight);
    panel->setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / baseWidth));
}

void LivecutAudioProcessorEditor::refreshDisplay()
{
    panel->refreshCapture();
    panel->refreshRecut();
    panel->refreshFracture();
    panel->showHoldStatus (livecutProcessor.holdStatusText());
    panel->refreshRuler (livecutProcessor.getDisplayState());
}

juce::String LivecutAudioProcessorEditor::statusText() const
{
    return panel->ruler.statusText();
}

void LivecutAudioProcessorEditor::timerCallback()
{
    refreshDisplay();
}
