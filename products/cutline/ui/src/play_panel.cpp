// SPDX-License-Identifier: AGPL-3.0-or-later
#include "play_panel.h"

#include "cl/pad_programs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string_view>

namespace cl::ui {

namespace {

constexpr float kTabHeight = 40.0f;
constexpr float kTabGap = 4.0f;
constexpr float kTabInset = 5.0f;
constexpr float kDockHeight = 46.0f;
constexpr float kDockGap = 10.0f;
// The coloured rails around the surface: lane names on the left, the Y zones on the right, the X zones below.
constexpr float kRailLeft = 72.0f;
constexpr float kRailRight = 56.0f;
constexpr float kRailTop = 12.0f;
constexpr float kRailBottom = 38.0f;
constexpr int kMaxPitchRows = 12;

float easeOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u;
}

float frac(float v) { return v - std::floor(v); }

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string percent(float v) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)));
    return buf;
}

// Which zone of a program's axis `v` is in, by the engine's own mapping; -1 for a continuous axis.
int activeZone(int program, bool isX, float v) {
    switch (static_cast<PadProgram>(program)) {
    case PadProgram::repeat:
        return isX ? (v < 0.25f ? 0 : (v < 0.5f ? 1 : (v < 0.75f ? 2 : 3))) : -1;
    case PadProgram::roll:
        if (isX)
            return v < 0.25f ? 0 : (v < 0.5f ? 1 : (v < 0.75f ? 2 : 3));
        return pads::rollLane(v) == 0 ? 0 : (pads::rollLane(v) == 1 ? 1 : 2);
    case PadProgram::density:
        return isX ? (v < 0.5f ? 0 : 1) : -1;
    case PadProgram::rate:
        if (isX)
            return pads::rateMultiplier(v) < 0.75 ? 0 : (pads::rateMultiplier(v) > 1.5 ? 2 : 1);
        return v < 0.30f ? 0 : (v > 0.70f ? 2 : 1);
    case PadProgram::drop:
        return isX ? static_cast<int>(pads::dropZone(v)) : -1;
    case PadProgram::fill:
        return -1;
    }
    return -1;
}

} // namespace

PlayPanel::PlayPanel(Context& context)
    : context_(context), holdBtn_(context, "Hold", [this] { toggleHold(); }),
      padMidiBtn_(context, "Pad MIDI", [this] { togglePadMidi(); }), ccChip_(context, ParamId::padCcMode, colour::cyan),
      auditionBtn_(context, "Audition",
                   [this] {
                       const bool on = context_.model.get(ParamId::audition) >= 0.5f;
                       context_.model.begin(ParamId::audition);
                       context_.model.set(ParamId::audition, on ? 0.0f : 1.0f);
                       context_.model.end(ParamId::audition);
                   }),
      dragMidBtn_(context, context.model.exportStatus() + " ↗", [this] { context_.model.dragMidiPhrase(); }) {
    setAcceptsKeystrokes(true);
    for (visage::Frame* child :
         std::initializer_list<visage::Frame*>{&holdBtn_, &padMidiBtn_, &ccChip_, &auditionBtn_, &dragMidBtn_})
        addChild(child);

    dragMidBtn_.setOnDragOut([this] { context_.model.dragMidiPhrase(); });

    program_ = playingProgram();
    programBefore_ = program_;
    const auto& v = context_.model.visuals();
    engage_ = v.padEngaged ? 1.0f : 0.0f;
    nowPpq_ = v.ppq;
    telemetryPpq_ = v.ppq;

    context_.live.push_back(this);
    tick();
}

PlayPanel::~PlayPanel() {
    endPadTouch();
    std::erase(context_.live, static_cast<Live*>(this));
}

int PlayPanel::playingProgram() const {
    const auto& v = context_.model.visuals();
    if (v.padEngaged || v.pendingPadProgram >= 0)
        return std::clamp(v.activePadProgram, 0, kPerfPadCount - 1);
    return std::clamp(static_cast<int>(context_.model.get(ParamId::padProgram) + 0.5f), 0, kPerfPadCount - 1);
}

bool PlayPanel::isDrum() const {
    return static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f) == static_cast<int>(EngineMode::drum);
}

bool PlayPanel::programDoesNothing(int program) const {
    return !isDrum() &&
           (program == static_cast<int>(PadProgram::density) || program == static_cast<int>(PadProgram::fill));
}

PlayPanel::Axis PlayPanel::xAxis(int program) const {
    const bool drum = isDrum();
    switch (static_cast<PadProgram>(program)) {
    case PadProgram::repeat:
        return {"WINDOW", {{0.0f, 0.25f, "1 BEAT"}, {0.25f, 0.5f, "1/2"}, {0.5f, 0.75f, "1/4"}, {0.75f, 1.0f, "1/8"}}};
    case PadProgram::roll:
        return {"RATE", {{0.0f, 0.25f, "1/8"}, {0.25f, 0.5f, "1/16"}, {0.5f, 0.75f, "1/16T"}, {0.75f, 1.0f, "1/32"}}};
    case PadProgram::density:
        if (!drum)
            return {"DENSITY DOES NOTHING IN MIDI SLICER MODE", {}, false};
        return {"DENSITY", {{0.0f, 0.5f, "CORE + GROOVE"}, {0.5f, 1.0f, "+ DETAIL"}}, true, true};
    case PadProgram::rate:
        return {"SPEED", {{0.0f, 1.0f / 3.0f, "0.5×"}, {1.0f / 3.0f, 2.0f / 3.0f, "1×"}, {2.0f / 3.0f, 1.0f, "2×"}}};
    case PadProgram::drop:
        if (!drum)
            return {"DROP", {{0.0f, 0.25f, "CUT"}, {0.25f, 1.0f, "PLAYS ON"}}};
        return {"DROP",
                {{0.0f, 0.25f, "CUT"}, {0.25f, 0.5f, "KICK"}, {0.5f, 0.75f, "KICK + SNARE"}, {0.75f, 1.0f, "NO KICK"}}};
    case PadProgram::fill:
        return {drum ? "WHILE HELD, EVERY BAR PLAYS ITS FILL" : "FILL DOES NOTHING IN MIDI SLICER MODE", {}, false};
    }
    return {"", {}, false};
}

