// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <visage_graphics/emoji.h>
#include <objbase.h>

#include <array>
#include <thread>

TEST_CASE("Rendering a Windows glyph releases the caller's COM apartment", "[glyph-lifetime]") {
    HRESULT started = E_FAIL;
    HRESULT after = E_FAIL;
    std::thread render([&] {
        started = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(started))
            return;

        constexpr int size = 32;
        std::array<unsigned int, size * size> pixels{};
        visage::EmojiRasterizer::instance().drawIntoBuffer(U'\U0001f3b5', size, size, pixels.data(), size, 0, 0);
        CoUninitialize();

        // A retained STA reference would prevent this thread from switching apartments.
        after = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(after))
            CoUninitialize();
    });
    render.join();

    REQUIRE(started == S_OK);
    REQUIRE(SUCCEEDED(after));
}
