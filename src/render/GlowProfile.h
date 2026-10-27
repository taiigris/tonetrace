// Glow look of the note map: a colour map (intensity -> black, violet, magenta, red, orange, yellow, white) and the
// vertical, gaussian profile of a line (white-hot core, shine, faint haze). Both are tables computed once, so the tile
// renderer only multiplies and adds. Pure C++ (no CoreGraphics) so the tests can check it.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace ttr {

constexpr int kGlowLutSize = 256;
constexpr int kGlowClasses = 8;   // strength classes: stronger lines get a wider shine
constexpr int kGlowPhases = 4;    // sub-pixel positions of a profile
constexpr float kGlowCoreAmp = 1.0f, kGlowShineAmp = 0.58f, kGlowHazeAmp = 0.30f;
constexpr float kGlowCoreSigmaPt = 1.5f;  // FWHM of the core about 3.5 points

using GlowLut = std::array<uint32_t, kGlowLutSize>;  // 0x00RRGGBB

// Warm map for the notes: black, dark violet, violet/magenta, red, orange, yellow, white. Input 0..1 (clamped).
const GlowLut& glowWarmLut();
// Cool map for weak partials and overtones: black, dark blue-violet, pale blue.
const GlowLut& glowCoolLut();
uint32_t glowLookup(const GlowLut& lut, float intensity);

// Intensity of a line of strength `brightness` (0..1) at vertical distance d (pixels) from its centre.
float glowProfileAt(float d, float brightness, double scale);

// Gauss profiles for one device scale: per strength class and sub-pixel phase a table of 2r+1 weights.
class GlowProfiles {
public:
    explicit GlowProfiles(double scale);
    static int classOf(float brightness);
    static int phaseOf(float fraction);  // fraction of the pixel (0..1) -> phase
    int radius(int strengthClass) const { return radius_[size_t(strengthClass)]; }
    // weight of pixel (floor(centre) + k) for k = -radius .. radius, scaled to the line's brightness 1
    const float* taps(int strengthClass, int phase) const { return taps_[size_t(strengthClass * kGlowPhases + phase)].data(); }
    int coolRadius() const { return coolRadius_; }
    const float* coolTaps(int phase) const { return cool_[size_t(phase)].data(); }
    int maxRadius() const { return maxRadius_; }

private:
    std::vector<float> taps_[kGlowClasses * kGlowPhases];
    std::vector<float> cool_[kGlowPhases];
    int radius_[kGlowClasses] = {};
    int coolRadius_ = 0, maxRadius_ = 0;
};

}  // namespace ttr
