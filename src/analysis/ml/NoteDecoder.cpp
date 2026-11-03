#include "analysis/ml/NoteDecoder.h"

#include <algorithm>

namespace ttr {

namespace {

constexpr int kMinSplitFrames = 4;     // an onset peak splits a held note only after this many frames of it
constexpr int kOnsetNearFrames = 2;    // an onset peak this close to the start of the next run marks a re-attack
constexpr float kCentsPerBin = 100.0f / 3.0f;

struct Span { int first, last; };

std::vector<char> onsetPeaks(const Activations& a, int key, float threshold) {
    std::vector<char> peaks(size_t(a.frames), 0);
    for (int t = 0; t < a.frames; ++t) {
        const float v = a.onsetAt(t, key);
        const float before = t > 0 ? a.onsetAt(t - 1, key) : 0.0f;
        const float after = t + 1 < a.frames ? a.onsetAt(t + 1, key) : 0.0f;
        peaks[size_t(t)] = v >= threshold && v >= before && v > after ? 1 : 0;
    }
    return peaks;
}

std::vector<Span> hysteresisSpans(const Activations& a, int key, const std::vector<char>& peaks, const std::vector<char>& splits, const DecoderSettings& s) {
    std::vector<Span> spans;
    int start = -1;
    for (int t = 0; t < a.frames; ++t) {
        const float v = a.noteAt(t, key);
        const bool canStart = v >= s.onThreshold || (peaks[size_t(t)] && v >= s.offThreshold);
        if (start < 0) {
            if (canStart) start = t;
            continue;
        }
        if (v < s.offThreshold) {
            spans.push_back({start, t - 1});
            start = canStart ? t : -1;
        } else if (splits[size_t(t)] && t - start >= kMinSplitFrames) {
            spans.push_back({start, t - 1});  // a new attack of the same key: repeated note
            start = t;
        }
    }
    if (start >= 0) spans.push_back({start, a.frames - 1});
    return spans;
}

bool hasOnsetNear(const std::vector<char>& peaks, int frame) {
    for (int t = std::max(0, frame - kOnsetNearFrames); t <= std::min(int(peaks.size()) - 1, frame + kOnsetNearFrames); ++t)
        if (peaks[size_t(t)]) return true;
    return false;
}

std::vector<Span> bridgeSpans(const Activations& a, int key, const std::vector<char>& peaks, const DecoderSettings& s, std::vector<Span> spans) {
    if (s.bridgeFrames <= 0 || spans.size() < 2) return spans;
    std::vector<Span> out{spans.front()};
    for (size_t i = 1; i < spans.size(); ++i) {
        Span& prev = out.back();
        const Span next = spans[i];
        const int gap = next.first - prev.last - 1;
        bool isBridged = gap <= s.bridgeFrames && !hasOnsetNear(peaks, next.first);
        for (int t = prev.last + 1; isBridged && t < next.first; ++t) isBridged = a.noteAt(t, key) >= s.bridgeFloor;
        if (isBridged) prev.last = next.last;
        else out.push_back(next);
    }
    return out;
}

DecodedNote describe(const Activations& a, int key, Span span) {
    DecodedNote n;
    n.midi = kNeuralLowestMidi + key;
    n.first = span.first;
    n.last = span.last;
    double sum = 0.0, weight = 0.0, offset = 0.0;
    const int centre = 3 * key + 1;
    const bool hasContour = a.contour.size() >= size_t(a.frames) * kNeuralContourBins;
    for (int t = span.first; t <= span.last; ++t) {
        sum += a.noteAt(t, key);
        if (!hasContour) continue;
        for (int d = -1; d <= 1; ++d) {
            const float c = a.contourAt(t, centre + d);
            weight += c;
            offset += double(c) * d;
        }
    }
    n.strength = float(sum / double(span.last - span.first + 1));
    n.cents = weight > 0.0 ? float(offset / weight) * kCentsPerBin : 0.0f;
    return n;
}

}  // namespace

std::vector<DecodedNote> decodeNotes(const Activations& acts, const DecoderSettings& settings) {
    std::vector<DecodedNote> notes;
    if (acts.frames <= 0 || acts.note.size() < size_t(acts.frames) * kNeuralKeys || acts.onset.size() < acts.note.size()) return notes;
    for (int key = 0; key < kNeuralKeys; ++key) {
        const std::vector<char> peaks = onsetPeaks(acts, key, settings.onsetThreshold);
        const std::vector<char> splits = onsetPeaks(acts, key, std::max(settings.onsetThreshold, settings.splitThreshold));
        const std::vector<Span> spans = bridgeSpans(acts, key, splits, settings, hysteresisSpans(acts, key, peaks, splits, settings));
        for (const Span& s : spans)
            if (s.last - s.first + 1 >= settings.minFrames) notes.push_back(describe(acts, key, s));
    }
    std::sort(notes.begin(), notes.end(), [](const DecodedNote& x, const DecodedNote& y) { return x.first != y.first ? x.first < y.first : x.midi < y.midi; });
    return notes;
}

}  // namespace ttr
