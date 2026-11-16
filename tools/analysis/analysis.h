// Offline signal analysis for tf-render's metrics and the engine tests. These
// are deliberately independent of the engine's own DSP (biquads instead of
// SVFs, a longer interpolator for true peak), so a measurement does not share
// a bug with the thing it measures.
#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace tf::analysis {

using Signal = std::vector<double>;

// In-place radix-2 FFT; size must be a power of two.
void fft(std::vector<std::complex<double>>& data);

// Blackman-Harris 4-term window (-92 dB side lobes).
Signal blackmanHarris(std::size_t n);
Signal hann(std::size_t n);

// Single-sided power spectrum |X|^2 of one windowed frame (size power of 2).
Signal powerSpectrum(const double* x, std::size_t n, const Signal& window);

// Welch-averaged power spectrum with 50% overlap.
Signal welch(const Signal& x, std::size_t frame);

double rms(const double* x, std::size_t n);
double rmsDb(const Signal& x);
double toDb(double linear);  // 20 log10, floored at -300 dB

// Linkwitz-Riley 4th order (two RBJ Butterworth biquads), zero initial state.
Signal lr4Lowpass(const Signal& x, double hz, double sampleRate);
Signal lr4Highpass(const Signal& x, double hz, double sampleRate);

// Peak of the 4x interpolated signal (long windowed sinc, linear phase).
double truePeak4x(const Signal& x);

// Pearson-style correlation sum(LR) / sqrt(sum(L^2) sum(R^2)).
double correlation(const Signal& l, const Signal& r);

struct Stereo {
    Signal left, right;
};
Stereo deinterleave(const std::vector<float>& interleaved);

// The numbers tf-render --metrics prints. `steady` selects the part of the
// render the level and movement metrics are computed on.
struct Metrics {
    double peakDb = 0, truePeakDb = 0, rmsDb = 0, crestDb = 0;
    double subRmsDb = 0, subRmsRangeDb = 0;
    double sideBelowXoverDb = 0;
    double correlation = 0;
    double centroidHz = 0, centroidRangeHz = 0;
    double growlRmsDb = 0;
};

struct MetricsRequest {
    double sampleRate = 48000;
    double subFreq = 120;
    double xover = 100;
    std::size_t steadyBegin = 0;  // frames
    std::size_t steadyEnd = 0;    // frames, exclusive
};

Metrics computeMetrics(const Stereo& audio, const MetricsRequest& request);

// RMS (dB) of x[begin, end) over Hann-weighted windows of `seconds`, hop half a window.
Signal windowedRmsDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double seconds = 0.05);

// Side energy below `xover` relative to mid energy below `xover`, in dB, from
// a Welch spectrum of the given range.
double sideBelowXoverDb(const Stereo& audio, std::size_t begin, std::size_t end, double sampleRate, double xover);

// Frame-wise magnitude-weighted spectral centroid (20 Hz..20 kHz) of x[begin, end).
Signal centroids(const Signal& x, std::size_t begin, std::size_t end, double sampleRate);

double percentile(Signal values, double p);

std::string toJson(const Metrics& m);

bool writeWavFloat(const std::string& path, const std::vector<float>& interleaved, int channels, int sampleRate);

}
