// Internal helpers: decimation and magnitude spectra mapped to MIDI notes.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "analysis/Fft.h"
#include "core/Types.h"

namespace ttr {

constexpr int kWindowSize = 4096;
constexpr int kFftSize = 8192;
constexpr int kHopSize = 512;
constexpr double kTargetRate = 22050.0;
// The harmonic/percussive separation uses its own short-window STFT (same frame grid): the 186 ms
// note-analysis window would smear a drum hit over several frames and hide its transient nature.
constexpr int kTransientWindow = 1024;
constexpr double kTransientMaxHz = 8000.0;  // HPSS ignores everything above this

struct DecimatedSignal {
    std::vector<float> samples;
    double sampleRate = kTargetRate;
};

// Windowed-sinc decimator to about kTargetRate. Output sample i only reads input samples i*factor +- tail().
class Decimator {
public:
    explicit Decimator(double sampleRate);
    int factor() const { return factor_; }
    double outputRate() const { return outputRate_; }
    int tail() const { return half_; }
    float at(const float* in, size_t count, size_t i) const;

private:
    int factor_ = 1, half_ = 0;
    double outputRate_ = kTargetRate;
    std::vector<float> taps_;
};

DecimatedSignal decimate(const float* in, size_t count, double sampleRate);

// Note energies of single frames (frame f is centred at decimated sample f*kHopSize).
class NoteFrameBuilder {
public:
    explicit NoteFrameBuilder(double decimatedRate);
    // Writes kNumNotes energies; samples outside [0, count) read as silence.
    void compute(const float* samples, long count, int frame, float* out);

private:
    struct Band { int lowBin, highBin; double centreBin; };
    std::vector<Band> bands_;
    std::vector<float> window_, frame_, re_, im_, mag_;
    std::unique_ptr<RealFft> fft_;
};

// Linear magnitude spectrogram (frames * bins) used by the harmonic/percussive separation.
struct PooledSpectrum {
    int bins = 0;
    double binHz = 0.0;
    std::vector<float> mags;
};

// Short-window magnitude spectrum of single frames on the same grid (frame f is centred at decimated sample f*kHopSize).
class TransientFrameBuilder {
public:
    explicit TransientFrameBuilder(double decimatedRate);
    int bins() const { return bins_; }
    double binHz() const { return binHz_; }
    // Writes bins() magnitudes; samples outside [0, count) read as silence.
    void compute(const float* samples, long count, int frame, float* out);

private:
    int bins_ = 0;
    double binHz_ = 0.0;
    std::vector<float> window_, frame_, re_, im_;
    std::unique_ptr<RealFft> fft_;
};

// Per-frame note energies (amplitude-like), frames * kNumNotes.
std::vector<float> computeNoteEnergies(const DecimatedSignal& signal, int* outFrames);

// Short-window magnitude spectrogram on the same frame grid as computeNoteEnergies.
PooledSpectrum computeTransientSpectrum(const DecimatedSignal& signal, int frames);

}  // namespace ttr