PlayPanel::Axis PlayPanel::yAxis(int program) const {
    const bool drum = isDrum();
    switch (static_cast<PadProgram>(program)) {
    case PadProgram::repeat:
        return {"GATE", {}, true, true};
    case PadProgram::roll:
        if (!drum)
            return {"", {}, false};
        return {"LANE", {{0.0f, 1.0f / 3.0f, "KICK"}, {1.0f / 3.0f, 2.0f / 3.0f, "SNARE"}, {2.0f / 3.0f, 1.0f, "HAT"}}};
    case PadProgram::density:
        if (!drum)
            return {"", {}, false};
        return {"DYN", {}, true, true};
    case PadProgram::rate:
        if (!drum)
            return {"", {}, false};
        return {"KIT", {{0.0f, 0.3f, "K + S"}, {0.3f, 0.7f, "FULL"}, {0.7f, 1.0f, "NO K"}}};
    case PadProgram::drop:
    case PadProgram::fill:
        return {"", {}, false};
    }
    return {"", {}, false};
}

PlayPanel::Rect PlayPanel::tabRect(int index) const {
    const float gap = context_.px(kTabGap);
    const float th = context_.px(kTabHeight);
    const float tw = (width() - static_cast<float>(kPerfPadCount - 1) * gap) / static_cast<float>(kPerfPadCount);
    return {static_cast<float>(index) * (tw + gap), 0.0f, tw, th};
}

PlayPanel::Rect PlayPanel::sheetRect() const {
    const float top = context_.px(kTabHeight);
    const float bottom = height() - context_.px(kDockHeight + kDockGap);
    return {0.0f, top, width(), std::max(10.0f, bottom - top)};
}

PlayPanel::Rect PlayPanel::artRect() const {
    const Rect s = sheetRect();
    const float l = context_.px(kRailLeft);
    const float r = context_.px(kRailRight);
    const float t = context_.px(kRailTop);
    const float b = context_.px(kRailBottom);
    return {s.x + l, s.y + t, std::max(10.0f, s.w - l - r), std::max(10.0f, s.h - t - b)};
}

PlayPanel::Rect PlayPanel::xStripRect() const {
    const Rect art = artRect();
    return {art.x, art.y + art.h + context_.px(8.0f), art.w, context_.px(kRailBottom - 16.0f)};
}

PlayPanel::Rect PlayPanel::yStripRect() const {
    const Rect art = artRect();
    return {art.x + art.w + context_.px(8.0f), art.y, context_.px(kRailRight - 16.0f), art.h};
}

int PlayPanel::tabAt(float x, float y) const {
    if (y < 0.0f || y > context_.px(kTabHeight))
        return -1;
    for (int i = 0; i < kPerfPadCount; ++i)
        if (tabRect(i).contains(x, y))
            return i;
    return -1;
}

// Pad MIDI's keys are shown on the tabs and in the badge only where they work: drum mode with Pad MIDI on.
bool PlayPanel::showKeyHints() const { return isDrum() && padMidiOn(); }

std::string PlayPanel::axisValue(int prog, bool isX, float v) const {
    const Axis axis = isX ? xAxis(prog) : yAxis(prog);
    if (!axis.used)
        return "";
    if (axis.continuous)
        return std::string(axis.title) + " " + percent(v);
    const int zone = activeZone(prog, isX, v);
    if (zone < 0 || zone >= static_cast<int>(axis.zones.size()))
        return "";
    return axis.zones[static_cast<std::size_t>(zone)].label;
}

std::string PlayPanel::statusBadge(int prog, float x, float y, bool engaged, bool holdOn) const {
    const auto& v = context_.model.visuals();
    const std::string name = upper(kPerfPadLabels[static_cast<std::size_t>(prog)]);
    const int ccIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::padCcMode) + 0.5f), 0, 3);
    const std::string cc =
        ccIdx > 0 ? " · " + std::string(choices::padCcMode[static_cast<std::size_t>(ccIdx)]) : std::string();
    const std::string key =
        showKeyHints() ? " · " + std::string(kPerfPadHints[static_cast<std::size_t>(prog)]) : std::string();
    if (programDoesNothing(prog))
        return "MIDI SLICER · " + name + " DOES NOTHING HERE";
    const bool pending = v.pendingPadProgram >= 0 && !v.padEngaged;
    if (!engaged && !holdOn && !pending)
        return "TOUCH THE PAD · " + name + key + cc;
    std::string out = holdOn ? "HOLD" : (pending ? "ON THE NEXT 1/16" : "LIVE");
    out += " · " + name;
    std::string xs = axisValue(prog, true, x);
    if (xs.rfind(name + " ", 0) == 0)
        xs.erase(0, name.size() + 1); // "DENSITY 62%", not "DENSITY DENSITY 62%"
    const std::string ys = axisValue(prog, false, y);
    if (!xs.empty())
        out += " " + xs;
    if (!ys.empty())
        out += " · " + ys;
    return out + key + cc;
}

