// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/studio_voices.h"

#include "lg/song.h"
#include "lg/voice_presets.h"

#include <dt/engine.h>
#include <dt/preset.h>
#include <ln/engine.h>
#include <ln/preset.h>
#include <sb/engine.h>
#include <sb/preset.h>
#include <tg/engine.h>
#include <tg/preset.h>

#include <hba/dsp/limiter.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/random.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lg {

namespace {

constexpr int kOversampling = 2; // the lowest factor every instrument accepts
constexpr int kVoices = tg::kVoiceCount;
constexpr int kMaxHeld = 16;
// Torus Glide plays one root note and spreads its five voices over the scene matrix. While no key is held its root
// glides to C2 + Root Key, so with Root Key C and the root held at C2 a release tail never bends: every pitch comes
// from the matrix instead.
constexpr int kRoot = 36;
constexpr float kQuiet = 1e-7f; // a source this quiet for half a second sleeps until its next note

// Each voice's level, matched by RMS to the sketch synth playing the same notes (VoicesTests "level" keeps them within
// 3 dB). The clap sits 3 dB above its RMS match: its room tail spreads the hit, so at equal RMS it lands 6 dB softer.
constexpr std::array<float, kRoleCount> kRoleTrim{0.316f, 0.238f, 0.160f, 0.0709f}; // chords, bass, melody, arp
constexpr std::array<float, 3> kDrumTrim{0.584f, 0.193f, 0.0489f};                  // kick, hat, clap

// A studio instrument's output on its way to the mix: scratch buffers, a delay that lines its latency up with the
// slowest instrument's, and an idle switch so a silent part costs nothing.
struct Source {
    std::vector<float> left, right, lineL, lineR;
    int delay = 0;
    int linePos = 0;
    int quiet = 0;
    int sleepAfter = 24000;
    bool awake = false;
    float trim = 1.0f;

    void prepare(int maxBlock, int delaySamples, int sleepSamples, float gain) {
        left.assign(static_cast<std::size_t>(maxBlock), 0.0f);
        right.assign(left.size(), 0.0f);
        delay = std::max(0, delaySamples);
        lineL.assign(static_cast<std::size_t>(std::max(1, delay)), 0.0f);
        lineR.assign(lineL.size(), 0.0f);
        sleepAfter = sleepSamples;
        trim = gain;
        reset();
    }

    void reset() noexcept LG_NONBLOCKING {
        std::fill(lineL.begin(), lineL.end(), 0.0f);
        std::fill(lineR.begin(), lineR.end(), 0.0f);
        linePos = 0;
        quiet = 0;
        awake = false;
    }

    void wake() noexcept LG_NONBLOCKING {
        awake = true;
        quiet = 0;
    }

    // After the instrument wrote `n` samples into left/right: delay them, add them to the mix, and go to sleep once
    // the instrument has been silent long enough (never while it holds a note).
    void finish(float* mixL, float* mixR, int n, bool holding) noexcept LG_NONBLOCKING {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            float l = left[k], r = right[k];
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
            if (delay > 0) {
                const auto p = static_cast<std::size_t>(linePos);
                std::swap(l, lineL[p]);
                std::swap(r, lineR[p]);
                linePos = linePos + 1 == delay ? 0 : linePos + 1;
            }
            mixL[i] += l * trim;
            mixR[i] += r * trim;
        }
        quiet = peak < kQuiet && !holding ? quiet + n : 0;
        if (quiet >= sleepAfter)
            awake = false;
    }
};

struct Held {
    int pitch = 0;
    std::uint32_t order = 0;
};

// The notes a part is holding, newest last-in.
struct HeldNotes {
    std::array<Held, kMaxHeld> notes{};
    int count = 0;
    std::uint32_t clock = 0;

