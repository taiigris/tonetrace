// Polyphonic preview synth (16 sounding notes): soft sine + light 2nd harmonic (band-limited), linear ADSR,
// click-free voice stealing (the stolen voice releases in ~2 ms while the new note starts on a spare voice). Real-time safe.
#pragma once
#include <array>

namespace ttr {

class PreviewSynth {
public:
    static constexpr int kMaxNotes = 16;           // sounding notes (not counting voices that are fading out)
    static constexpr int kVoices = kMaxNotes + 8;  // spare voices for fast releases

    void prepare(double sampleRate);
    void noteOn(int pitch, float velocity, double durationSec);
    void allNotesOff();
    bool isActive() const;
    // Adds the mixed voices into `out` (mono).
    void render(float* out, int frames);

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    struct Voice {
        Stage stage = Stage::Idle;
        double phase = 0.0, phaseInc = 0.0;
        float level = 0.0f, velocity = 0.0f, releaseStep = 0.001f;
        bool hasSecondHarmonic = true;
        long samplesLeft = 0;
        unsigned long age = 0;
    };
    int allocateVoice();
    void stealOldestNote();
    float nextEnvelope(Voice& v) const;

    std::array<Voice, kVoices> voices_{};
    double sampleRate_ = 44100.0;
    unsigned long counter_ = 0;
    float attackStep_ = 0.01f, decayStep_ = 0.001f, releaseStep_ = 0.001f, stealStep_ = 0.01f;
};

}  // namespace ttr
