// Offline signal analysis for tf-render's metrics and the engine tests. These
// are deliberately independent of the engine's own DSP (biquads instead of
// SVFs, a longer interpolator for true peak), so a measurement does not share
// a bug with the thing it measures.
#pragma once

#include <complex>
#include <cstddef>
#include <optional>
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

// offHarmonicDb never counts energy this close to a harmonic as noise.
inline constexpr double kMinHarmonicToleranceHz = 5.0;

// The numbers tf-render --metrics prints. `steady` selects the part of the
// render the level and movement metrics are computed on.
struct Metrics {
    double peakDb = 0, truePeakDb = 0, rmsDb = 0, crestDb = 0;
    double subRmsDb = 0, subRmsRangeDb = 0;
    double sideBelowXoverDb = 0;
    double correlation = 0;
    double centroidHz = 0, centroidRangeHz = 0;
    double growlRmsDb = 0;
    // Steady part only: sample peak over the stereo RMS of the whole output
    // (sub and growl together). crestDb takes its peak over the whole file.
    double sumCrestDb = 0;
    // See tiltDbPerOct() and offHarmonicDb(). offHarmonicDb is only computed
    // when the request gives f0.
    std::optional<double> tiltDbPerOct;  // printed as null when empty
    double offHarmonicDb = 0;
    double harmonicToleranceHz = 0;
};

struct MetricsRequest {
    double sampleRate = 48000;
    double subFreq = 120;         // Hz: where the sub band ends and the growl begins
    double xover = 100;
    std::size_t steadyBegin = 0;  // frames
    std::size_t steadyEnd = 0;    // frames, exclusive
    double f0 = 0;                // Hz; 0 skips offHarmonicDb
    double tiltUpperHz = 16000;   // see tiltDbPerOct()
    double harmonicToleranceHz = kMinHarmonicToleranceHz;
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

// Welch spectrum with a Blackman-Harris window (-92 dB side lobes), 50 %
// overlap, frames of `frame` samples (power of two). Bin i is at i fs / frame.
Signal welchBlackmanHarris(const Signal& x, std::size_t frame);

// Largest power of two <= min(n, cap).
std::size_t powerOfTwoAtMost(std::size_t n, std::size_t cap);

// Spectral tilt in dB per octave: the least-squares slope of the energy (dB)
// in whole octave bands from 500 Hz (500-1k, 1k-2k, ...) against octave
// number, for every band that ends at or below min(upperHz, 16 kHz, Nyquist).
// Band energy, as an octave-band analyser shows it: pink noise reads 0, white
// noise +3, a sawtooth -3. (The 0.2 spec's targets and the 0.1 measurements
// use this.) tf-render passes upperHz = 0.8 x reach x f0, so the Reach
// cut-off itself is not read as tilt. Empty when the span from 500 Hz to the
// upper limit is under two octaves: there is no slope to speak of.
std::optional<double> tiltDbPerOct(const Signal& x, std::size_t begin, std::size_t end, double sampleRate,
                                   double upperHz = 16000.0);

// The share, in dB, of the 300 Hz - 6 kHz energy of x[begin, end) that lies
// further than `toleranceHz` from every multiple of f0. White noise at F1 with
// the 5 Hz minimum reads about -1.1 dB; a periodic note reads -60 dB or lower.
// The tolerance is the caller's: harmonicToleranceHz() gives the one
// tf-render uses. The spectrum is Welch-averaged Blackman-Harris frames of up
// to 2^16 samples, whose main lobe (+-4 bins, +-2.9 Hz at 48 kHz) fits inside
// the 5 Hz minimum, so a steady harmonic does not leak into the off share.
double offHarmonicDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double f0,
                     double toleranceHz);

// The tolerance for offHarmonicDb: max(5 Hz, 1.5 x the larger voice offset
// from f0 x its ratio). A detuned or beat-locked voice puts its partials that
// far from the harmonics on purpose; that beating is not noise.
double harmonicToleranceHz(double voiceOffsetAHz, double voiceOffsetBHz);

// Power (dB, arbitrary reference) of x[begin, end) between lo and hi Hz, from
// a Welch spectrum. Comparable between signals of the same length.
double bandPowerDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double lo, double hi);

std::string toJson(const Metrics& m);

bool writeWavFloat(const std::string& path, const std::vector<float>& interleaved, int channels, int sampleRate);

}
