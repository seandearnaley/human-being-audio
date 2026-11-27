// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/host/external_file_drag.h>
#include <hba/host/offline_export.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
enum class State { idle, rendering, ready, failed };
using Worker = hba::host::OfflineWavWorker<int, State>;

template <typename Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}
} // namespace

TEST_CASE("An offline render failure can be retried from the same worker", "[plugin][export]") {
    Worker worker("Export test", [](int request) -> std::vector<float> {
        if (request == 0)
            throw std::runtime_error("render failed");
        if (request == 1)
            return {};
        return {0.25f, -0.25f};
    });
    juce::TemporaryFile target(".wav");
    REQUIRE(worker.start(0, target.getFile(), 48000));
    REQUIRE(waitFor([&] { return worker.state() == State::failed; }));
    CHECK(worker.file() == juce::File{});
    REQUIRE(waitFor([&] { return worker.start(1, target.getFile(), 48000); }));
    REQUIRE(waitFor([&] { return worker.state() == State::failed; }));
    REQUIRE(waitFor([&] { return worker.start(2, target.getFile(), 48000); }));
    REQUIRE(waitFor([&] { return worker.state() == State::ready; }));
    CHECK(worker.file().getSize() == 66);
    CHECK(worker.file().deleteFile());
}

TEST_CASE("Concurrent exports keep both renders at a shared destination", "[plugin][export]") {
    juce::WaitableEvent release(true);
    auto render = [&](int frames) {
        if (!release.wait(2000))
            return std::vector<float>{};
        return std::vector<float>(static_cast<std::size_t>(frames) * 2u, 0.25f);
    };
    Worker first("First export", render);
    Worker second("Second export", render);
    juce::TemporaryFile target(".wav");
    REQUIRE(first.start(1, target.getFile(), 48000));
    REQUIRE(second.start(2, target.getFile(), 48000));
    release.signal();
    REQUIRE(waitFor([&] { return first.state() == State::ready && second.state() == State::ready; }));
    CHECK(first.file() != second.file());
    CHECK(first.file().getSize() == 66);
    CHECK(second.file().getSize() == 74);
    CHECK(first.file().deleteFile());
    CHECK(second.file().deleteFile());
}

TEST_CASE("An exported file keeps reserved filename characters across native drag transport", "[plugin][export]") {
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("A + B #1 50%.wav");
    const auto path = hba::host::externalDragPath(file);
#if JUCE_LINUX
    CHECK(path.startsWith("file:///"));
    CHECK(path.endsWith("A%20%2B%20B%20%231%2050%25.wav"));
    CHECK(juce::URL(path).getLocalFile() == file);
#else
    CHECK(path == file.getFullPathName());
#endif
}
