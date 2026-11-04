#include "preview/NoteSynth.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

constexpr float kAttackSec = 0.006f, kDecaySec = 0.12f, kReleaseSec = 0.12f, kSustain = 0.7f, kStealReleaseSec = 0.003f;
constexpr double kMaxHarmonicFraction = 0.45;  // partials above this fraction of the sample rate are left out (no aliasing)
constexpr float kToneGain = 0.14f, kDrumGain = 0.5f, kSecondHarmonic = 0.22f;
constexpr int kDrumProgram = 128;
constexpr double kTwoPi = 2.0 * M_PI;

double midiHz(double pitch) { return 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0); }

// Per-sample multiplier that decays to 1/e after `seconds`.
float decayFactor(double seconds, double rate) { return float(std::exp(-1.0 / std::max(1.0, seconds * rate))); }

}  // namespace

void NoteSynth::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    attackStep_ = 1.0f / (kAttackSec * float(sampleRate_));
    decayStep_ = (1.0f - kSustain) / (kDecaySec * float(sampleRate_));
    releaseStep_ = 1.0f / (kReleaseSec * float(sampleRate_));
    stealStep_ = 1.0f / (kStealReleaseSec * float(sampleRate_));
    reset();
}

float NoteSynth::noise() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(int32_t(seed_)) / 2147483648.0f;
}

// Voice for a new note: an idle one, else the quietest one that is already fading out.
int NoteSynth::allocateVoice() {
    int quietest = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].stage == Stage::Idle) return i;
        if (voices_[i].stage == Stage::Release && (voices_[quietest].stage != Stage::Release || voices_[i].level < voices_[quietest].level)) quietest = i;
    }
    return quietest;
}

// More than kMaxNotes sounding notes: the oldest one fades out quickly instead of being cut.
void NoteSynth::stealOldestNote() {
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

// GM percussion map, reduced to six synthesized sounds.
void NoteSynth::setupDrum(Voice& v, int pitch) {
    const double rate = sampleRate_;
    auto sweepTo = [&](double fromHz, double toHz, double seconds) {
        v.phaseInc = kTwoPi * toHz / rate;
        v.sweep = fromHz / toHz;
        v.sweepEnd = 1.0;
        v.sweepStep = std::pow(1.0 / v.sweep, 1.0 / std::max(1.0, seconds * rate));
    };
    switch (pitch) {
        case 35: case 36:
            v.sound = Sound::Kick; sweepTo(160.0, 48.0, 0.07); v.decay = decayFactor(0.22, rate); v.noiseMix = 0.0f; break;
        case 37: case 38: case 39: case 40:
            v.sound = Sound::Snare; sweepTo(260.0, 185.0, 0.03); v.decay = decayFactor(0.12, rate); v.noiseMix = 0.75f; break;
        case 42: case 44:
            v.sound = Sound::Hat; v.decay = decayFactor(0.035, rate); v.noiseMix = 1.0f; break;
        case 46:
            v.sound = Sound::Hat; v.decay = decayFactor(0.22, rate); v.noiseMix = 1.0f; break;
        case 41: case 43: case 45: case 47: case 48: case 50: {
            const double hz = 70.0 + (pitch - 41) * 14.0;
            v.sound = Sound::Tom; sweepTo(hz * 1.6, hz, 0.05); v.decay = decayFactor(0.25, rate); v.noiseMix = 0.1f; break;
        }
        case 49: case 52: case 55: case 57:
            v.sound = Sound::Cymbal; v.decay = decayFactor(0.9, rate); v.noiseMix = 1.0f; break;
        case 51: case 53: case 59:
            v.sound = Sound::Cymbal; v.decay = decayFactor(0.45, rate); v.noiseMix = 0.85f; v.phaseInc = kTwoPi * 3100.0 / rate; break;
        default:
            v.sound = Sound::Click; v.phaseInc = kTwoPi * midiHz(pitch + 12) / rate; v.decay = decayFactor(0.06, rate); v.noiseMix = 0.5f; break;
    }
}

void NoteSynth::noteOn(int pitch, int program, float gain, double durationSec, int delayFrames) {
    if (!(gain > 0.0f)) return;
    stealOldestNote();
    Voice& v = voices_[allocateVoice()];
    v = Voice{};
    v.gain = std::min(gain, 4.0f);
    v.slot = std::clamp(program + 1, 0, kLevelSlots - 1);
    v.delay = std::max(0, delayFrames);
    v.stage = v.delay > 0 ? Stage::Waiting : Stage::Attack;
    v.age = ++counter_;
    v.releaseStep = releaseStep_;
    if (program == kDrumProgram) {
        setupDrum(v, pitch);
        v.level = 1.0f;  // percussive: full level at once, then the exponential decay
        v.samplesLeft = long(2.0 * sampleRate_);
        return;
    }
    const double freq = midiHz(pitch);
    v.sound = Sound::Tone;
    v.phaseInc = kTwoPi * freq / sampleRate_;
    v.hasSecondHarmonic = 2.0 * freq < kMaxHarmonicFraction * sampleRate_;
    v.samplesLeft = std::max(1L, long(durationSec * sampleRate_));
}

void NoteSynth::allNotesOff() {
    for (Voice& v : voices_) {
        if (v.stage == Stage::Idle) continue;
        if (v.stage == Stage::Waiting) { v.stage = Stage::Idle; continue; }
        v.stage = Stage::Release;
        v.releaseStep = stealStep_;
    }
}

void NoteSynth::reset() {
    for (Voice& v : voices_) v = Voice{};
    levels_.fill(0.0f);
}

bool NoteSynth::isActive() const {
    return std::any_of(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage != Stage::Idle; });
}

