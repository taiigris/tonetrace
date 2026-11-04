// Per-instrument mix of the MIDI playback (NeuralNote's sidebar strips): mute, solo and a fader per instrument.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>

namespace ttr {

struct InstrumentMix {
    bool mute = false;
    bool solo = false;
    float gain = 0.8f;  // fader 0..1 (0.8 = 0 dB)
    bool operator==(const InstrumentMix& o) const { return mute == o.mute && solo == o.solo && gain == o.gain; }
};

constexpr float kUnityFader = 0.8f;
constexpr float kFaderRangeDb = 48.0f;  // fader 0..0.8 spans -48..0 dB, 0.8..1 spans 0..+6 dB
constexpr float kFaderBoostDb = 6.0f;

// Fader position -> dB (0 -> -inf).
inline float faderToDb(float fader) {
    const float f = std::clamp(fader, 0.0f, 1.0f);
    if (f <= 0.0f) return -INFINITY;
    if (f >= kUnityFader) return (f - kUnityFader) / (1.0f - kUnityFader) * kFaderBoostDb;
    return (f / kUnityFader - 1.0f) * kFaderRangeDb;
}

inline float faderToGain(float fader) {
    const float db = faderToDb(fader);
    return std::isfinite(db) ? std::pow(10.0f, db / 20.0f) : 0.0f;
}

class MixerState {
public:
    InstrumentMix get(int program) const {
        const auto it = mix_.find(program);
        return it == mix_.end() ? InstrumentMix{} : it->second;
    }
    // A new state with one instrument changed (this one stays as it is).
    MixerState with(int program, const InstrumentMix& m) const {
        MixerState out = *this;
        out.mix_[program] = sanitized(m);
        return out;
    }
    bool anySolo() const {
        return std::any_of(mix_.begin(), mix_.end(), [](const auto& e) { return e.second.solo; });
    }
    bool isAudible(int program) const {
        const InstrumentMix m = get(program);
        return !m.mute && (!anySolo() || m.solo);
    }
    // Linear gain of the instrument's notes (0 when muted or another instrument is soloed).
    float gainOf(int program) const { return isAudible(program) ? faderToGain(get(program).gain) : 0.0f; }
    const std::map<int, InstrumentMix>& entries() const { return mix_; }
    bool operator==(const MixerState& o) const { return mix_ == o.mix_; }

private:
    static InstrumentMix sanitized(InstrumentMix m) {
        m.gain = std::isfinite(m.gain) ? std::clamp(m.gain, 0.0f, 1.0f) : kUnityFader;
        return m;
    }
    std::map<int, InstrumentMix> mix_;
};

}  // namespace ttr