void PlayPanel::tick() {
    const auto& v = context_.model.visuals();

    // The surface's clock and its notes keep up even while PLAY is hidden, so showing it never shows stale marks.
    constexpr float dt = 1.0f / 60.0f;
    const double bpm = v.bpm > 1.0 ? v.bpm : 120.0;
    const auto sentCount = static_cast<std::size_t>(std::clamp(v.sentCount, 0, kMaxSentPerRefresh));
    std::array<SentNote, kMaxSentPerRefresh> sent{};
    std::copy_n(v.sent.begin(), sentCount, sent.begin());
    if (v.playing && v.ppq != telemetryPpq_) {
        // A loop or a jump: the marks move with the clock, so each keeps its age and still fades over one bar. Notes
        // drained in this tick that were sent before the jump are still on the old clock (ahead of the new position),
        // so they move with it too.
        if (v.ppq < nowPpq_ - 0.25 || v.ppq > nowPpq_ + 2.0) {
            const double jump = v.ppq - nowPpq_;
            history_.shift(jump);
            for (std::size_t i = 0; i < sentCount; ++i)
                if (sent[i].ppq > v.ppq + 0.25)
                    sent[i].ppq += jump;
        }
        nowPpq_ = v.ppq;
        telemetryPpq_ = v.ppq;
    } else {
        nowPpq_ += static_cast<double>(dt) * bpm / 60.0; // between updates, and after a stop: fade out over a bar
    }
    history_.ingest({sent.data(), sentCount});

    if (!isVisible()) {
        endPadTouch();
        return;
    }

    const int nextProg = playingProgram();
    if (nextProg != program_) {
        programBefore_ = change_ >= 0.5f ? program_ : programBefore_;
        program_ = nextProg;
        change_ = 0.0f;
        ring_ = 0.0f;
    }

    const unsigned col = colour::programArgb(program_);
    const bool holdOn = context_.model.get(ParamId::padHold) >= 0.5f;
    const bool audOn = context_.model.get(ParamId::audition) >= 0.5f;

    holdBtn_.setLabel(holdOn ? "Hold ON" : "Hold");
    holdBtn_.setAccent(holdOn ? col : colour::textDim);
    const bool midiOn = padMidiOn();
    padMidiBtn_.setLabel(midiOn ? "Pad MIDI ON" : "Pad MIDI");
    padMidiBtn_.setAccent(midiOn ? (isDrum() ? col : colour::textFaint) : colour::textDim);
    ccChip_.setAccent(colour::cyan);
    auditionBtn_.setLabel(audOn ? "Aud ON" : "Aud OFF");
    auditionBtn_.setAccent(audOn ? colour::emerald : colour::textFaint);
    dragMidBtn_.setAccent(col);

    const std::string exp = context_.model.exportStatus() + " ↗";
    if (exp != shownExport_) {
        shownExport_ = exp;
        dragMidBtn_.setLabel(shownExport_);
    }

    change_ = std::min(1.0f, change_ + dt / 0.16f);
    ring_ = std::min(1.0f, ring_ + dt / 0.34f);
    const float targetEng = (v.padEngaged || draggingPad_ || holdOn) ? 1.0f : 0.0f;
    engage_ += (targetEng - engage_) * std::min(1.0f, dt * 14.0f);
    float peak = 0.0f;
    for (const float f : v.laneFlash)
        peak = std::max(peak, f);
    impulse_ = std::max(peak, impulse_ * 0.86f);

    redraw();
}

