// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/dsp/gain.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Studio gain is an unclamped amplitude multiplier", "[dsp][gain]")
{
    using hba::dsp::dbToGain;
    CHECK(dbToGain(0.0) == 1.0);
    CHECK_THAT(dbToGain(-6.020599913279624), Catch::Matchers::WithinAbs(0.5, 1.e-15));
    CHECK_THAT(dbToGain(20.0), Catch::Matchers::WithinAbs(10.0, 1.e-14));
    CHECK_THAT(dbToGain(-120.0), Catch::Matchers::WithinAbs(1.e-6, 1.e-20));
}
