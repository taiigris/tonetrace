#include "analysis/Spectrogram.h"

#include <algorithm>
#include <cmath>

#include "analysis/Hpss.h"
#include "analysis/PooledBins.h"
#include "analysis/SpectralFrames.h"

namespace ttr {


std::shared_ptr<Spectrogram> computeSpectrogram(const float* mono, size_t count, double sampleRate) {
    if (!mono || count < 1024 || sampleRate < 8000.0) return nullptr;
    const DecimatedSignal sig = decimate(mono, count, sampleRate);
    int frames = 0;
    std::vector<float> raw = computeNoteEnergies(sig, &frames);
    const PooledSpectrum pooled = computeTransientSpectrum(sig, frames);
    const float peak = *std::max_element(raw.begin(), raw.end());
    if (peak <= 1e-9f) return nullptr;

    auto spec = std::make_shared<Spectrogram>();
    spec->frames = frames;
    spec->peak = peak;
    spec->hopSec = double(kHopSize) / sig.sampleRate;
    spec->lengthSec = double(count) / sampleRate;

    const int timeWin = int(std::lround(kHpssTimeSec / spec->hopSec));
    const int freqWin = int(std::lround(kHpssFreqHz / pooled.binHz));
    HpssResult hpss = runHpss(pooled.mags, frames, pooled.bins, timeWin, freqWin);

    const std::vector<PooledPair> bin = noteToPooledBins(pooled.binHz, pooled.bins);
    spec->harmonic.resize(raw.size());
    for (int f = 0; f < frames; ++f)
        for (int n = 0; n < kNumNotes; ++n) {
            const size_t i = size_t(f) * kNumNotes + size_t(n);
            const float* m = &hpss.mask[size_t(f) * size_t(pooled.bins)];
            const float mask = std::max(m[bin[size_t(n)].first], m[bin[size_t(n)].second]);
            spec->harmonic[i] = raw[i] * mask;
        }
    spec->raw = std::move(raw);
    spec->percFrac = std::move(hpss.percFrac);
    spec->flux = std::move(hpss.flux);
    return spec;
}

}  // namespace ttr