void PlayPanel::drawTabs(visage::Canvas& canvas) {
    const float px = context_.scale;
    const auto& v = context_.model.visuals();
    const bool showKeys = showKeyHints();
    for (int i = 0; i < kPerfPadCount; ++i) {
        const Rect tr = tabRect(i);
        const bool inert = programDoesNothing(i); // dimmed: Density and Fill in MIDI Slicer mode
        const unsigned pCol =
            inert ? colour::lerpArgb(colour::programArgb(i), colour::raised, 0.62f) : colour::programArgb(i);
        const bool isPlaying = (i == program_);
        const bool isPending = (i == v.pendingPadProgram);
        const bool isMidiHeld = showKeys && ((v.midiPadMask >> static_cast<unsigned>(i)) & 1u) != 0u;
        const bool isHovered = (i == hoveredTab_);

        const float inset = isPlaying ? 0.0f : kTabInset * px;
        const float fy = tr.y + inset;
        const float fh = tr.h - inset;

        unsigned faceCol = colour::raised;
        unsigned nameCol = pCol;
        if (isPlaying) {
            faceCol = pCol;
            nameCol = colour::onColour;
        } else if (isPending) {
            const float lit = std::pow(1.0f - frac(static_cast<float>(nowPpq_)), 2.0f);
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.24f + 0.36f * lit);
            nameCol = colour::text;
        } else if (isMidiHeld) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.44f);
            nameCol = colour::text;
        } else if (isHovered) {
            faceCol = colour::lerpArgb(colour::raised, pCol, 0.18f);
            nameCol = colour::text;
        }
        if (inert && !isPlaying)
            nameCol = colour::withAlpha(colour::textFaint, 0.8f);

        canvas.setColor(faceCol);
        canvas.topRoundedRectangle(tr.x, fy, tr.w, fh + 2.0f * px, 9.0f * px);
        if (!isPlaying) {
            canvas.setColor(colour::withAlpha(pCol, isHovered || isPending ? 0.62f : 0.26f));
            canvas.roundedRectangleBorder(tr.x, fy, tr.w, fh, 9.0f * px, 1.0f * px);
        }

        const float textY = tr.y + kTabInset * px;
        const float textH = tr.h - kTabInset * px;
        drawLabel(canvas, context_, std::string(kPerfPadLabels[static_cast<std::size_t>(i)]), tr.x + 12.0f * px, textY,
                  tr.w - 46.0f * px, textH, nameCol, visage::Font::kLeft, 13.0f,
                  isPlaying ? Weight::semibold : Weight::medium);

        if (!showKeys)
            continue;
        const unsigned badgeCol = isPlaying    ? colour::withAlpha(colour::onColour, 0.68f)
                                  : isMidiHeld ? colour::text
                                               : colour::withAlpha(colour::textDim, 0.75f);
        drawLabel(canvas, context_, std::string(kPerfPadHints[static_cast<std::size_t>(i)]), tr.x + tr.w - 40.0f * px,
                  textY, 30.0f * px, textH, badgeCol, visage::Font::kRight, 9.5f, Weight::semibold);
    }
}

void PlayPanel::drawSurface(visage::Canvas& canvas, const Rect& art, unsigned accent, bool engaged) {
    const auto& v = context_.model.visuals();
    const bool drum = isDrum();
    const int prog = program_;
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);

    SurfaceScene scene;
    scene.x = art.x;
    scene.y = art.y;
    scene.w = art.w;
    scene.h = art.h;
    scene.scale = context_.scale;
    scene.accent = accent;
    scene.drum = drum;
    scene.history = &history_;
    scene.nowPpq = nowPpq_;
    if (v.playing) {
        const double ahead = std::max(0.0, nowPpq_ - v.ppq) * static_cast<double>(v.position.rate);
        scene.playhead = static_cast<float>(std::fmod(static_cast<double>(v.position.beatInBar) + ahead, 4.0));
    }

    const GrooveState& groove = context_.model.groove();
    const int sceneIdx = std::clamp(
        v.playing ? v.activeScene : static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(sceneIdx)];
    const int bar = std::clamp(v.position.barInPattern, 0, patternBars(pattern) - 1);
    ResolvedBar resolved;

    if (drum) {
        // What this bar resolves to, with the Density and Dynamics the engine reports (the Density pad's while held).
        resolveBar(pattern, groove.maps[static_cast<std::size_t>(sceneIdx)], bar,
                   {v.position.density, v.position.dynamics, v.position.fillLive}, resolved);
        scene.outline = &resolved;
        if (v.position.fillLive)
            scene.fillFromStep = fillWindowStart(pattern.fill);
        if (v.position.repeatStartStep >= 0 && v.padEngaged) {
            scene.repeatFromBeat = static_cast<float>(v.position.repeatStartStep % kStepsPerBar) * 0.25f;
            scene.repeatBeats = static_cast<float>(v.position.repeatBeats);
        }
        // What the program does to the lanes: dark where it keeps a lane quiet, lit where it plays a lane itself.
        const float on = 0.25f + 0.75f * engage_;
        for (int lane = 0; lane < kDrumLaneCount; ++lane) {
            const auto li = static_cast<std::size_t>(lane);
            switch (static_cast<PadProgram>(prog)) {
            case PadProgram::roll:
                scene.glow[li] = lane == pads::rollLane(padY) ? on : 0.0f;
                break;
            case PadProgram::rate:
                scene.veil[li] = pads::rateKeepsLane(padY, lane) ? 0.0f : engage_;
                break;
            case PadProgram::drop:
                scene.veil[li] = pads::dropKeepsLane(padX, lane) ? 0.0f : engage_;
                break;
            default:
                break;
            }
        }
    } else {
        // MIDI Slicer: a row for each pitch that is held, in the phrase's bar, or still fading on the surface.
        pitchRows_.clear();
        const auto addPitch = [this](int note) {
            if (std::find(pitchRows_.begin(), pitchRows_.end(), note) == pitchRows_.end())
                pitchRows_.push_back(note);
        };
        for (int i = 0; i < v.heldNotes.count; ++i)
            addPitch(v.heldNotes.notes[static_cast<std::size_t>(i)]);
        const double barStart = static_cast<double>(bar) * 4.0;
        for (int i = 0; i < v.slicerPlan.noteCount; ++i) {
            const SlicerNote& n = v.slicerPlan.notes[static_cast<std::size_t>(i)];
            if (n.beat >= barStart && n.beat < barStart + 4.0)
                addPitch(n.note);
        }
        history_.forEachLive(nowPpq_, [&](const SentNote& m, float) { addPitch(m.note); });
        std::sort(pitchRows_.begin(), pitchRows_.end(), std::greater<>());
        if (static_cast<int>(pitchRows_.size()) > kMaxPitchRows)
            pitchRows_.resize(static_cast<std::size_t>(kMaxPitchRows));
        scene.pitches = pitchRows_;
        scene.plan = &v.slicerPlan;
        scene.planBarStart = barStart;
    }
    drawPlaySurface(canvas, scene);

    // An empty surface says why.
    if (!drum && pitchRows_.empty()) {
        drawLabel(canvas, context_, "MIDI SLICER · hold a chord to slice it", art.x, art.y, art.w, art.h,
                  colour::withAlpha(colour::cyan, 0.7f), visage::Font::kCenter, 12.0f, Weight::medium);
    }
    (void)engaged;
}

