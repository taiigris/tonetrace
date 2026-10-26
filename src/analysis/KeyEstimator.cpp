#include "analysis/KeyEstimator.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {
const float kMajorProfile[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
const float kMinorProfile[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

float correlation(const float* a, const float* b) {
    float ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
    ma /= 12; mb /= 12;
    float num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da <= 0 || db <= 0) ? 0.0f : num / std::sqrt(da * db);
}
}  // namespace

std::array<float, 12> pitchClassWeights(const std::vector<NoteEvent>& notes) {
    std::array<float, 12> w{};
    for (const NoteEvent& n : notes)
        w[((n.pitch % 12) + 12) % 12] += n.strength * float(n.endSec - n.startSec);
    const float mx = *std::max_element(w.begin(), w.end());
    if (mx > 0) for (float& v : w) v /= mx;
    return w;
}

KeyResult estimateKey(const std::array<float, 12>& w) {
    KeyResult best;
    if (*std::max_element(w.begin(), w.end()) <= 0.0f) return best;
    best.score = -2.0f;
    for (int root = 0; root < 12; ++root) {
        float rotated[12];
        for (int i = 0; i < 12; ++i) rotated[i] = w[(root + i) % 12];
        const float maj = correlation(rotated, kMajorProfile);
        const float min = correlation(rotated, kMinorProfile);
        if (maj > best.score) best = {root, false, maj};
        if (min > best.score) best = {root, true, min};
    }
    return best;
}

const char* pitchClassName(int pc) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((pc % 12) + 12) % 12];
}

std::string keyName(const KeyResult& key) {
    if (key.root < 0) return "-";
    return std::string(pitchClassName(key.root)) + (key.minor ? " minor" : " major");
}

}  // namespace ttr
