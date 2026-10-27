// Per-frame spectral peaks of the pitch lines: a multi-resolution STFT (long windows for low pitches so that neighbouring
// semitones separate, short ones for high pitches so that they stay sharp in time) with parabolic peak interpolation.
#pragma once
#include <memory>
#include <vector>

#include "analysis/Fft.h"
#include "analysis/PitchLines.h"

namespace ttr {

class LineFrameAnalyzer {
public:
    explicit LineFrameAnalyzer(double decimatedRate);
    // Samples the longest window needs after the frame centre (live analysis waits for them).
    static int lookahead();
    // Writes the peaks of frame f (centred at sample f*kHopSize) fundamentals first (strongest first), then overtones, at most kLineSlots; returns how many.
    // Samples outside [0, count) read as silence. Sets kLineHarmonic on overtones; track / age stay unset.
    int compute(const float* samples, long count, int f, LinePoint* out);
    // The whole spectrum of the frame computed last on the fine pitch grid (kFineBins bytes, see PitchLines.h); silence writes zeros.
    void fineRow(uint8_t* out) const;

private:
    struct Band {
        int n = 0;
        double loHz = 0.0, hiHz = 0.0, binHz = 0.0;
        int firstBin = 0, lastBin = 0;
        int magFirst = 0, magLast = 0;  // bins whose magnitude is computed: firstBin..lastBin plus the blend zone of the fine spectrum
        float scale = 0.0f;  // magnitude -> sinusoid amplitude
        bool isValid = false;  // `mag` holds the last frame (false for a silent frame)
        std::vector<float> window, frame, re, im, mag;
        std::unique_ptr<RealFft> fft;
    };
    void collect(Band& band, const float* samples, long count, int f);
    void markHarmonics();
    float fineLevel(const Band& b, double hz) const;  // magnitude at hz (interpolated or strongest bin of the fine bin's span)

    std::vector<Band> bands_;
    std::vector<LinePoint> cands_;
    std::vector<float> scratch_;
};

}  // namespace ttr
