// tf-render: preset + note + bars -> 32-bit float stereo WAV, and optionally a
// JSON line of metrics, so patches can be designed and checked without ears.
//
// tf-render [--preset file.json] [--note 29] [--bars 4] [--bpm 174] [--rate 48000]
//           [--os 16] [--set id=value ...] --out file.wav [--metrics]
//
// --set overrides one parameter after the preset is loaded, in real units or
// by choice label (--set motion=0 --set fbTime="1 cyc"); repeatable.
//
// Metrics are computed on the held part of the note, skipping the first 250 ms
// (attack, smoothing): rms, sub, side, correlation and centroid. Peaks cover the
// whole file. centroidRangeHz is the 5th-95th percentile spread of 4096-sample
// frame centroids. sideBelowXoverDb is side/mid energy below the preset's
// crossover. sumCrestDb is the crest factor of the whole output over the held
// part. tiltDbPerOct and offHarmonicDb are defined in tools/analysis/analysis.h;
// the off-harmonic tolerance is max(5 Hz, 1.5 x the larger voice offset from
// f0 x ratio) for the parameters as set (lanes not included), and is printed
// as harmonicToleranceHz. Four extra fields report the limiter's and the growl
// clipper's gain reduction.
#include "analysis/analysis.h"

#include "tf/preset.h"
#include "tf/render.h"
#include "tf/voice_pitch.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void usage() {
    std::cerr << "usage: tf-render [--preset file.json] [--note 29] [--bars 4] [--bpm 174] [--rate 48000] [--os 16]\n"
                 "                 [--set id=value ...] --out file.wav [--metrics]\n";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// "id=value": value in real units, or a choice label.
bool applySet(tf::Params& params, const std::string& assignment) {
    const auto eq = assignment.find('=');
    if (eq == std::string::npos) return false;
    const auto id = tf::findParam(assignment.substr(0, eq));
    if (!id) return false;
    const std::string value = assignment.substr(eq + 1);
    const auto& spec = tf::spec(*id);
    for (std::size_t i = 0; i < spec.choices.size(); ++i) {
        if (spec.choices[i] == value) {
            params[*id] = static_cast<float>(i);
            return true;
        }
    }
    try {
        params[*id] = tf::clampToRange(spec, std::stof(value));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

}

int main(int argc, char** argv) {
    tf::RenderRequest request;
    std::string presetPath, outPath;
    std::vector<std::string> sets;
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
            else if (arg == "--set") sets.push_back(value());
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
    for (const auto& assignment : sets) {
        if (!applySet(request.params, assignment)) {
            std::cerr << "bad --set " << assignment << "\n";
            return 2;
        }
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
        q.f0 = 440.0 * std::exp2((request.note - 69) / 12.0);
        const auto offsets = tf::voiceOffsetsHz(request.params, q.f0, request.bpm);
        q.harmonicToleranceHz = tf::analysis::harmonicToleranceHz(offsets.a, offsets.b);
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