void PlayPanel::drawLegend(visage::Canvas& canvas, const Rect& art, unsigned sheetCol) {
    const float px = context_.scale;
    const Rect sheet = sheetRect();
    const float lx = sheet.x + 8.0f * px;
    const float lw = art.x - lx - 14.0f * px;
    if (isDrum()) {
        const float rowH = art.h / static_cast<float>(kDrumLaneCount);
        const auto noteMap = static_cast<NoteMap>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::noteMap) + 0.5f), 0, kNoteMapCount - 1));
        const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
        const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
        for (int row = 0; row < kDrumLaneCount; ++row) {
            const int lane = kSurfaceLaneOrder[static_cast<std::size_t>(row)];
            const float ry = art.y + static_cast<float>(row) * rowH;
            bool quiet = false;
            if (engage_ > 0.5f && program_ == static_cast<int>(PadProgram::drop))
                quiet = !pads::dropKeepsLane(padX, lane);
            if (engage_ > 0.5f && program_ == static_cast<int>(PadProgram::rate))
                quiet = !pads::rateKeepsLane(padY, lane);
            drawLabel(canvas, context_, std::string(kitLaneName(noteMap, lane)), lx, ry, lw, rowH,
                      colour::withAlpha(colour::onColour, quiet ? 0.38f : 0.86f), visage::Font::kRight, 9.0f,
                      Weight::semibold);
            // The lane's colour, set in a dark socket so it reads on any sheet colour.
            const float chipH = std::min(rowH * 0.46f, 18.0f * px);
            const float cy = ry + (rowH - chipH) * 0.5f;
            canvas.setColor(colour::onColour);
            canvas.roundedRectangle(art.x - 10.0f * px, cy - 1.5f * px, 7.0f * px, chipH + 3.0f * px, 3.0f * px);
            canvas.setColor(colour::withAlpha(colour::lane[static_cast<std::size_t>(lane)], quiet ? 0.35f : 1.0f));
            canvas.roundedRectangle(art.x - 8.5f * px, cy, 4.0f * px, chipH, 2.0f * px);
        }
    } else if (!pitchRows_.empty()) {
        const float rowH = art.h / static_cast<float>(pitchRows_.size());
        for (std::size_t row = 0; row < pitchRows_.size(); ++row) {
            const float ry = art.y + static_cast<float>(row) * rowH;
            drawLabel(canvas, context_, midiNoteLabel(pitchRows_[row]), lx, ry, lw, rowH,
                      colour::withAlpha(colour::onColour, 0.86f), visage::Font::kRight, 9.0f, Weight::semibold);
        }
    }
    (void)sheetCol;
}