    void add(int pitch) noexcept LG_NONBLOCKING {
        remove(pitch);
        if (count == kMaxHeld)
            removeAt(oldest());
        notes[static_cast<std::size_t>(count++)] = {pitch, ++clock};
    }
    void remove(int pitch) noexcept LG_NONBLOCKING {
        for (int i = 0; i < count; ++i)
            if (notes[static_cast<std::size_t>(i)].pitch == pitch) {
                removeAt(i);
                return;
            }
    }
    void removeAt(int index) noexcept LG_NONBLOCKING {
        notes[static_cast<std::size_t>(index)] = notes[static_cast<std::size_t>(--count)];
    }
    int oldest() const noexcept LG_NONBLOCKING {
        int best = 0;
        for (int i = 1; i < count; ++i)
            if (notes[static_cast<std::size_t>(i)].order < notes[static_cast<std::size_t>(best)].order)
                best = i;
        return best;
    }
    int newestPitch() const noexcept LG_NONBLOCKING {
        int best = 0;
        for (int i = 1; i < count; ++i)
            if (notes[static_cast<std::size_t>(i)].order > notes[static_cast<std::size_t>(best)].order)
                best = i;
        return notes[static_cast<std::size_t>(best)].pitch;
    }
    // The held pitches, lowest first. Returns how many.
    int sorted(std::array<int, kMaxHeld>& out) const noexcept LG_NONBLOCKING {
        for (int i = 0; i < count; ++i) {
            const int p = notes[static_cast<std::size_t>(i)].pitch;
            int j = i;
            for (; j > 0 && out[static_cast<std::size_t>(j - 1)] > p; --j)
                out[static_cast<std::size_t>(j)] = out[static_cast<std::size_t>(j - 1)];
            out[static_cast<std::size_t>(j)] = p;
        }
        return count;
    }
};

// Keeps at most five of the held chord notes (lowest first): the bass and the top always stay; inner notes that
// double a pitch class go first, then the inner note crowding its neighbours most.
int chooseChordNotes(std::array<int, kMaxHeld>& notes, int count) noexcept LG_NONBLOCKING {
    const auto at = [&notes](int i) noexcept LG_NONBLOCKING { return notes[static_cast<std::size_t>(i)]; };
    while (count > kVoices) {
        int drop = -1;
        for (int i = 1; i < count - 1 && drop < 0; ++i)
            for (int j = 0; j < count; ++j)
                if (j != i && (at(i) - at(j)) % 12 == 0) {
                    drop = i;
                    break;
                }
        if (drop < 0) {
            int tightest = 1000;
            for (int i = 1; i < count - 1; ++i) {
                const int gap = std::min(at(i) - at(i - 1), at(i + 1) - at(i));
                if (gap < tightest) {
                    tightest = gap;
                    drop = i;
                }
            }
        }
        for (int i = drop; i < count - 1; ++i)
            notes[static_cast<std::size_t>(i)] = at(i + 1);
        --count;
    }
    return count;
}

// A Torus Glide instance whose pitches all come from its scene matrix (every scene the same, so nothing morphs).
struct GlidePart {
    bool chordal = true; // chords: the held notes spread over the voices. Otherwise a lead: the newest note in unison.
    tg::Engine engine;
    tg::Params params = tg::Params::defaults();
    tg::SceneMatrix matrix{};
    std::array<double, kVoices> pitch{};
    HeldNotes held;
    float velocity = 0.8f;
    bool gate = false;
    bool dirty = false;
    Source out;

    void noteOn(int p, float v) noexcept LG_NONBLOCKING {
        held.add(p);
        velocity = std::clamp(v, 0.05f, 1.0f);
        dirty = true;
        out.wake();
    }
    void noteOff(int p) noexcept LG_NONBLOCKING {
        held.remove(p);
        dirty = true;
    }
    void releaseAll() noexcept LG_NONBLOCKING {
        held.count = 0;
        dirty = true;
    }

