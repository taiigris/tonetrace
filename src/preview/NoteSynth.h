// Real-time synth for the MIDI playback: soft tones for the instruments, a small synthesized GM kit for the drums.
// Notes start sample-accurately inside a block (delay), carry their instrument's gain and report a level per
// instrument for the sidebar meters. Never allocates or locks.
#pragma once
#include <array>
#include <cstdint>

namespace ttr {

class NoteSynth {
public:
    static constexpr int kMaxNotes = 48;           // sounding notes (not counting voices that are fading out)
    static constexpr int kVoices = kMaxNotes + 16;  // spare voices for fast releases
    static constexpr int kLevelSlots = 130;         // program + 1 (index 0: notes without an instrument)

    void prepare(double sampleRate);
    // A note `delayFrames` into the next render call; program 128 plays the GM drum sound of `pitch`.
    void noteOn(int pitch, int program, float gain, double durationSec, int delayFrames = 0);
    void allNotesOff();  // fast fade (no click)
    void reset();        // silence at once (not playing anyway)
    bool isActive() const;
    // Adds the voices into `out` (mono). Updates the per-instrument peak levels of this block.
    void render(float* out, int frames);
    // Peak of the last render call for an instrument slot (program + 1).
    float level(int slot) const { return slot >= 0 && slot < kLevelSlots ? levels_[size_t(slot)] : 0.0f; }

private:
    enum class Stage : uint8_t { Idle, Waiting, Attack, Decay, Sustain, Release };
    enum class Sound : uint8_t { Tone, Kick, Snare, Hat, Tom, Cymbal, Click };
    struct Voice {
        Stage stage = Stage::Idle;
        Sound sound = Sound::Tone;
        double phase = 0.0, phaseInc = 0.0, sweep = 1.0, sweepEnd = 1.0, sweepStep = 1.0;
        float level = 0.0f, gain = 0.0f, releaseStep = 0.001f, decay = 1.0f, noiseMix = 0.0f, hpState = 0.0f;
        bool hasSecondHarmonic = true;
        long samplesLeft = 0;
        int delay = 0;
        int slot = 0;
        unsigned long age = 0;
    };
    int allocateVoice();
    void stealOldestNote();
    float nextEnvelope(Voice& v) const;
    float toneSample(Voice& v);
    float drumSample(Voice& v);
    void setupDrum(Voice& v, int pitch);
    float noise();

    std::array<Voice, kVoices> voices_{};
    std::array<float, kLevelSlots> levels_{};
    double sampleRate_ = 44100.0;
    unsigned long counter_ = 0;
    uint32_t seed_ = 0x9E3779B9u;
    float attackStep_ = 0.01f, decayStep_ = 0.001f, releaseStep_ = 0.001f, stealStep_ = 0.01f;
};

}  // namespace ttr
