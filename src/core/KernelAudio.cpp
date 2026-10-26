// TonetraceKernel: the audio thread. No allocation, no locks, no syscalls, no logging: only atomics,
// preallocated buffers and the wait-free event queue (the worker turns events into log lines).
#include <mach/mach_time.h>

#include <algorithm>
#include <cmath>

#include "core/DenormalGuard.h"
#include "core/Logger.h"
#include "core/TonetraceKernel.h"

namespace ttr {

namespace {
constexpr float kAuditionVelocity = 0.8f;
constexpr double kAuditionSeconds = 0.35;
constexpr double kMinLoopSec = 0.05;
constexpr long kFadeSamples = 96;           // click-free start / stop / loop wrap of the preview (about 2 ms)
constexpr float kLimiterKnee = 0.8f;        // soft limiter on the preview bus: transparent below, never above 1.0
constexpr double kSyncToleranceSec = 0.02;  // drift against the host position before the preview jumps
constexpr double kNsPerSecond = 1e9;

inline float softLimit(float x) {
    const float a = std::fabs(x);
    if (a <= kLimiterKnee) return x;
    const float y = kLimiterKnee + (1.0f - kLimiterKnee) * std::tanh((a - kLimiterKnee) / (1.0f - kLimiterKnee));
    return x < 0.0f ? -y : y;
}
}  // namespace

bool TonetraceKernel::previewActive() const { return previewing() && param(kParamRecord) < 0.5f && param(kParamBypass) < 0.5f; }

// Song position (seconds on the recording's timeline) of the host's current block.
double TonetraceKernel::syncedPosition() const {
    const double beats = hostBeat_.load(std::memory_order_relaxed) - docStartPpq_.load(std::memory_order_relaxed);
    return beats * 60.0 / docBpm_.load(std::memory_order_relaxed);
}

// Plays [playSample_, playSample_ + frames) of the preview timeline (sample + notes), wrapping at the region end.
// With fadeOutOnly the block is the short tail after the preview was switched off.
void TonetraceKernel::playBlock(int frames, bool fadeOutOnly) {
    const NoteTable::View view = table_.acquire();
    const long timeline = long(view.loopSec * sampleRate_);
    long regionStart = 0, regionEnd = timeline;
    double loopA = 0.0, loopB = 0.0;
    if (previewLoop(&loopA, &loopB)) {
        regionStart = long(loopA * sampleRate_);
        regionEnd = std::min(timeline, long(loopB * sampleRate_));
    } else {
        regionStart = std::clamp(long(previewStart_.load() * sampleRate_), 0L, timeline);
    }
    if (regionEnd - regionStart < long(kMinLoopSec * sampleRate_)) { table_.release(); return; }

    if (syncToHost_.load(std::memory_order_relaxed) && !fadeOutOnly) {  // follow the host song position
        const long length = regionEnd - regionStart;
        const long target = long(syncedPosition() * sampleRate_) - regionStart;
        const long wrapped = regionStart + (target % length + length) % length;
        long drift = std::labs(wrapped - playSample_);
        drift = std::min(drift, length - drift);
        if (drift > long(kSyncToleranceSec * sampleRate_)) {
            playSample_ = wrapped;
            fadeInDone_ = 0;
            synth_.allNotesOff();
        }
    }

    const float* audio = capture_.data();
    const long audioCount = long(capture_.size());
    int done = 0;
    while (done < frames) {
        if (playSample_ < regionStart || playSample_ >= regionEnd) {
            playSample_ = regionStart;
            if (fadeOutOnly) break;
        }
        const int segment = int(std::min<long>(frames - done, regionEnd - playSample_));
        const double from = double(playSample_) / sampleRate_, to = double(playSample_ + segment) / sampleRate_;
        for (int i = 0; i < view.count; ++i) {
            const PreviewNote& n = view.notes[i];
            if (n.startSec >= from && n.startSec < to) synth_.noteOn(n.pitch, n.velocity, double(n.endSec - n.startSec));
        }
        for (int i = 0; i < segment; ++i) {
            const long idx = playSample_ + i;
            sampleBuf_[size_t(done + i)] = idx < audioCount ? audio[idx] : 0.0f;
        }
        if (synth_.isActive()) synth_.render(synthBuf_.data() + done, segment);
        for (int i = 0; i < segment; ++i) {  // fades: loop-wrap edges, start of playback, stop
            const long pos = playSample_ + i;
            const long k = done + i;  // sample index inside this block
            float g = std::min(1.0f, float(regionEnd - pos) / float(kFadeSamples));
            if (fadeOutOnly) {
                g = std::min(g, 1.0f - float(fadeOutDone_ + k) / float(kFadeSamples));
            } else {
                g = std::min(g, float(pos - regionStart) / float(kFadeSamples));
                g = std::min(g, float(fadeInDone_ + k) / float(kFadeSamples));
            }
            g = std::max(g, 0.0f);
            synthBuf_[size_t(k)] *= g;
            sampleBuf_[size_t(k)] *= g;
        }
        playSample_ += segment;
        done += segment;
    }
    if (!fadeOutOnly) fadeInDone_ = std::min<long>(kFadeSamples, fadeInDone_ + frames);
    playhead_.store(double(playSample_) / sampleRate_, std::memory_order_relaxed);
    table_.release();
}

// Fills synthBuf_ / sampleBuf_ with the preview and auditions of this block. False: nothing to mix.
bool TonetraceKernel::renderPreview(int frames) {
    const bool host = hostPlaying_.load(std::memory_order_relaxed);
    const bool sync = syncToHost_.load(std::memory_order_relaxed);
    const bool wanted = previewActive();
    const bool active = wanted && (!sync || (host && syncedPosition() >= 0.0));
    int pitch = 0;
    while (table_.popAudition(&pitch)) synth_.noteOn(pitch, kAuditionVelocity, kAuditionSeconds);

    if (active) {
        tailPending_ = false;
        std::fill_n(synthBuf_.begin(), frames, 0.0f);
        std::fill_n(sampleBuf_.begin(), frames, 0.0f);
        if (!wasPreviewing_) {
            playSample_ = sync ? long(std::max(0.0, syncedPosition()) * sampleRate_) : long(previewStart_.load() * sampleRate_);
            fadeInDone_ = 0;
            synth_.allNotesOff();
        }
        wasPreviewing_ = true;
        playBlock(frames, false);
        return true;
    }
    if (wasPreviewing_) {  // switched off: a short fade-out (possibly spread over several blocks) instead of a cut
        wasPreviewing_ = false;
        tailPending_ = true;
        fadeOutDone_ = 0;
    }
    if (tailPending_) {
        std::fill_n(synthBuf_.begin(), frames, 0.0f);
        std::fill_n(sampleBuf_.begin(), frames, 0.0f);
        const int n = int(std::min<long>(frames, kFadeSamples - fadeOutDone_));
        playBlock(n, true);
        fadeOutDone_ += n;
        if (fadeOutDone_ >= kFadeSamples) {
            tailPending_ = false;
            synth_.allNotesOff();
        }
        return true;
    }
    if (playhead_.load(std::memory_order_relaxed) != 0.0) playhead_.store(0.0, std::memory_order_relaxed);
    if (!synth_.isActive()) return false;  // idle: the host's signal is left untouched
    std::fill_n(synthBuf_.begin(), frames, 0.0f);
    std::fill_n(sampleBuf_.begin(), frames, 0.0f);
    synth_.render(synthBuf_.data(), frames);  // auditions
    return true;
}

// Adds the preview to the host's signal with smoothed Sound / Mix / Solo gains and a soft limiter on the preview bus.
void TonetraceKernel::mixPreview(float* const* channels, int numChannels, int frames) {
    const float mix = param(kParamMix);
    const float sampleTarget = std::min(1.0f, 2.0f * (1.0f - mix));
    const float noteTarget = std::min(1.0f, 2.0f * mix) * param(kParamSoundVolume);
    const float dryTarget = param(kParamSolo) >= 0.5f && wasPreviewing_ ? 0.0f : 1.0f;
    if (!gainsValid_) { sampleGain_ = sampleTarget; noteGain_ = noteTarget; gainsValid_ = true; }
    const float inv = 1.0f / float(frames);
    const float sampleStep = (sampleTarget - sampleGain_) * inv, noteStep = (noteTarget - noteGain_) * inv, dryStep = (dryTarget - dryGain_) * inv;
    for (int i = 0; i < frames; ++i) {
        sampleGain_ += sampleStep;
        noteGain_ += noteStep;
        dryGain_ += dryStep;
        const float preview = softLimit(synthBuf_[size_t(i)] * noteGain_ + sampleBuf_[size_t(i)] * sampleGain_);
        for (int c = 0; c < numChannels; ++c) channels[c][i] = channels[c][i] * dryGain_ + preview;
    }
    sampleGain_ = sampleTarget;
    noteGain_ = noteTarget;
    dryGain_ = dryTarget;
}

void TonetraceKernel::trackBlock(int frames, int numChannels) {
    if (lastBlock_ == 0) events_.push({AudioEventCode::FirstBlock, frames, numChannels, 0.0});
    else if (frames > maxSeenBlock_) events_.push({AudioEventCode::BlockSize, frames, maxSeenBlock_, 0.0});
    lastBlock_ = frames;
    maxSeenBlock_ = std::max(maxSeenBlock_, frames);
}

void TonetraceKernel::process(float* const* channels, int numChannels, int frames) {
    if (numChannels <= 0 || frames <= 0 || frames > maxFrames_) return;
    const ScopedDenormalGuard denormals;
    const uint64_t startTicks = mach_absolute_time();
    trackBlock(frames, numChannels);

    const bool bypass = param(kParamBypass) >= 0.5f;
    const bool recording = param(kParamRecord) >= 0.5f && !bypass;
    if (recording && !wasRecording_) {
        capture_.reset();
        captureFull_ = false;
        recBpm_ = hostBpm_.load(std::memory_order_relaxed);
        recBeat_ = hostBeat_.load(std::memory_order_relaxed);
        recordSeq_.fetch_add(1);
        status_.store(int(KernelStatus::Recording));
        events_.push({AudioEventCode::RecordStart, int32_t(sampleRate_), 0, recBeat_});
    } else if (!recording && wasRecording_) {
        analysisSeq_.fetch_add(1);
        status_.store(int(KernelStatus::Analysing));
        events_.push({AudioEventCode::RecordStop, int32_t(capture_.size()), 0, double(capture_.size()) / sampleRate_});
    }
    wasRecording_ = recording;

    if (recording && !captureFull_) {
        const float scale = 1.0f / float(numChannels);
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < numChannels; ++c) sum += channels[c][i];
            mono_[size_t(i)] = sum * scale;
        }
        captureFull_ = !capture_.write(mono_.data(), size_t(frames));
        if (captureFull_) events_.push({AudioEventCode::CaptureFull, int32_t(capture_.size()), 0, 0.0});
    }

    if (!bypass && renderPreview(frames)) mixPreview(channels, numChannels, frames);
    else gainsValid_ = false;

    const double budgetNs = double(frames) / sampleRate_ * kNsPerSecond;
    diagnostics_.recordBlock(frames, double(mach_absolute_time() - startTicks) * nsPerTick_, budgetNs);
}

}  // namespace ttr