    // Applies the note changes since the last render. Done lazily so a chord change, whose note-offs and note-ons
    // share a sample, is voiced once from the finished chord rather than through every half-changed set.
    void settle() noexcept LG_NONBLOCKING {
        if (!dirty)
            return;
        dirty = false;
        if (held.count == 0) {
            if (gate)
                engine.noteOff(kRoot);
            gate = false;
            return;
        }
        std::array<double, kVoices> target{};
        if (chordal)
            voiceChord(target);
        else {
            constexpr std::array<double, kVoices> kUnison{0.0, -0.07, 0.07, -0.035, 0.035};
            const double p = held.newestPitch();
            for (int v = 0; v < kVoices; ++v)
                pitch[static_cast<std::size_t>(v)] = p + kUnison[static_cast<std::size_t>(v)];
        }
        for (auto& scene : matrix.offsets)
            for (int v = 0; v < kVoices; ++v)
                scene[static_cast<std::size_t>(v)] =
                    static_cast<float>(std::clamp(pitch[static_cast<std::size_t>(v)], 12.0, 120.0) - kRoot);
        matrix.isCustom = true;
        engine.setSceneMatrix(matrix);
        engine.noteOn(kRoot, velocity); // opens the gate, or refreshes the velocity while it is open
        gate = true;
    }

    void voiceChord(std::array<double, kVoices>& target) noexcept LG_NONBLOCKING {
        std::array<int, kMaxHeld> notes{};
        const int m = chooseChordNotes(notes, held.sorted(notes));
        // Fewer notes than voices: double the top, then the bottom, a few cents apart so the doubles chorus.
        for (int k = 0; k < kVoices; ++k) {
            if (k < m) {
                target[static_cast<std::size_t>(k)] = notes[static_cast<std::size_t>(k)];
                continue;
            }
            const int d = k - m;
            const int from = d % 2 == 0 ? m - 1 - (d / 2) % m : (d / 2) % m;
            const double detune = (d % 2 == 0 ? 0.07 : -0.07) * (1 + d / 2); // semitones
            target[static_cast<std::size_t>(k)] = notes[static_cast<std::size_t>(from)] + detune;
        }
        std::array<bool, kVoices> voiceDone{}, targetDone{};
        const auto assign = [&](int v, int t) noexcept LG_NONBLOCKING {
            pitch[static_cast<std::size_t>(v)] = target[static_cast<std::size_t>(t)];
            voiceDone[static_cast<std::size_t>(v)] = true;
            targetDone[static_cast<std::size_t>(t)] = true;
        };
        if (!engine.telemetry().sounding) {
            // After silence: the lowest note in the middle, then outwards left and right as the chord climbs.
            constexpr std::array<int, kVoices> kSpread{0, 3, 4, 1, 2};
            for (int k = 1; k < kVoices; ++k)
                for (int j = k; j > 0 && target[static_cast<std::size_t>(j - 1)] > target[static_cast<std::size_t>(j)];
                     --j)
                    std::swap(target[static_cast<std::size_t>(j - 1)], target[static_cast<std::size_t>(j)]);
            for (int k = 0; k < kVoices; ++k)
                assign(kSpread[static_cast<std::size_t>(k)], k);
            return;
        }
        // While the last chord still rings (held or in its tail): voices keep the notes that stay and the rest move
        // as little as they can, so no voice sweeps far under an audible tail.
        for (int v = 0; v < kVoices; ++v)
            for (int t = 0; t < kVoices; ++t)
                if (!targetDone[static_cast<std::size_t>(t)] && !voiceDone[static_cast<std::size_t>(v)] &&
                    std::abs(pitch[static_cast<std::size_t>(v)] - target[static_cast<std::size_t>(t)]) < 1e-6)
                    assign(v, t);
        for (;;) {
            int bv = -1, bt = -1;
            double best = 1e9;
            for (int v = 0; v < kVoices; ++v)
                for (int t = 0; t < kVoices; ++t) {
                    if (voiceDone[static_cast<std::size_t>(v)] || targetDone[static_cast<std::size_t>(t)])
                        continue;
                    const double d = std::abs(pitch[static_cast<std::size_t>(v)] - target[static_cast<std::size_t>(t)]);
                    if (d < best) {
                        best = d;
                        bv = v;
                        bt = t;
                    }
                }
            if (bv < 0)
                break;
            assign(bv, bt);
        }
    }
};

