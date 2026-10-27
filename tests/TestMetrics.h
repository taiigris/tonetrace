// Accuracy metrics of detected notes against ground truth (unit tests and tools/accuracy.cpp).
//  noteF1     onset within +-50 ms, pitch exact, every note used once
//  durationF1 recall: truth notes whose duration is covered >= 70 % by detected pieces of the same pitch;
//             precision: detected pieces with >= 50 % of their length inside a same-pitch truth note (+-50 ms)
//  frag       detected pieces per truth note, over the truth notes that were found at all
//  shortShare share of the detected pieces shorter than 120 ms
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace tmetric {

struct Note { int pitch; double start, end; };

struct Score { double noteF1 = 0, durationF1 = 0, frag = 0, shortShare = 0; int pieces = 0; };

constexpr double kOnsetTol = 0.050, kCoverage = 0.70, kPieceInside = 0.50, kShort = 0.120;

inline double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }

inline double f1Of(double precision, double recall) { return precision + recall > 0 ? 2.0 * precision * recall / (precision + recall) : 0.0; }

inline Score score(const std::vector<Note>& truth, const std::vector<Note>& found) {
    Score s;
    s.pieces = int(found.size());
    if (found.empty() || truth.empty()) return s;
    std::vector<char> used(found.size(), 0);
    int hits = 0;
    for (const Note& t : truth)
        for (size_t i = 0; i < found.size(); ++i)
            if (!used[i] && found[i].pitch == t.pitch && std::fabs(found[i].start - t.start) <= kOnsetTol) { used[i] = 1; ++hits; break; }
    s.noteF1 = f1Of(double(hits) / double(found.size()), double(hits) / double(truth.size()));

    int covered = 0, foundTruth = 0, associated = 0;
    for (const Note& t : truth) {
        double cover = 0.0;
        int pieces = 0;
        for (const Note& d : found) {
            if (d.pitch != t.pitch) continue;
            const double ov = overlap(d.start, d.end, t.start - kOnsetTol, t.end + kOnsetTol);
            if (ov < kPieceInside * (d.end - d.start)) continue;
            ++pieces;
            cover += overlap(d.start, d.end, t.start, t.end);  // pieces of one pitch do not overlap
        }
        covered += cover >= kCoverage * (t.end - t.start) ? 1 : 0;
        if (pieces > 0) { ++foundTruth; associated += pieces; }
    }
    int correct = 0, shortPieces = 0;
    for (const Note& d : found) {
        shortPieces += d.end - d.start < kShort ? 1 : 0;
        for (const Note& t : truth)
            if (t.pitch == d.pitch && overlap(d.start, d.end, t.start - kOnsetTol, t.end + kOnsetTol) >= kPieceInside * (d.end - d.start)) { ++correct; break; }
    }
    s.durationF1 = f1Of(double(correct) / double(found.size()), double(covered) / double(truth.size()));
    s.frag = foundTruth > 0 ? double(associated) / double(foundTruth) : 0.0;
    s.shortShare = double(shortPieces) / double(found.size());
    return s;
}

}  // namespace tmetric