void PlayPanel::drawStrip(visage::Canvas& canvas, const Rect& r, const Axis& axis, float value, bool vertical,
                          unsigned sheetCol, bool engaged) {
    const float px = context_.scale;
    const unsigned dark = colour::onColour;
    if (!axis.used) {
        // No axis here: say what the program does instead (a long message only fits the horizontal strip).
        if (!vertical && axis.title[0] != '\0') {
            canvas.setColor(colour::withAlpha(dark, engaged ? 0.82f : 0.16f));
            canvas.roundedRectangle(r.x, r.y, r.w, r.h, 6.0f * px);
            drawLabel(canvas, context_, axis.title, r.x, r.y, r.w, r.h,
                      engaged ? colour::lerpArgb(sheetCol, 0xffffffffu, 0.35f) : colour::withAlpha(dark, 0.82f),
                      visage::Font::kCenter, 10.0f, Weight::semibold);
        } else if (vertical) {
            canvas.setColor(colour::withAlpha(dark, 0.08f));
            canvas.roundedRectangle(r.x, r.y, r.w, r.h, 6.0f * px);
        }
        return;
    }
    const float gap = 3.0f * px;
    const int active = activeZone(program_, !vertical, value);
    // A continuous axis with zones (Density) keeps a line under them for the exact value.
    const float barT = 4.0f * px;
    const bool valueBar = axis.continuous && !axis.zones.empty() && !vertical;
    const float zoneH = valueBar ? r.h - barT - 3.0f * px : r.h;
    for (std::size_t i = 0; i < axis.zones.size(); ++i) {
        const Zone& z = axis.zones[i];
        const Rect zr = vertical ? Rect{r.x, r.y + (1.0f - z.to) * r.h + gap * 0.5f, r.w, (z.to - z.from) * r.h - gap}
                                 : Rect{r.x + z.from * r.w + gap * 0.5f, r.y, (z.to - z.from) * r.w - gap, zoneH};
        const bool on = static_cast<int>(i) == active;
        canvas.setColor(on ? colour::withAlpha(dark, engaged ? 0.86f : 0.52f) : colour::withAlpha(dark, 0.14f));
        canvas.roundedRectangle(zr.x, zr.y, zr.w, zr.h, 6.0f * px);
        drawLabel(canvas, context_, z.label, zr.x, zr.y, zr.w, zr.h, on ? sheetCol : colour::withAlpha(dark, 0.78f),
                  visage::Font::kCenter, vertical ? 8.5f : 9.5f, Weight::semibold);
    }
    if (valueBar) {
        canvas.setColor(colour::withAlpha(dark, 0.18f));
        canvas.roundedRectangle(r.x, r.y + r.h - barT, r.w, barT, 2.0f * px);
        canvas.setColor(colour::withAlpha(dark, engaged ? 0.9f : 0.6f));
        canvas.roundedRectangle(r.x, r.y + r.h - barT, std::max(barT, value * r.w), barT, 2.0f * px);
    } else if (axis.continuous) {
        // A meter: the whole strip, filled up to the value, with the value written in it.
        canvas.setColor(colour::withAlpha(dark, 0.14f));
        canvas.roundedRectangle(r.x, r.y, r.w, r.h, 6.0f * px);
        canvas.setColor(colour::withAlpha(dark, engaged ? 0.62f : 0.34f));
        if (vertical)
            canvas.roundedRectangle(r.x, r.y + (1.0f - value) * r.h, r.w, std::max(6.0f * px, value * r.h), 6.0f * px);
        else
            canvas.roundedRectangle(r.x, r.y, std::max(6.0f * px, value * r.w), r.h, 6.0f * px);
        const bool inFill = vertical ? value > 0.93f : false;
        drawLabel(canvas, context_, percent(value), r.x, r.y + 6.0f * px, r.w, 16.0f * px,
                  inFill ? sheetCol : colour::withAlpha(dark, 0.85f), visage::Font::kCenter, 9.0f, Weight::semibold);
    }
    // Where the puck is on this axis.
    const float m = 5.0f * px;
    canvas.setColor(dark);
    if (vertical) {
        const float my = r.y + (1.0f - value) * r.h;
        canvas.triangle(r.x - 1.0f * px, my, r.x - 1.0f * px - m, my - m, r.x - 1.0f * px - m, my + m);
    } else {
        const float mx = r.x + value * r.w;
        canvas.triangle(mx, r.y - 1.0f * px, mx - m, r.y - 1.0f * px - m, mx + m, r.y - 1.0f * px - m);
    }
}

void PlayPanel::drawPuck(visage::Canvas& canvas, const Rect& art, unsigned sheetCol, bool engaged) {
    const float px = context_.scale;
    const auto& v = context_.model.visuals();
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    const float mx = art.x + padX * art.w;
    const float my = art.y + (1.0f - padY) * art.h;

    canvas.setColor(colour::withAlpha(0xffffffffu, engaged ? 0.42f : 0.22f));
    canvas.rectangle(art.x, my - 0.5f * px, art.w, 1.0f * px);
    canvas.rectangle(mx - 0.5f * px, art.y, 1.0f * px, art.h);

    if (ring_ < 0.995f) {
        const float r = (12.0f + easeOut(ring_) * 38.0f) * px;
        canvas.setColor(colour::withAlpha(0xffffffffu, (1.0f - ring_) * 0.85f));
        canvas.ring(mx - r, my - r, 2.0f * r, 2.2f * px);
    }
    if (engaged) {
        const float auraR = (26.0f + 8.0f * impulse_) * px;
        canvas.setColor(colour::withAlpha(sheetCol, 0.28f));
        canvas.circle(mx - auraR, my - auraR, 2.0f * auraR);
        const float pr = 22.0f * px;
        canvas.setColor(colour::withAlpha(0xff000000u, 0.36f));
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 4.0f * px);
        canvas.setColor(0xffffffffu);
        canvas.ring(mx - pr, my - pr, 2.0f * pr, 2.0f * px);
    }
    const float swell = v.playing ? (0.18f * impulse_) : 0.0f;
    const float dotR = 10.5f * (1.0f + swell) * px;
    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.circle(mx - dotR, my - dotR + 2.5f * px, 2.0f * dotR);
    canvas.setColor(engaged ? sheetCol : 0xffffffffu);
    canvas.circle(mx - dotR, my - dotR, 2.0f * dotR);
    canvas.setColor(engaged ? 0xffffffffu : colour::onColour);
    canvas.ring(mx - dotR, my - dotR, 2.0f * dotR, 2.6f * px);
}

