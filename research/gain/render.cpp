// SPDX-License-Identifier: GPL-3.0-or-later
#include "DrumLoop.h"
#include "engine/Cutter.h"
#include "engine/PadPrograms.h"
#include "tf/engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace livecut::engine;

static void render(const fs::path& output, int rate, int block)
{
    const testing::DrumLoop drum(55.0, testing::DrumLoop::Bass::sine);
    Cutter cutter;
    cutter.prepare(rate);
    const auto pad = mapPad(PadProgram::rush, 0.65f, 0.8f, {});
    cutter.setProcedure(pad.procedure);
    cutter.setUnitsPerBar(pad.unitsPerBar);
    cutter.setSeed(pad.seed);
    cutter.setParameters(pad.cut);
    tf::Engine synth;
    synth.prepare(rate, block, 4);
    synth.noteOn(29);
    auto params = tf::Params::defaults();
    params[tf::ParamId::midGain] = -3.0f;
    const auto suffix = "-" + std::to_string(rate) + "-" + std::to_string(block) + ".f32";
    std::ofstream cut(output / ("livecut-redux" + suffix), std::ios::binary);
    std::ofstream tract(output / ("tract-form" + suffix), std::ios::binary);
    cut.exceptions(std::ios::badbit | std::ios::failbit);
    tract.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<float> left(block), right(block);
    const auto write = [&](std::ostream& out, int n) {
        for (int i = 0; i < n; ++i) {
            const std::array<float, 2> stereo {left[i], right[i]};
            out.write(reinterpret_cast<const char*>(stereo.data()), sizeof(stereo));
        }
    };
    for (int start = 0; start < rate * 3; start += block) {
        const int count = std::min(block, rate * 3 - start);
        const double beats = 2.0 * start / rate;
        const auto bar = static_cast<std::int64_t>(beats / 4.0);
        for (int i = 0; i < count; ++i) {
            const auto frame = static_cast<std::size_t>((start + i) * 48000LL / rate);
            left[i] = right[i] = drum.samples[frame % drum.samples.size()];
        }
        cutter.process(left.data(), right.data(), count, {120.0, 4.0}, {bar, beats - 4.0 * bar});
        if (cutter.error() != Cutter::Error::none) throw std::runtime_error("Livecut render failed");
        write(cut, count);
        synth.process(params, {120.0, beats, true}, left.data(), right.data(), count);
        write(tract, count);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    fs::create_directories(argv[1]);
    for (const int rate : {44100, 48000})
        for (const int block : {64, 257})
            render(argv[1], rate, block);
}