// A closed hat of its own: the 808's six square oscillators and white noise through a bright band-pass.
class NoiseHat {
  public:
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        band_ = hba::dsp::SvfCoefs::make(std::min(9500.0, 0.42 * sr_), 1.2, sr_);
        air_ = hba::dsp::SvfCoefs::make(std::min(7000.0, 0.40 * sr_), 0.7, sr_);
        constexpr std::array<double, 6> kMetal{205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
        for (std::size_t k = 0; k < kMetal.size(); ++k)
            inc_[k] = kMetal[k] / sr_;
        reset();
    }
    void reset() noexcept LG_NONBLOCKING {
        env_ = 0.0;
        bandL_.reset();
        bandR_.reset();
        airL_.reset();
        airR_.reset();
        phase_.fill(0.0);
    }
    void hit(float velocity) noexcept LG_NONBLOCKING {
        const double v = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        env_ = std::max(env_, v);
        decay_ = std::exp(-1.0 / ((0.010 + 0.022 * v) * sr_)); // accents ring a little longer than ghosts
    }
    bool active() const noexcept LG_NONBLOCKING { return env_ > 1e-6; }
    void render(float* left, float* right, int n) noexcept LG_NONBLOCKING {
        for (int i = 0; i < n; ++i) {
            if (env_ < 1e-6) {
                left[i] = right[i] = 0.0f;
                continue;
            }
            double metal = 0.0;
            for (std::size_t k = 0; k < phase_.size(); ++k) {
                metal += hba::dsp::polyBlepSquare(phase_[k], inc_[k]);
                phase_[k] += inc_[k];
                if (phase_[k] >= 1.0)
                    phase_[k] -= 1.0;
            }
            const double shared = noise_.nextBipolar();
            const double xl = 0.09 * metal + 0.55 * shared + 0.25 * noise_.nextBipolar();
            const double xr = 0.09 * metal + 0.55 * shared + 0.25 * noise_.nextBipolar();
            left[i] = static_cast<float>(airL_.highpass(air_, bandL_.bandpass(band_, xl)) * env_ * 0.9);
            right[i] = static_cast<float>(airR_.highpass(air_, bandR_.bandpass(band_, xr)) * env_);
            env_ *= decay_;
        }
    }

  private:
    double sr_ = 48000.0;
    double env_ = 0.0;
    double decay_ = 0.999;
    std::array<double, 6> phase_{}, inc_{};
    hba::dsp::SvfCoefs band_{}, air_{};
    hba::dsp::SvfState bandL_, bandR_, airL_, airR_;
    hba::dsp::XorShift32 noise_{0x6a09e667u};
};

void neutraliseGlide(tg::Params& p) noexcept {
    using tg::ParamId;
    p.set(ParamId::mode, static_cast<float>(tg::Mode::copedent));
    p.set(ParamId::rootKey, 0.0f); // C, so the idle root is C2 (kRoot)
    p.set(ParamId::morph, 0.0f);
    p.set(ParamId::morphAuto, 0.0f);
    p.set(ParamId::gravity, 0.0f); // scale wells would bend the parts' notes toward Torus Glide's own key
    p.set(ParamId::stagger, 0.0f);
    p.set(ParamId::anchorSub, 0.0f);
    p.set(ParamId::keySwitch, 0.0f);
    p.set(ParamId::retrig, 0.0f);
    p.set(ParamId::rissetGate, 0.0f);
    p.set(ParamId::mpeOut, 0.0f);
    p.set(ParamId::padTouch, 0.0f);
    p.set(ParamId::padHold, 0.0f);
    p.set(ParamId::bypass, 0.0f);
    p.set(ParamId::subAnchor, 0.0f); // the bass part owns the sub
    for (const auto [target, depth] :
         {std::pair{ParamId::lane1Target, ParamId::lane1Depth}, std::pair{ParamId::lane2Target, ParamId::lane2Depth}}) {
        const auto t = static_cast<tg::LaneTarget>(p.choice(target));
        if (t == tg::LaneTarget::morph || t == tg::LaneTarget::gravity || t == tg::LaneTarget::rissetGate)
            p.set(depth, 0.0f);
    }
}

} // namespace

