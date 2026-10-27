// Spectrogram -> bitmap: semitone rows, colour ramp dark -> violet -> red -> yellow -> white.
#pragma once
#import <CoreGraphics/CoreGraphics.h>

#include "analysis/PitchLines.h"

struct TTRHeatParams {
    // Thin-line mode (when linePoints is set): the pitch lines are drawn on a piano-roll grid instead of the heat map.
    // Spectrum mode (when `fine` is set): the whole spectrum on the fine pitch grid, drawn as a heat map (see TTRFineRender.mm).
    // `linePoints` / `lineCounts` then only mark the overtones that "Fundamentals" dims, `data` / `harmonic` the drums that "Hide drums" dims.
    const uint8_t* fine = nullptr;               // frames * kFineBins
    const float* harmonic = nullptr;             // frames * kNumNotes harmonic part of `data`; may be null
    int harmonicFrames = 0;
    const ttr::LinePoint* linePoints = nullptr;  // frames * kLineSlots
    const uint8_t* lineCounts = nullptr;
    bool isLive = false;          // the lines still grow: hide the first points of a line (it may turn out to be noise)
    bool fundamentals = true;     // dim overtones
    bool hideDrums = true;
    float tuningCents = 0.0f;     // the semitone grid is shifted by this offset
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

// The colour ramp (shade 0..255 -> 0xAARRGGBB) and a darker version of a colour; shared by the heat map renderers.
uint32_t TTRHeatColor(uint8_t shade);
uint32_t TTRDarkenColor(uint32_t color, float keep);

// Spectrum mode: the full spectrogram of the fine grid. Returns an owned image (caller releases) or NULL.
CGImageRef TTRRenderFine(const TTRHeatParams& params);

// Returns an owned image (caller releases) or NULL without data.
CGImageRef TTRRenderHeatmap(const TTRHeatParams& params);

// Thin pitch lines on a transparent background (see TTRLineRender.mm); returns an owned image or NULL.
// Only the vertical band that contains lines is rendered: `bandTop` / `bandHeight` (points, from the top of the note range) say where the image belongs.
CGImageRef TTRRenderLines(const TTRHeatParams& params, double* bandTop, double* bandHeight);
// Piano-roll rows behind the line tiles, in view coordinates (flipped context, origin at the top left of the map).
void TTRDrawRowGrid(CGContextRef ctx, const TTRHeatParams& params);
uint32_t TTRRowColor(int pitch);
