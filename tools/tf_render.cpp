// tf-render: preset + note + bars -> 32-bit float stereo WAV, and optionally a
// JSON line of metrics, so patches can be designed and checked without ears.
//
// tf-render [--preset file.json] [--note 29] [--bars 4] [--bpm 174] [--rate 48000]
//           [--os 16] --out file.wav [--metrics]
//
// Metrics are computed on the held part of the note, skipping the first 250 ms
// (attack, smoothing): rms, sub, side, correlation and centroid. Peaks cover the
// whole file. centroidRangeHz is the 5th-95th percentile spread of 4096-sample
// frame centroids. sideBelowXoverDb is side/mid energy below the preset's
// crossover. Two extra fields report the limiter's gain reduction.
#include "analysis/analysis.h"

#include "tf/preset.h"
#include "tf/render.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: tf-render [--preset file.json] [--note 29] [--bars 4] [--bpm 174] [--rate 48000] [--os 16]\n"
                 "                 --out file.wav [--metrics]\n";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

}

int main(int argc, char** argv) {
    tf::RenderRequest request;
    std::string presetPath, outPath;
    bool metrics = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        try {
            if (arg == "--preset") presetPath = value();
            else if (arg == "--note") request.note = std::stoi(value());
            else if (arg == "--bars") request.bars = std::stod(value());
            else if (arg == "--bpm") request.bpm = std::stod(value());
            else if (arg == "--rate") request.sampleRate = std::stod(value());
            else if (arg == "--os") request.oversampling = std::stoi(value());
            else if (arg == "--out") outPath = value();
            else if (arg == "--metrics") metrics = true;
            else if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            } else {
                std::cerr << "unknown argument " << arg << "\n";
                usage();
                return 2;
            }
        } catch (const std::exception&) {
            std::cerr << "bad value for " << arg << "\n";
            return 2;
        }
    }
    if (outPath.empty()) {
        usage();
        return 2;
    }

    if (!presetPath.empty()) {
        std::string json, error;
        if (!readFile(presetPath, json)) {
            std::cerr << "cannot read " << presetPath << "\n";
            return 1;
        }
        const auto preset = tf::parsePreset(json, &error);
        if (!preset) {
            std::cerr << presetPath << ": " << error << "\n";
            return 1;
        }
        request.params = preset->params;
    }

    tf::RenderStats stats;
    const auto audio = tf::renderOffline(request, &stats);
    const int rate = static_cast<int>(std::lround(request.sampleRate));
    if (!tf::analysis::writeWavFloat(outPath, audio, 2, rate)) {
        std::cerr << "cannot write " << outPath << "\n";
        return 1;
    }

    if (metrics) {
        tf::analysis::MetricsRequest q;
        q.sampleRate = request.sampleRate;
        q.subFreq = request.params[tf::ParamId::subFreq];
        q.xover = request.params[tf::ParamId::xover];
        q.steadyBegin = static_cast<std::size_t>(std::lround(0.25 * request.sampleRate));
        q.steadyEnd = static_cast<std::size_t>(stats.heldSamples);
        auto m = tf::analysis::computeMetrics(tf::analysis::deinterleave(audio), q);
        std::string json = tf::analysis::toJson(m);
        char extra[256];
        std::snprintf(extra, sizeof extra,
                      ", \"limiterMaxGrDb\": %.2f, \"limiterMeanGrDb\": %.2f, \"growlClipMaxDb\": %.2f, \"growlClipMeanDb\": %.2f}",
                      static_cast<double>(stats.maxGainReductionDb), static_cast<double>(stats.meanGainReductionDb),
                      static_cast<double>(stats.maxGrowlClipDb), static_cast<double>(stats.meanGrowlClipDb));
        json.pop_back();
        json += extra;
        std::cout << json << "\n";
    }
    return 0;
}