struct StudioVoices::Impl {
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool ready = false;
    int latency = 0;

    GlidePart chords, melody;

    ln::Engine bass;
    ln::Params bassParams = ln::Params::defaults();
    HeldNotes bassHeld; // only to keep the bass awake while it holds a note; Lineage keeps its own note stack
    Source bassOut;

    sb::Engine arp;
    sb::Params arpParams = sb::Params::defaults();
    Source arpOut;

    dt::Engine kick;
    dt::Params kickParams = dt::Params::defaults();
    Source kickOut;

    sb::Engine clap;
    sb::Params clapParams = sb::Params::defaults();
    Source clapOut;

    NoiseHat hat;
    Source hatOut;

    std::vector<float> mixL, mixR;

    void renderChunk(float* left, float* right, int n, float gain, double bpm, double beat,
                     bool playing) noexcept LG_NONBLOCKING {
        std::fill(mixL.begin(), mixL.begin() + n, 0.0f);
        std::fill(mixR.begin(), mixR.begin() + n, 0.0f);
        float* ml = mixL.data();
        float* mr = mixR.data();
        const tg::Transport tgClock{bpm, beat, playing};
        GlidePart* const parts[] = {&chords, &melody};
        for (GlidePart* part : parts) {
            auto& o = part->out;
            if (!o.awake)
                continue;
            part->engine.process(part->params, tgClock, o.left.data(), o.right.data(), n);
            o.finish(ml, mr, n, part->gate);
        }
        if (bassOut.awake) {
            bass.process(bassParams, {bpm, beat, playing}, bassOut.left.data(), bassOut.right.data(), n);
            bassOut.finish(ml, mr, n, bassHeld.count > 0);
        }
        if (arpOut.awake) {
            arp.process(arpParams, arpOut.left.data(), arpOut.right.data(), n);
            arpOut.finish(ml, mr, n, false);
        }
        if (kickOut.awake) {
            kick.process(kickParams, {bpm, beat, playing}, kickOut.left.data(), kickOut.right.data(), n);
            kickOut.finish(ml, mr, n, false);
        }
        if (clapOut.awake) {
            clap.process(clapParams, clapOut.left.data(), clapOut.right.data(), n);
            clapOut.finish(ml, mr, n, false);
        }
        if (hatOut.awake) {
            hat.render(hatOut.left.data(), hatOut.right.data(), n);
            hatOut.finish(ml, mr, n, hat.active());
        }
        const double g = static_cast<double>(gain);
        for (int i = 0; i < n; ++i) {
            left[i] += static_cast<float>(hba::dsp::softClip(static_cast<double>(ml[i]) * g));
            right[i] += static_cast<float>(hba::dsp::softClip(static_cast<double>(mr[i]) * g));
        }
    }
};

StudioVoices::StudioVoices() : impl_(std::make_unique<Impl>()) {}

StudioVoices::~StudioVoices() = default;

