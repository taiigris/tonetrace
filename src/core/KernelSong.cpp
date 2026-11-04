// TonetraceKernel: 4.7 song import. A whole audio file becomes the document (like a finished recording), then MuScriptor
// transcribes every instrument; the notes carry their instrument, so the MIDI export writes one track per instrument.
#include <malloc/malloc.h>

#include <chrono>
#include <cstdio>

#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/Logger.h"
#include "core/RecordingBuilder.h"
#include "core/SongImport.h"
#include "core/TonetraceKernel.h"

namespace ttr {

namespace {

constexpr int kMaxNamesInNotice = 6;
constexpr int kSecondsPerMinute = 60;

std::string minutesText(double seconds) {
    char text[16];
    const int total = int(seconds + 0.5);
    std::snprintf(text, sizeof text, "%d:%02d", total / kSecondsPerMinute, total % kSecondsPerMinute);
    return text;
}

// "Bass, Drums, Piano and 3 more".
std::string instrumentList(const std::vector<NoteEvent>& notes) {
    const std::vector<InstrumentSummary> all = instrumentsIn(notes);
    std::string text;
    int shown = 0;
    for (const InstrumentSummary& s : all) {
        if (shown == kMaxNamesInNotice) break;
        text += (shown++ > 0 ? ", " : "") + instrumentName(s.program);
    }
    if (int(all.size()) > shown) text += " and " + std::to_string(int(all.size()) - shown) + " more";
    return text;
}

}  // namespace

bool TonetraceKernel::startSongImport(const std::string& path) {
    if (status() != KernelStatus::Idle || !isSongFile(path)) return false;
    bool expected = false;
    if (!grabRunning_.compare_exchange_strong(expected, true)) return false;
    if (grabThread_.joinable()) grabThread_.join();
    grabControl_.cancel.store(false);
    grabControl_.report(0.0f);
    importStage_.store(int(SongStage::Loading));
    importRunning_.store(true);
    const double bpm = hostBpm();
    const uint64_t seq = recordSeq_.load();
    grabThread_ = std::thread([this, path, bpm, seq] {
        runSongImport(path, bpm, seq);
        malloc_zone_pressure_relief(nullptr, 0);  // the decoded song and the analysis temporaries go back to the OS
        importRunning_.store(false);
        grabRunning_.store(false);
    });
    return true;
}

void TonetraceKernel::runSongImport(const std::string& path, double bpm, uint64_t recordSeq) {
    const auto started = std::chrono::steady_clock::now();
    const std::string name = fileNameOf(path);
    DecodedAudio audio;
    std::string reason;
    if (!decodeAudioFile(path, &audio, &reason)) {
        logWarn(instanceId_, "song import failed: %s", reason.c_str());
        notice("\"" + name + "\" could not be loaded: " + reason, NoticeLevel::Error);
        return;
    }
    if (grabControl_.cancelled()) return;
    if (audio.truncated)
        notice("\"" + name + "\" is " + minutesText(audio.fileSeconds) + " long - only the first " + minutesText(kMaxSongSeconds) + " are used",
               NoticeLevel::Warning);

    importStage_.store(int(SongStage::Analysing));
    Recording recording = analyseAudio(audio.mono.data(), audio.mono.size(), audio.sampleRate, kMaxSongSeconds, &grabControl_);
    if (grabControl_.cancelled()) return;
    if (!recording.spectrogram) {
        notice("\"" + name + "\" is silent - nothing to transcribe", NoticeLevel::Warning);
        return;
    }
    recording.bpm = bpm;
    recording.startPpq = 0.0;
    recording.sourceName = name;
    const SnapshotPtr song = publishSong(recording, recordSeq);
    if (!song) return;
    logInfo(instanceId_, "song loaded: %s, %.1f s", name.c_str(), recording.lengthSec);

    importStage_.store(int(SongStage::Transcribing));
    const std::vector<NoteEvent> notes = transcribeSong(song, audio.mono, audio.sampleRate);
    if (grabControl_.cancelled()) {
        notice("Transcription cancelled - the song stays loaded, Grab transcribes it", NoticeLevel::Info);
        return;
    }
    const int added = mergeGrabbed(song, notes);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    logInfo(instanceId_, "song import done: %d notes, %zu instruments in %.0f ms", added, instrumentsIn(notes).size(), ms);
    if (added == 0) {
        notice("No notes found in \"" + name + "\"", NoticeLevel::Warning);
        return;
    }
    notice(std::to_string(added) + " notes - " + instrumentList(notes) + ". Drag MIDI gives one track per instrument", NoticeLevel::Info);
}

// The song replaces the document as one undo step; null when a recording started meanwhile.
SnapshotPtr TonetraceKernel::publishSong(const Recording& recording, uint64_t recordSeq) {
    SnapshotPtr song;
    {
        std::lock_guard<std::mutex> guard(mutateMutex_);
        if (recordSeq_.load() != recordSeq || status() != KernelStatus::Idle) return nullptr;
        const SnapshotPtr old = snapshot();
        if (old) history_.recordDocument(old);
        song = makeSnapshot({}, recording, old ? old->version + 1 : 1);
        playhead_.store(0.0);
        previewStart_.store(0.0);
        setPreviewLoop(0.0, 0.0);
        publishLocked(song);
    }
    restoreCaptureFromSnapshot();  // the preview plays the song (as far as the capture buffer reaches)
    return song;
}

// Every instrument with MuScriptor; without the model the Classic Quick Grab (one track) and a notice why.
std::vector<NoteEvent> TonetraceKernel::transcribeSong(const SnapshotPtr& song, const std::vector<float>& mono, double rate) {
    MuScriptorGrab grab = muscriptorTranscribe(mono, rate, song->bpm, &grabControl_);
    if (grab.ok || grabControl_.cancelled()) return std::move(grab.notes);
    logWarn(instanceId_, "song import: MuScriptor unavailable (%s), using Classic", grab.reason.c_str());
    notice("MuScriptor unavailable (" + grab.reason + ") - Classic transcribed the song without instruments", NoticeLevel::Warning);
    grabControl_.report(0.0f);
    return quickGrabWithEngine(*song, param(kParamQuickGrab), filterSettings(), tuningOffsetCents(), GrabEngine::Classic, &grabControl_).notes;
}

}  // namespace ttr