float NoteSynth::nextEnvelope(Voice& v) const {
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

float NoteSynth::toneSample(Voice& v) {
    if (v.stage != Stage::Release && --v.samplesLeft <= 0) v.stage = Stage::Release;
    const float env = nextEnvelope(v);
    const float s = float(std::sin(v.phase)) + (v.hasSecondHarmonic ? kSecondHarmonic * float(std::sin(2.0 * v.phase)) : 0.0f);
    v.phase += v.phaseInc;
    if (v.phase > kTwoPi) v.phase -= kTwoPi;
    return s * env * kToneGain;
}

float NoteSynth::drumSample(Voice& v) {
    if (v.stage == Stage::Release) {
        v.level -= v.releaseStep;
    } else {
        v.level *= v.decay;
        if (--v.samplesLeft <= 0) v.stage = Stage::Release;
    }
    if (v.level <= 1e-4f) { v.level = 0.0f; v.stage = Stage::Idle; return 0.0f; }
    const float n = noise();
    const float highPassed = n - v.hpState;  // brighter noise for hats and cymbals
    v.hpState = n;
    const float noisePart = (v.sound == Sound::Hat || v.sound == Sound::Cymbal) ? 0.5f * highPassed : n;
    float tone = 0.0f;
    if (v.phaseInc > 0.0) {
        tone = float(std::sin(v.phase));
        v.phase += v.phaseInc * v.sweep;
        if (v.phase > kTwoPi) v.phase -= kTwoPi;
        if (v.sweep > v.sweepEnd) v.sweep = std::max(v.sweepEnd, v.sweep * v.sweepStep);
    }
    return (tone * (1.0f - v.noiseMix) + noisePart * v.noiseMix) * v.level * kDrumGain;
}

void NoteSynth::render(float* out, int frames) {
    levels_.fill(0.0f);
    for (Voice& v : voices_) {
        if (v.stage == Stage::Idle) continue;
        float peak = 0.0f;
        for (int i = 0; i < frames && v.stage != Stage::Idle; ++i) {
            if (v.stage == Stage::Waiting) {
                if (v.delay > 0) { --v.delay; continue; }
                v.stage = Stage::Attack;
            }
            const float s = (v.sound == Sound::Tone ? toneSample(v) : drumSample(v)) * v.gain;
            out[i] += s;
            peak = std::max(peak, std::fabs(s));
        }
        levels_[size_t(v.slot)] = std::max(levels_[size_t(v.slot)], peak);
    }
}

}  // namespace ttr
