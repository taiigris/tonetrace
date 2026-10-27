// Look of the glow map: colour map (black -> violet -> red -> orange -> yellow -> white) and the gauss profile of a line.
#include <cmath>

#include "TestUtil.h"
#include "render/GlowProfile.h"

using namespace ttr;

namespace {
int red(uint32_t c) { return int((c >> 16) & 0xFF); }
int green(uint32_t c) { return int((c >> 8) & 0xFF); }
int blue(uint32_t c) { return int(c & 0xFF); }
int luma(uint32_t c) { return red(c) * 3 + green(c) * 6 + blue(c); }
}  // namespace

TEST(glow_colormap_runs_from_black_through_violet_red_orange_yellow_to_white) {
    const GlowLut& lut = glowWarmLut();
    CHECK(lut[0] == 0u);
    const uint32_t violet = glowLookup(lut, 0.20f), red45 = glowLookup(lut, 0.45f), orange = glowLookup(lut, 0.60f), yellow = glowLookup(lut, 0.80f), white = glowLookup(lut, 1.0f);
    CHECK(blue(violet) > red(violet) && red(violet) > green(violet));          // violet
    CHECK(red(red45) > 180 && green(red45) < 60 && blue(red45) < 80);            // red
    CHECK(red(orange) > 220 && green(orange) > 70 && green(orange) < 140 && blue(orange) < 60);
    CHECK(red(yellow) > 240 && green(yellow) > 180 && blue(yellow) < 100);       // yellow
    CHECK(red(white) >= 250 && green(white) >= 245 && blue(white) >= 220);       // white-hot
    for (size_t i = 1; i < lut.size(); ++i) CHECK_MSG(luma(lut[i]) >= luma(lut[i - 1]), "luminance falls at %zu", i);
}

TEST(glow_colormap_clamps_and_the_cool_map_is_pale_blue) {
    const GlowLut& warm = glowWarmLut();
    CHECK(glowLookup(warm, -3.0f) == warm[0] && glowLookup(warm, 7.0f) == warm.back() && glowLookup(warm, NAN) == warm[0]);
    const uint32_t weak = glowLookup(glowCoolLut(), 0.8f);
    CHECK(blue(weak) > red(weak) && blue(weak) > green(weak) && blue(weak) > 120);
    CHECK(glowCoolLut()[0] == 0u);
}

TEST(glow_profile_is_gaussian_symmetric_and_brighter_in_the_core) {
    for (double scale : {1.0, 2.0}) {
        const GlowProfiles profiles(scale);
        for (int cls = 0; cls < kGlowClasses; ++cls) {
            const int r = profiles.radius(cls);
            for (int phase = 0; phase < kGlowPhases; ++phase) {
                const float* t = profiles.taps(cls, phase);
                int peak = 0;
                for (int k = 0; k <= 2 * r; ++k) if (t[k] > t[peak]) peak = k;
                CHECK_MSG(std::abs(peak - r) <= 1, "class %d phase %d peaks at %d of %d", cls, phase, peak, 2 * r);
                CHECK(t[0] < t[peak] * 0.2f && t[2 * r] < t[peak] * 0.2f);  // falls off towards the edges
            }
        }
        // the two centre phases mirror each other
        const float* a = profiles.taps(7, 0);
        const float* b = profiles.taps(7, kGlowPhases - 1);
        const int r = profiles.radius(7);
        for (int k = 0; k <= 2 * r; ++k) CHECK(std::fabs(a[k] - b[2 * r - k]) < 1e-5f);
    }
}

TEST(glow_core_is_about_two_points_and_the_shine_stays_within_eight) {
    const double scale = 2.0;
    const float peak = glowProfileAt(0.0f, 1.0f, scale);
    CHECK(peak > 1.0f);                                    // a strong line burns out to white at its centre
    float fwhm = 0.0f;                                     // width of the core in points
    for (float d = 0.0f; d < 20.0f * float(scale); d += 0.05f) if (glowProfileAt(d, 1.0f, scale) > kGlowCoreAmp * 0.5f + kGlowShineAmp * 0.6f) fwhm = d;
    CHECK(fwhm / float(scale) * 2.0f > 2.5f && fwhm / float(scale) * 2.0f < 6.0f);
    CHECK(glowProfileAt(12.0f * float(scale), 1.0f, scale) < 0.08f);  // shine and haze of a strong line have faded 12 points out
    CHECK(0.15f * glowProfileAt(0.0f, 0.15f, scale) < 0.3f * peak);    // intensity = brightness * profile: a weak line is dim
    const float d = 4.0f * float(scale);                                // narrower for weak lines
    CHECK(glowProfileAt(d, 1.0f, scale) / peak > glowProfileAt(d, 0.15f, scale) / glowProfileAt(0.0f, 0.15f, scale));
    // radius of the tables is within the 6-8 point limit
    CHECK(GlowProfiles(scale).maxRadius() <= int(18.0 * scale));
}

TEST(glow_profile_classes_and_phases_map_into_range) {
    CHECK(GlowProfiles::classOf(0.0f) == 0 && GlowProfiles::classOf(1.0f) == kGlowClasses - 1 && GlowProfiles::classOf(5.0f) == kGlowClasses - 1);
    CHECK(GlowProfiles::classOf(-1.0f) == 0);
    CHECK(GlowProfiles::phaseOf(0.0f) == 0 && GlowProfiles::phaseOf(0.999f) == kGlowPhases - 1 && GlowProfiles::phaseOf(1.0f) == kGlowPhases - 1);
}
