#include "preview/PreviewSynth.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {
constexpr float kAttackSec = 0.008f, kDecaySec = 0.12f, kReleaseSec = 0.14f, kSustain = 0.7f, kStealReleaseSec = 0.002f;
constexpr double kMaxHarmonicFraction = 0.45;  // partials above this fraction of the sample rate are left out (no aliasing)
constexpr float kVoiceGain = 0.16f, kSecondHarmonic = 0.22f;
}  // namespace

void PreviewSynth::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    attackStep_ = 1.0f / (kAttackSec * float(sampleRate_));
    decayStep_ = (1.0f - kSustain) / (kDecaySec * float(sampleRate_));
    releaseStep_ = 1.0f / (kReleaseSec * float(sampleRate_));
    stealStep_ = 1.0f / (kStealReleaseSec * float(sampleRate_));
    allNotesOff();
}

// Voice for a new note: an idle one, else the quietest one that is already fading out.
int PreviewSynth::allocateVoice() {
    int quietest = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].stage == Stage::Idle) return i;
        if (voices_[i].stage == Stage::Release && (voices_[quietest].stage != Stage::Release || voices_[i].level < voices_[quietest].level)) quietest = i;
    }
    return quietest;
}

// More than kMaxNotes sounding notes: the oldest one fades out quickly instead of being cut.
void PreviewSynth::stealOldestNote() {
    int sounding = 0, oldest = -1;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].stage == Stage::Idle || voices_[i].stage == Stage::Release) continue;
        ++sounding;
        if (oldest < 0 || voices_[i].age < voices_[oldest].age) oldest = i;
    }
    if (sounding < kMaxNotes || oldest < 0) return;
    voices_[oldest].stage = Stage::Release;
    voices_[oldest].releaseStep = stealStep_;
}

void PreviewSynth::noteOn(int pitch, float velocity, double durationSec) {
    stealOldestNote();
    Voice& v = voices_[allocateVoice()];
    const double freq = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
    v.phase = 0.0;
    v.phaseInc = 2.0 * M_PI * freq / sampleRate_;
    v.hasSecondHarmonic = 2.0 * freq < kMaxHarmonicFraction * sampleRate_;
    v.level = 0.0f;
    v.releaseStep = releaseStep_;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.samplesLeft = std::max(1L, long(durationSec * sampleRate_));
    v.stage = Stage::Attack;
    v.age = ++counter_;
}

void PreviewSynth::allNotesOff() {
    for (Voice& v : voices_) v = Voice{};
}

bool PreviewSynth::isActive() const {
    return std::any_of(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage != Stage::Idle; });
}

float PreviewSynth::nextEnvelope(Voice& v) const {
    switch (v.stage) {
        case Stage::Attack:
            v.level += attackStep_;
            if (v.level >= 1.0f) { v.level = 1.0f; v.stage = Stage::Decay; }
            break;
        case Stage::Decay:
            v.level -= decayStep_;
            if (v.level <= kSustain) { v.level = kSustain; v.stage = Stage::Sustain; }
            break;
        case Stage::Release:
            v.level -= v.releaseStep;
            if (v.level <= 0.0f) { v.level = 0.0f; v.stage = Stage::Idle; }
            break;
        default: break;
    }
    return v.level;
}

void PreviewSynth::render(float* out, int frames) {
    for (Voice& v : voices_) {
        if (v.stage == Stage::Idle) continue;
        for (int i = 0; i < frames && v.stage != Stage::Idle; ++i) {
            if (v.stage != Stage::Release && --v.samplesLeft <= 0) v.stage = Stage::Release;
            const float env = nextEnvelope(v);
            const float s = float(std::sin(v.phase)) + (v.hasSecondHarmonic ? kSecondHarmonic * float(std::sin(2.0 * v.phase)) : 0.0f);
            out[i] += s * env * v.velocity * kVoiceGain;
            v.phase += v.phaseInc;
            if (v.phase > 2.0 * M_PI) v.phase -= 2.0 * M_PI;
        }
    }
}

}  // namespace ttr
