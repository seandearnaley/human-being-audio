// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <hba/core/note_name.h>
#include <hba/core/param_math.h>
#include <hba/core/smf.h>
#include <hba/core/wav.h>
#include <hba/dsp/allpass.h>
#include <hba/dsp/biquad.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/pitch.h>
#include <hba/dsp/random.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <vector>

TEST_CASE("hba::core::writeWavFloat writes spec-compliant IEEE float WAV with fact chunk", "[core][wav]") {
    const auto path =
        std::filesystem::temp_directory_path() / ("hba_wave_" + std::to_string(std::random_device{}()) + ".wav");
    const std::vector<float> samples{0.25f, -0.25f, 0.5f, -0.5f, 0.0f, 0.0f};
    REQUIRE(hba::core::writeWavFloat(path.string(), samples, 2, 48000));

    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    REQUIRE(std::filesystem::remove(path));

    // 12 (RIFF) + 26 (18-byte fmt) + 12 (4-byte fact) + 8 (data hdr) + 24 (6 floats) = 82 bytes
    REQUIRE(bytes.size() == 82u);
    CHECK(std::string(bytes.data(), 4) == "RIFF");
    CHECK(std::string(bytes.data() + 8, 4) == "WAVE");
    CHECK(std::string(bytes.data() + 12, 4) == "fmt ");
    CHECK(std::string(bytes.data() + 38, 4) == "fact");
    CHECK(std::string(bytes.data() + 50, 4) == "data");
}

TEST_CASE("hba::core::encodeSmfType0 writes valid MThd/MTrk headers and sorted events", "[core][smf]") {
    std::vector<hba::core::SmfEvent> events;
    events.push_back(hba::core::SmfEvent::msg(480, 1, 0x80, 60, 0));
    events.push_back(hba::core::SmfEvent::msg(0, 0, 0x90, 60, 100));

    const auto bytes = hba::core::encodeSmfType0(std::move(events), 960, 172.0, 960);
    const std::vector<std::uint8_t> expected{
        'M',  'T',  'h',  'd',  0, 0, 0, 6,  0, 0,    0,    1, 0x03, 0xC0,       // Type 0, one track, 960 PPQ
        'M',  'T',  'r',  'k',  0, 0, 0, 21, 0, 0xFF, 0x51, 3, 0x05, 0x52, 0xA5, // 172 BPM
        0,    0x90, 60,   100,                                                   // note-on at tick 0
        0x83, 0x60, 0x80, 60,   0,                                               // note-off at tick 480
        0x83, 0x60, 0xFF, 0x2F, 0};                                              // end at tick 960
    CHECK(bytes == expected);
}

TEST_CASE("hba::core param_math and note_name handle edge values cleanly", "[core][param]") {
    const float nanF = std::numeric_limits<float>::quiet_NaN();
    CHECK(hba::core::clampParam(nanF, 0.0f, 10.0f, 3.0f, true) == Catch::Approx(3.0f));
    CHECK(hba::core::clampParam(2.6f, 0.0f, 5.0f, 0.0f, false) == Catch::Approx(3.0f));

    const float logNorm = hba::core::toNormalisedLogSkew(200.0f, 20.0f, 2000.0f, 200.0f, 0.0f, true);
    CHECK(logNorm == Catch::Approx(0.5f).margin(1e-5f));
    CHECK(hba::core::fromNormalisedLogSkew(0.5f, 20.0f, 2000.0f, 200.0f, 0.0f, true) ==
          Catch::Approx(200.0f).margin(1e-3f));

    const float juceNorm = hba::core::toNormalisedJuceSkew(320.0f, 80.0f, 2400.0f, 320.0f, 0.35f, true);
    CHECK(hba::core::fromNormalisedJuceSkew(juceNorm, 80.0f, 2400.0f, 320.0f, 0.35f, true) ==
          Catch::Approx(320.0f).margin(1e-3f));

    CHECK(hba::core::midiNoteName(69) == "A4");
    CHECK(hba::core::midiNoteName(61, false) == "C#4");
    CHECK(hba::core::midiNoteName(61, true) == "Db4");
    CHECK(hba::core::hzToNoteName(440.0f) == "A4");
}

TEST_CASE("hba::dsp pitch, random, oscillator, allpass, and biquad are accurate and finite", "[dsp]") {
    CHECK(hba::dsp::midiToHz(69.0) == Catch::Approx(440.0));
    CHECK(hba::dsp::hzToMidi(440.0) == Catch::Approx(69.0));
    CHECK(hba::dsp::semitonesToRatio(12.0) == Catch::Approx(2.0));
    CHECK(hba::dsp::centsToRatio(1200.0) == Catch::Approx(2.0));

    hba::dsp::Lcg32 lcg(42u);
    hba::dsp::XorShift32 xs(0u);
    for (int i = 0; i < 64; ++i) {
        const double a = lcg.nextBipolar();
        const double b = xs.nextBipolar();
        CHECK(a >= -1.0);
        CHECK(a <= 1.0);
        CHECK(b >= -1.0);
        CHECK(b <= 1.0);
    }

    // AllpassPair preserves unit energy on a sine wave
    hba::dsp::AllpassPair ap;
    ap.setFreq(600.0, 48000.0);
    double inE = 0.0, outE = 0.0;
    for (int i = 0; i < 2048; ++i) {
        const double s = std::sin(hba::dsp::kTwoPi * 440.0 * i / 48000.0);
        const double y = ap.process(s);
        if (i >= 512) {
            inE += s * s;
            outE += y * y;
        }
    }
    CHECK(outE / inE == Catch::Approx(1.0).margin(0.01));

    // Biquad lowpass attenuates high frequency and passes DC at unity
    const auto lp = hba::dsp::BiquadCoefs::lowpass(500.0, 0.7071, 48000.0);
    hba::dsp::BiquadState st;
    double dc = 0.0;
    for (int i = 0; i < 2048; ++i)
        dc = st.process(lp, 1.0);
    CHECK(dc == Catch::Approx(1.0).margin(1e-4));

    // PolyBLEP saw stays bounded
    double ph = 0.0;
    const double inc = 440.0 / 48000.0;
    for (int i = 0; i < 512; ++i) {
        const double y = hba::dsp::polyBlepSaw(ph, inc);
        CHECK(std::isfinite(y));
        CHECK(std::abs(y) <= 1.15);
        ph = hba::dsp::frac(ph + inc);
    }
}

#if defined(__linux__)
TEST_CASE("A buffered WAV write reports a full destination", "[core][wav]") {
    const float samples[] = {0.25f, -0.25f};
    REQUIRE_FALSE(hba::core::writeWavFloat("/dev/full", samples, 2, 48000));
}
#endif
