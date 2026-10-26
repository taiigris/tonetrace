// Spectrogram -> bitmap: semitone rows, colour ramp dark -> violet -> red -> yellow -> white.
#pragma once
#import <CoreGraphics/CoreGraphics.h>

struct TTRHeatParams {
    const float* data = nullptr;  // frames * kNumNotes note energies
    int frames = 0;
    double hopSec = 0.0;
    float reference = 0.0f;       // energy shown as full brightness (at neutral gain)
    double scrollSec = 0.0, pxPerSec = 100.0, scrollY = 0.0, rowH = 15.0;
    double width = 0.0, height = 0.0;  // points
    double scale = 1.0;           // device pixels per point
    float gain = 0.5f, range = 0.5f;   // 0..1, see TTRHeatShade
};

// Display brightness 0..1 of an energy: `range` is the dynamic range, `gain` shifts it. Exposed for tests/legend use.
float TTRHeatShade(float energy, float reference, float gain, float range);

// Returns an owned image (caller releases) or NULL without data.
CGImageRef TTRRenderHeatmap(const TTRHeatParams& params);