void StudioVoices::prepare(double sampleRate, int maxBlockSize) {
    auto& m = *impl_;
    m.ready = false;
    m.sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    m.maxBlock = std::clamp(maxBlockSize, 16, 8192);
    const double sr = m.sampleRate;
    const int block = m.maxBlock;

    m.chords.chordal = true;
    m.melody.chordal = false;
    bool loaded = true;
    for (auto [part, json] :
         {std::pair{&m.chords, voice_presets::chords()}, std::pair{&m.melody, voice_presets::melody()}}) {
        const auto preset = tg::parsePresetJson(json);
        loaded = loaded && preset.has_value();
        part->params = preset ? preset->params : tg::Params::defaults();
        neutraliseGlide(part->params);
        if (part->chordal) // Torus Glide's voices share one envelope: a shorter tail lets Stabs and Pulse articulate
            part->params.set(tg::ParamId::releaseMs, std::min(part->params.real(tg::ParamId::releaseMs), 180.0f));
        part->engine.prepare(sr, block, kOversampling);
        if (preset)
            part->engine.setLaneCurves(preset->curves);
    }

    const auto bass = ln::parsePreset(voice_presets::bass());
    loaded = loaded && bass.has_value();
    m.bassParams = bass ? bass->params : ln::Params::defaults();
    m.bassParams[ln::ParamId::glideMode] = static_cast<float>(ln::GlideMode::legato); // slide only between tied notes
    m.bassParams[ln::ParamId::playMode] = static_cast<float>(ln::PlayMode::mono);
    m.bassParams[ln::ParamId::genMix] = 0.0f; // no Strata print to play
    m.bassParams[ln::ParamId::padTouch] = 0.0f;
    m.bassParams[ln::ParamId::padHold] = 0.0f;
    m.bass.prepare(sr, block, kOversampling);
    if (bass)
        m.bass.setLaneCurves(bass->curves);

    const auto arp = sb::parsePresetJson(voice_presets::arp());
    loaded = loaded && arp.has_value();
    m.arpParams = arp ? arp->params : sb::Params::defaults();
    m.arpParams.set(sb::ParamId::keyTrack, static_cast<float>(sb::KeyTrackMode::chromatic));
    m.arpParams.set(sb::ParamId::chokeMode, static_cast<float>(sb::ChokeMode::poly4x));
    m.arpParams.set(sb::ParamId::humanize, 0.0f);
    // A pluck, not a bell left to ring: 16ths reuse Strike Body's four voices every bar, so each must have died away
    // before it is taken again.
    m.arpParams.set(sb::ParamId::shellDecay, 120.0f);
    m.arpParams.set(sb::ParamId::bodyDecay, 110.0f);
    m.arp.prepare(sr, block, kOversampling);

    const auto clap = sb::parsePresetJson(voice_presets::clap());
    loaded = loaded && clap.has_value();
    m.clapParams = clap ? clap->params : sb::Params::defaults();
    m.clapParams.set(sb::ParamId::keyTrack, static_cast<float>(sb::KeyTrackMode::kitMap));
    m.clapParams.set(sb::ParamId::humanize, 0.0f);
    m.clap.prepare(sr, block, kOversampling);

    const auto kick = dt::parsePreset(voice_presets::kick());
    loaded = loaded && kick.has_value();
    m.kickParams = kick ? kick->params : dt::Params::defaults();
    m.kickParams[dt::ParamId::seqRun] = 0.0f;
    m.kickParams[dt::ParamId::refBassMode] = static_cast<float>(dt::RefBassMode::off);
    m.kickParams[dt::ParamId::humanize] = 0.0f;
    m.kick.prepare(sr, block, kOversampling);
    if (kick) {
        m.kick.setCurves(kick->pitchCurve, kick->ampCurve);
        m.kick.setPattern(kick->pattern);
    }

    m.hat.prepare(sr);

    // Line every instrument up behind the slowest look-ahead.
    const int tgLatency = std::max(m.chords.engine.latencySamples(), m.melody.engine.latencySamples());
    m.latency = std::max(
        {tgLatency, m.bass.latencySamples(), m.arp.latencySamples(), m.kick.latencySamples(), m.clap.latencySamples()});
    const int sleep = static_cast<int>(sr * 0.5);
    m.chords.out.prepare(block, m.latency - m.chords.engine.latencySamples(), sleep, kRoleTrim[0]);
    m.bassOut.prepare(block, m.latency - m.bass.latencySamples(), sleep, kRoleTrim[1]);
    m.melody.out.prepare(block, m.latency - m.melody.engine.latencySamples(), sleep, kRoleTrim[2]);
    m.arpOut.prepare(block, m.latency - m.arp.latencySamples(), sleep, kRoleTrim[3]);
    m.kickOut.prepare(block, m.latency - m.kick.latencySamples(), sleep, kDrumTrim[0]);
    m.hatOut.prepare(block, m.latency, sleep, kDrumTrim[1]);
    m.clapOut.prepare(block, m.latency - m.clap.latencySamples(), sleep, kDrumTrim[2]);
    m.mixL.assign(static_cast<std::size_t>(block), 0.0f);
    m.mixR.assign(m.mixL.size(), 0.0f);

    m.ready = loaded;
    reset();
}

