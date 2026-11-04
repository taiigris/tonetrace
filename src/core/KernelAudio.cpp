// TonetraceKernel: the audio thread. Records the input, plays the song (original) and the notes (synth) mixed by the
// ORIG/MIDI balance, reports levels. Never allocates, locks or logs (events go through the queue).
#include <mach/mach_time.h>

#include <algorithm>
#include <cmath>

#include "core/DenormalGuard.h"
#include "core/SongAudio.h"
#include "core/TonetraceKernel.h"

namespace ttr {

namespace {
constexpr double kNsPerSecond = 1e9;
constexpr double kMinSoundingSec = 0.02;  // a note that ends sooner than this after a seek is not restarted
}  // namespace

void TonetraceKernel::process(float* const* channels, int numChannels, int frames) {
    if (!channels || numChannels <= 0 || frames <= 0) return;
    ScopedDenormalGuard denormals;
    const uint64_t started = mach_absolute_time();
    if (firstBlock_) {
        firstBlock_ = false;
        events_.push({AudioEventCode::FirstBlock, frames, numChannels, 0.0});
    }
    const bool bypass = param(kParamBypass) >= 0.5f;
    for (int done = 0; done < frames && !bypass;) {  // never more than prepare() allocated for
        const int n = std::min(frames - done, std::max(1, maxFrames_));
        float* chunk[2] = {channels[0] + done, numChannels > 1 ? channels[1] + done : nullptr};
        const int chunkChannels = std::min(numChannels, 2);
        recordBlock(chunk, chunkChannels, n);
        if (param(kParamMuteInput) >= 0.5f)
            for (int c = 0; c < chunkChannels; ++c) std::fill(chunk[c], chunk[c] + n, 0.0f);
        playBlock(chunk, chunkChannels, n);
        updateMeters(chunk, chunkChannels, n);
        done += n;
    }
    audioEpoch_.fetch_add(1, std::memory_order_release);
    const double elapsedNs = double(mach_absolute_time() - started) * nsPerTick_;
    diagnostics_.recordBlock(frames, elapsedNs, double(frames) / sampleRate_ * kNsPerSecond);
}

void TonetraceKernel::recordBlock(float* const* channels, int numChannels, int frames) {
    const bool wantRecord = param(kParamRecord) >= 0.5f;
    if (wantRecord && !wasRecording_) {
        captureL_.reset();
        captureR_.reset();
        wasRecording_ = true;
        wasRecordingFlag_.store(true);
        playing_.store(false);  // recording and playback do not mix
        events_.push({AudioEventCode::RecordStart, int32_t(sampleRate_), 0, 0.0});
    } else if (!wantRecord && wasRecording_) {
        wasRecording_ = false;
        wasRecordingFlag_.store(false);
        recordStops_.fetch_add(1);
        events_.push({AudioEventCode::RecordStop, int32_t(captureL_.size()), 0, double(captureL_.size()) / sampleRate_});
    }
    if (!wasRecording_ || captureL_.capacity() == 0) return;
    const bool fitsLeft = captureL_.write(channels[0], size_t(frames));
    const bool fitsRight = captureR_.write(numChannels > 1 ? channels[1] : channels[0], size_t(frames));
    if (fitsLeft && fitsRight) return;
    params_[kParamRecord].store(0.0f);  // full: stop here, the recording gets transcribed
    wasRecording_ = false;
    wasRecordingFlag_.store(false);
    recordStops_.fetch_add(1);
    events_.push({AudioEventCode::CaptureFull, int32_t(captureL_.size()), 0, 0.0});
}

// Starts the notes of this block at their sample; after a start or seek also the notes that are already sounding.
void TonetraceKernel::scheduleNotes(const PlayTable::View& table, double fromSec, int frames, bool startSounding) {
    if (table.count <= 0) return;
    const double blockSec = double(frames) / sampleRate_;
    if (startSounding) {
        for (int i = table.firstFrom(fromSec - PlayTable::kMaxNoteSec); i < table.count && table.notes[i].startSec < fromSec; ++i) {
            const PlayNote& n = table.notes[i];
            if (n.endSec > fromSec + kMinSoundingSec && n.program != kDrumProgram) synth_.noteOn(n.pitch, n.program, n.gain, n.endSec - fromSec, 0);
        }
    }
    for (int i = table.firstFrom(fromSec); i < table.count && table.notes[i].startSec < fromSec + blockSec; ++i) {
        const PlayNote& n = table.notes[i];
        const int delay = std::clamp(int((n.startSec - fromSec) * sampleRate_), 0, frames - 1);
        synth_.noteOn(n.pitch, n.program, n.gain, n.endSec - n.startSec, delay);
    }
}

void TonetraceKernel::playBlock(float* const* channels, int numChannels, int frames) {
    if (size_t(frames) > synthBuf_.size()) return;  // not prepared yet
    double pos = positionSec_.load(std::memory_order_relaxed);
    bool restart = false;
    const double seekTo = seekRequest_.exchange(-1.0);
    if (seekTo >= 0.0) {
        pos = seekTo;
        synth_.allNotesOff();
        restart = true;
    }
    const bool playing = playing_.load(std::memory_order_relaxed);
    if (playing && !wasPlaying_) restart = true;
    if (!playing && wasPlaying_) synth_.allNotesOff();  // the voices fade out below
    wasPlaying_ = playing;
    if (!playing && !synth_.isActive()) {
        for (auto& level : levels_) level.store(0.0f, std::memory_order_relaxed);
        return;
    }

    std::fill(synthBuf_.begin(), synthBuf_.begin() + frames, 0.0f);
    if (playing) {
        const PlayTable::View table = table_.acquire();
        scheduleNotes(table, pos, frames, restart);
        table_.release();
    }
    synth_.render(synthBuf_.data(), frames);

    const float mix = param(kParamMix), volume = faderToGain(param(kParamVolume));
    const float origGain = std::min(1.0f, 2.0f * (1.0f - mix)) * volume, midiGain = std::min(1.0f, 2.0f * mix) * volume;
    const SongAudio* audio = playing ? playAudio_.load(std::memory_order_acquire) : nullptr;
    const double step = audio ? audio->sampleRate / sampleRate_ : 0.0;
    double src = audio ? pos * audio->sampleRate : 0.0;
    for (int i = 0; i < frames; ++i) {
        float left = 0.0f, right = 0.0f;
        if (audio) {
            sampleAt(*audio, src, &left, &right);
            src += step;
        }
        const float note = synthBuf_[size_t(i)] * midiGain;
        channels[0][i] += left * origGain + note;
        if (numChannels > 1) channels[1][i] += right * origGain + note;
    }
    for (int s = 0; s < NoteSynth::kLevelSlots; ++s) levels_[size_t(s)].store(synth_.level(s) * midiGain, std::memory_order_relaxed);
    if (!playing) return;
    pos += double(frames) / sampleRate_;
    const double length = docLength_.load(std::memory_order_relaxed);
    if (pos >= length) {  // the end: stop there (Play starts again from the beginning)
        pos = length;
        playing_.store(false);
    }
    if (seekRequest_.load(std::memory_order_relaxed) < 0.0) positionSec_.store(pos, std::memory_order_relaxed);
}

void TonetraceKernel::updateMeters(float* const* channels, int numChannels, int frames) {
    float peak = 0.0f;
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(channels[c][i]));
    masterLevel_.store(peak, std::memory_order_relaxed);
}

}  // namespace ttr