void PlayPanel::drawDock(visage::Canvas& canvas, unsigned accent, bool engaged, bool holdOn) {
    const float px = context_.scale;
    const float dockY = height() - context_.px(kDockHeight);
    const float dockH = context_.px(kDockHeight);
    drawPanel(canvas, context_, 0.0f, dockY, width(), dockH, colour::panel, metric::radius);

    // The badge: what the pad is doing, right-aligned in the dock.
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    const std::string status = statusBadge(program_, padX, padY, engaged, holdOn);
    const float hudW = 352.0f * px;
    const float hudH = 28.0f * px;
    const float hudX = width() - hudW - 10.0f * px;
    const float hudY = dockY + (dockH - hudH) * 0.5f;
    canvas.setColor(0xe60a0d13u);
    canvas.roundedRectangle(hudX, hudY, hudW, hudH, 6.0f * px);
    canvas.setColor(colour::withAlpha(accent, engaged ? 0.92f : 0.40f));
    canvas.roundedRectangleBorder(hudX, hudY, hudW, hudH, 6.0f * px, 1.2f * px);
    const float sDot = 7.0f * px;
    canvas.setColor(engaged ? accent : colour::textDim);
    canvas.circle(hudX + 10.0f * px, hudY + (hudH - sDot) * 0.5f, sDot);
    drawLabel(canvas, context_, status, hudX + 24.0f * px, hudY, hudW - 32.0f * px, hudH, colour::text,
              visage::Font::kLeft, 10.0f, Weight::semibold);

    // The hint fills the dock between the controls (which end at 556 design pixels) and the badge.
    const float hintX = context_.px(566.0f);
    const float hintW = hudX - hintX - 10.0f * px;
    if (hintW > 120.0f * px)
        drawLabel(canvas, context_, "Double-click the pad to Hold · Shift for fine drag", hintX, dockY, hintW, dockH,
                  colour::textFaint, visage::Font::kCenter, 9.5f, Weight::regular);
}

void PlayPanel::draw(visage::Canvas& canvas) {
    const float px = context_.scale;
    const auto& v = context_.model.visuals();
    const bool holdOn = context_.model.get(ParamId::padHold) >= 0.5f;
    const bool engaged = v.padEngaged || draggingPad_ || holdOn;
    const unsigned sheetCol =
        colour::lerpArgb(colour::programArgb(programBefore_), colour::programArgb(program_), easeOut(change_));
    const unsigned accent = programDoesNothing(program_) ? colour::lerpArgb(sheetCol, colour::raised, 0.55f) : sheetCol;

    // 1. The program tabs, Repeat to Fill, with their keys (C3 to F3) while Pad MIDI is on in drum mode.
    drawTabs(canvas);

    // 2. The coloured sheet, fused to the playing program's tab.
    const Rect sheet = sheetRect();
    canvas.setColor(accent);
    canvas.bottomRoundedRectangle(sheet.x, sheet.y, sheet.w, sheet.h, 12.0f * px);

    // 3. The surface: what was sent, over the bar it resolves to, with the program's lanes and windows.
    const Rect art = artRect();
    canvas.saveState();
    canvas.trimClampBounds(art.x, art.y, art.w, art.h);
    drawSurface(canvas, art, accent, engaged);
    drawPuck(canvas, art, accent, engaged);
    canvas.restoreState();
    canvas.setColor(colour::withAlpha(0xff000000u, 0.48f));
    canvas.roundedRectangleBorder(art.x, art.y, art.w, art.h, 6.0f * px, 2.0f * px);

    // 4. The rails: lane names on the left, the program's zones below (X) and on the right (Y).
    drawLegend(canvas, art, accent);
    const float padX = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
    const float padY = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    const Axis xa = xAxis(program_);
    const Axis ya = yAxis(program_);
    drawStrip(canvas, xStripRect(), xa, padX, false, accent, engaged);
    drawStrip(canvas, yStripRect(), ya, padY, true, accent, engaged);
    // The axis names, in the corners the strips leave.
    const Rect xs = xStripRect();
    const Rect ys = yStripRect();
    if (xa.used)
        drawLabel(canvas, context_, xa.title, sheet.x + 8.0f * px, xs.y, art.x - sheet.x - 22.0f * px, xs.h,
                  colour::withAlpha(colour::onColour, 0.7f), visage::Font::kRight, 8.5f, Weight::semibold);
    if (ya.used)
        drawLabel(canvas, context_, ya.title, ys.x - 4.0f * px, xs.y, ys.w + 8.0f * px, xs.h,
                  colour::withAlpha(colour::onColour, 0.7f), visage::Font::kCenter, 8.5f, Weight::semibold);

    // 5. The dock.
    drawDock(canvas, accent, engaged, holdOn);
}

void PlayPanel::resized() {
    const auto px = [this](float v) { return context_.px(v); };
    const float dockY = height() - px(kDockHeight);
    const float bh = px(28.0f);
    const float cy = dockY + (px(kDockHeight) - bh) * 0.5f;

    // Left to right: [Hold] [Pad MIDI] [XY -> CC] [Audition] [Drag .mid]. The kit map and the slicer style live in
    // GRID; Scene Quantize too: it governs scene switches only, and the pads always land on the next 1/16.
    float x = px(12.0f);
    holdBtn_.setBounds(x, cy, px(76.0f), bh);
    x += px(76.0f + 8.0f);
    padMidiBtn_.setBounds(x, cy, px(96.0f), bh);
    x += px(96.0f + 8.0f);
    ccChip_.setBounds(x, cy, px(110.0f), bh);
    x += px(110.0f + 8.0f);
    auditionBtn_.setBounds(x, cy, px(80.0f), bh);
    x += px(80.0f + 8.0f);
    dragMidBtn_.setBounds(x, cy, px(150.0f), bh);
}