void StudioVoices::reset() noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    GlidePart* const parts[] = {&m.chords, &m.melody};
    for (GlidePart* part : parts) {
        part->engine.reset();
        part->held.count = 0;
        part->gate = false;
        part->dirty = false;
        part->out.reset();
    }
    m.bass.reset();
    m.bassHeld.count = 0;
    m.arp.reset();
    m.kick.reset();
    m.clap.reset();
    m.hat.reset();
    Source* const sources[] = {&m.bassOut, &m.arpOut, &m.kickOut, &m.clapOut, &m.hatOut};
    for (Source* s : sources)
        s->reset();
}

int StudioVoices::latencySamples() const noexcept LG_NONBLOCKING { return impl_->latency; }

bool StudioVoices::presetsLoaded() const noexcept LG_NONBLOCKING { return impl_->ready; }

void StudioVoices::noteOn(int role, int pitch, float velocity) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    const int p = std::clamp(pitch, 0, 127);
    const float v = std::clamp(velocity, 0.0f, 1.0f);
    switch (role) {
    case 0:
        m.chords.noteOn(p, v);
        break;
    case 1:
        m.bass.noteOn(p, v);
        m.bassHeld.add(p);
        m.bassOut.wake();
        break;
    case 2:
        m.melody.noteOn(p, v);
        break;
    case 3:
        m.arp.noteOn(p, v, m.arpParams);
        m.arpOut.wake();
        break;
    default:
        break;
    }
}

void StudioVoices::noteOff(int role, int pitch) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    const int p = std::clamp(pitch, 0, 127);
    switch (role) {
    case 0:
        m.chords.noteOff(p);
        break;
    case 1:
        m.bass.noteOff(p);
        m.bassHeld.remove(p);
        break;
    case 2:
        m.melody.noteOff(p);
        break;
    default: // the arp's plucks ring out on their own
        break;
    }
}

void StudioVoices::drum(int kind, float velocity) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    const float v = std::clamp(velocity, 0.0f, 1.0f);
    switch (kind) {
    case 0:
        m.kick.triggerSlot(0, v);
        m.kickOut.wake();
        break;
    case 1:
        m.hat.hit(v);
        m.hatOut.wake();
        break;
    case 2:
        m.clap.noteOn(36, v, m.clapParams); // C1: the main hit in Kit Map
        m.clapOut.wake();
        break;
    default:
        break;
    }
}

void StudioVoices::releaseAll() noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    m.chords.releaseAll();
    m.melody.releaseAll();
    m.bass.allNotesOff();
    m.bassHeld.count = 0;
}

void StudioVoices::render(float* left, float* right, int count, float gain, double bpm, double beat,
                          bool playing) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    if (!m.ready || left == nullptr || right == nullptr || count <= 0)
        return;
    m.chords.settle();
    m.melody.settle();
    const double beatsPerSample = std::clamp(bpm, 20.0, 400.0) / 60.0 / m.sampleRate;
    for (int done = 0; done < count;) {
        const int n = std::min(count - done, m.maxBlock);
        m.renderChunk(left + done, right + done, n, gain, bpm, beat + done * beatsPerSample, playing);
        done += n;
    }
}

} // namespace lg
