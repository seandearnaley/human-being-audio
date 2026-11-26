// SPDX-License-Identifier: GPL-3.0-or-later
#include "Link.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if !JUCE_WINDOWS
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace stage {
namespace {

std::string randomToken() {
    std::random_device device;
    std::string token;
    for (int i = 0; i < 4; ++i) {
        char part[9];
        std::snprintf(part, sizeof part, "%08x", static_cast<unsigned>(device()));
        token += part;
    }
    return token;
}

// Where the Stage app writes its hub's ports and token: hub.json in Electron's userData folder
// for an app named "Stage".
juce::File hubFile() {
#if JUCE_MAC
    constexpr auto path = "Application Support/Stage/hub.json"; // ~/Library/…
#else
    constexpr auto path = "Stage/hub.json"; // %APPDATA%/… on Windows, ~/.config/… on Linux
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(path);
}

// Called once on each new connection, before anything is written to it.
void prepare([[maybe_unused]] const juce::StreamingSocket& socket) {
#if JUCE_MAC
    // Writing after the Stage quits must not raise SIGPIPE: that would take the whole DAW down.
    const int on = 1;
    ::setsockopt(socket.getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
#if !JUCE_WINDOWS
    // A Stage that is running but has stopped reading must not hold the pump thread, and with it
    // the plugin's destructor, for ever. A write that times out counts as a lost connection.
    const timeval limit{2, 0};
    ::setsockopt(socket.getRawSocketHandle(), SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);
#endif
}

// Writes all of it, or returns false. Never raises SIGPIPE, and never touches the process's signal
// handling: macOS sets SO_NOSIGPIPE on the socket (see prepare), Linux has no such option and
// passes MSG_NOSIGNAL with every send instead, Windows has no SIGPIPE.
bool writeAll(juce::StreamingSocket& socket, const void* data, std::size_t size) {
#if JUCE_LINUX || JUCE_BSD
    // JUCE's own write sends with no flags, so go through the raw handle.
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const auto sent = ::send(socket.getRawSocketHandle(), bytes, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return false;
        bytes += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
#else
    const auto bytes = static_cast<int>(size);
    return socket.write(data, bytes) == bytes;
#endif
}

void appendNumber(std::string& out, const char* format, double value) {
    char text[40];
    std::snprintf(text, sizeof text, format, value);
    out += text;
}

} // namespace

struct Link::State {
    Capture& capture;
    SampleRing& samples;
    std::string id{randomToken()}; // this instance, sent on every line
    std::thread pump;
    std::atomic<bool> running{true};

    // The hub connection. Touched only by the pump thread, apart from the atomics.
    juce::StreamingSocket hub;
    juce::StreamingSocket sound;
    int soundPort{0};
    std::uint64_t framesSent{0};
    std::atomic<double> sampleRate{48000.0};
    std::string hubPrefix;
    juce::int64 nextHubAttempt{0};
    std::atomic<int> role{1};
    std::atomic<bool> hubConnected{false};

    State(Capture& c, SampleRing& s) : capture(c), samples(s) {}

    void dropHub() {
        hub.close();
        sound.close();
        hubConnected.store(false);
    }

    // One line per block: {"token":…,"id":…,"role":…,<body>}. Every line carries the role, so the
    // Stage never has to remember a hello. Retries once a second while there is no hub.
    void sendToHub(const std::string& body) {
        if (!hub.isConnected()) {
            dropHub();
            const auto now = juce::Time::currentTimeMillis();
            if (now < nextHubAttempt)
                return;
            nextHubAttempt = now + 1000;
            const auto info = juce::JSON::parse(hubFile());
            const int hubPort = info["port"];
            if (hubPort <= 0 || !hub.connect("127.0.0.1", hubPort, 200)) {
                hub.close();
                return;
            }
            prepare(hub);
            soundPort = info["soundPort"];
            hubPrefix = "{\"token\":\"" + info["token"].toString().toStdString() + "\",\"id\":\"" + id + "\",";
            hubConnected.store(true);
        }
        const std::string line = hubPrefix + "\"role\":" + std::to_string(role.load()) + "," + body + "}\n";
        if (!writeAll(hub, line.data(), line.size()))
            dropHub();
    }

    // The sound at the input, if there is any: one header line saying where in the stream it
    // starts, then raw interleaved float samples. Nothing is taken from the ring while there is no
    // connection, so the audio thread's frame count and this one always agree.
    void sendSound() {
        if (!hub.isConnected() || samples.empty())
            return;
        if (!sound.isConnected()) {
            if (soundPort <= 0 || !sound.connect("127.0.0.1", soundPort, 200)) {
                sound.close();
                return;
            }
            prepare(sound);
            std::string header = hubPrefix + "\"sound\":{\"channels\":2,\"frame\":" + std::to_string(framesSent);
            appendNumber(header, ",\"rate\":%.1f}}\n", sampleRate.load());
            if (!writeAll(sound, header.data(), header.size())) {
                sound.close();
                return;
            }
        }
        float chunk[4096];
        while (const auto count = samples.pop(chunk, std::size(chunk))) {
            framesSent += count / 2;
            if (!writeAll(sound, chunk, count * sizeof(float))) {
                sound.close();
                return;
            }
        }
    }

    // One message per drain: the newest block, every note since the last drain, and the notes held
    // now. Events alone are not enough: a Stage that connects mid-note never saw its note-on.
    void pumpLoop() {
        Event event;
        std::vector<Event> held;
        while (running.load()) {
            Event block;
            bool haveBlock = false;
            std::string notes;
            while (capture.pop(event)) {
                if (event.type == Event::Type::block) {
                    block = event;
                    haveBlock = true;
                    continue;
                }
                std::erase_if(held, [&](const Event& h) { return h.note == event.note && h.channel == event.channel; });
                if (event.type == Event::Type::noteOn)
                    held.push_back(event);
                notes += notes.empty() ? "[" : ",[";
                notes += event.type == Event::Type::noteOn ? "1," : "0,";
                notes += std::to_string(event.note) + "," + std::to_string(event.channel) + ",";
                appendNumber(notes, "%.3f,", static_cast<double>(event.velocity));
                appendNumber(notes, "%.5f,", event.ppq);
                notes += std::to_string(event.micros) + "]";
            }
            if (haveBlock) {
                std::string text = "\"us\":" + std::to_string(block.micros);
                appendNumber(text, ",\"ppq\":%.5f", block.ppq);
                if (block.frame >= 0)
                    text += ",\"frame\":" + std::to_string(block.frame);
                appendNumber(text, ",\"bpm\":%.3f", block.bpm);
                text += ",\"playing\":" + std::string(block.playing ? "true" : "false");
                text += ",\"sig\":[" + std::to_string(block.sigNumerator) + "," + std::to_string(block.sigDenominator) +
                        "]";
                appendNumber(text, ",\"level\":%.4f", static_cast<double>(block.level));
                text += ",\"macros\":[";
                for (int i = 0; i < macroCount; ++i)
                    appendNumber(text, i == 0 ? "%.4f" : ",%.4f",
                                 static_cast<double>(block.macros[static_cast<std::size_t>(i)]));
                text += "],\"notes\":[" + notes + "],\"held\":[";
                for (const auto& h : held) {
                    text += (&h == held.data() ? "[" : ",[") + std::to_string(h.note) + "," + std::to_string(h.channel);
                    appendNumber(text, ",%.5f]", h.ppq);
                }
                text += "]";
                sendToHub(text);
            }
            sendSound();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
};

Link::Link(Capture& capture, SampleRing& samples) : state_(std::make_unique<State>(capture, samples)) {
    state_->pump = std::thread([&s = *state_] { s.pumpLoop(); });
}

Link::~Link() {
    state_->running.store(false);
    if (state_->pump.joinable())
        state_->pump.join();
}

void Link::setRole(int role) { state_->role.store(role); }
void Link::setSampleRate(double rate) { state_->sampleRate.store(rate); }
bool Link::hubConnected() const { return state_->hubConnected.load(); }

} // namespace stage