void PlayPanel::updatePointer(float x, float y, bool shiftDown) {
    const Rect art = artRect();
    if (art.w <= 1.0f || art.h <= 1.0f)
        return;
    if (shiftDown && !fineDragging_) {
        dragOriginX_ = x;
        dragOriginY_ = y;
        dragStartX_ = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
        dragStartY_ = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
    }
    fineDragging_ = shiftDown;

    float nx = (x - art.x) / art.w;
    float ny = (art.y + art.h - y) / art.h;
    if (fineDragging_) {
        nx = dragStartX_ + (x - dragOriginX_) / art.w * 0.1f;
        ny = dragStartY_ + (dragOriginY_ - y) / art.h * 0.1f;
    }
    context_.model.set(ParamId::padX, std::clamp(nx, 0.0f, 1.0f));
    context_.model.set(ParamId::padY, std::clamp(ny, 0.0f, 1.0f));
    redraw();
}

void PlayPanel::endPadTouch() {
    if (touching_) {
        touching_ = false;
        context_.model.set(ParamId::padTouch, 0.0f);
        context_.model.end(ParamId::padTouch);
    }
    if (draggingPad_) {
        draggingPad_ = false;
        context_.model.end(ParamId::padX);
        context_.model.end(ParamId::padY);
        redraw();
    }
}

void PlayPanel::toggleHold() {
    const bool next = context_.model.get(ParamId::padHold) < 0.5f;
    context_.model.begin(ParamId::padHold);
    context_.model.set(ParamId::padHold, next ? 1.0f : 0.0f);
    context_.model.end(ParamId::padHold);
    if (next)
        ring_ = 0.0f;
    redraw();
}

// Pad MIDI: while it is on, C3 to F3 on the MIDI input play the six programs in drum mode. It is off by default.
bool PlayPanel::padMidiOn() const { return context_.model.get(ParamId::padMidi) >= 0.5f; }

void PlayPanel::togglePadMidi() {
    context_.model.begin(ParamId::padMidi);
    context_.model.set(ParamId::padMidi, padMidiOn() ? 0.0f : 1.0f);
    context_.model.end(ParamId::padMidi);
    redraw();
}

void PlayPanel::selectProgram(int index) {
    const int clamped = std::clamp(index, 0, kPerfPadCount - 1);
    context_.model.begin(ParamId::padProgram);
    context_.model.set(ParamId::padProgram, static_cast<float>(clamped));
    context_.model.end(ParamId::padProgram);
    redraw();
}

void PlayPanel::mouseMove(const visage::MouseEvent& e) {
    const int idx = tabAt(e.position.x, e.position.y);
    if (idx != hoveredTab_) {
        hoveredTab_ = idx;
        redraw();
    }
}

void PlayPanel::mouseExit(const visage::MouseEvent&) {
    if (hoveredTab_ != -1) {
        hoveredTab_ = -1;
        redraw();
    }
}

void PlayPanel::mouseDown(const visage::MouseEvent& e) {
    const int tab = tabAt(e.position.x, e.position.y);
    if (tab >= 0) {
        selectProgram(tab);
        if (e.shouldTriggerPopup())
            toggleHold();
        return;
    }
    if (e.shouldTriggerPopup())
        return;

    if (artRect().contains(e.position.x, e.position.y)) {
        if (e.repeatClickCount() >= 2) {
            toggleHold();
            return;
        }
        endPadTouch();
        draggingPad_ = true;
        fineDragging_ = e.isShiftDown();
        dragOriginX_ = e.position.x;
        dragOriginY_ = e.position.y;
        dragStartX_ = std::clamp(context_.model.get(ParamId::padX), 0.0f, 1.0f);
        dragStartY_ = std::clamp(context_.model.get(ParamId::padY), 0.0f, 1.0f);
        ring_ = 0.0f;

        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        updatePointer(e.position.x, e.position.y, e.isShiftDown());

        touching_ = true;
        context_.model.begin(ParamId::padTouch);
        context_.model.set(ParamId::padTouch, 1.0f);
        redraw();
    }
}

void PlayPanel::mouseDrag(const visage::MouseEvent& e) {
    if (draggingPad_)
        updatePointer(e.position.x, e.position.y, e.isShiftDown());
}

void PlayPanel::mouseUp(const visage::MouseEvent& e) {
    if (!draggingPad_)
        return;
    updatePointer(e.position.x, e.position.y, fineDragging_);
    endPadTouch();
}

bool PlayPanel::keyPress(const visage::KeyEvent& key) {
    const float step = key.isShiftDown() ? 0.005f : key.isMainModifier() ? 0.10f : 0.02f;
    const auto nudge = [&](ParamId id, float delta) {
        const float next = std::clamp(context_.model.get(id) + delta, 0.0f, 1.0f);
        context_.model.begin(id);
        context_.model.set(id, next);
        context_.model.end(id);
        redraw();
    };
    if (key.keyCode() == visage::KeyCode::Left) {
        nudge(ParamId::padX, -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Right) {
        nudge(ParamId::padX, step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Down) {
        nudge(ParamId::padY, -step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Up) {
        nudge(ParamId::padY, step);
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Space) {
        toggleHold();
        return true;
    }
    if (key.keyCode() == visage::KeyCode::Home) {
        context_.model.begin(ParamId::padX);
        context_.model.begin(ParamId::padY);
        context_.model.set(ParamId::padX, 0.5f);
        context_.model.set(ParamId::padY, 0.5f);
        context_.model.end(ParamId::padX);
        context_.model.end(ParamId::padY);
        redraw();
        return true;
    }
    return false;
}

} // namespace cl::ui
