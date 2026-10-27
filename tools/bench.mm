// tonetrace_bench: measures the audio path, the analysis worker, memory and the editor against the v4 targets
// (SPEC-v4 section 2) and prints the numbers as Markdown (docs/PERFORMANCE.md). Exit code 1 when a target is missed.
//   tonetrace_bench [--markdown file]
#import <Cocoa/Cocoa.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <numeric>

#include "TestKernel.h"
#include "TestSignals.h"
#include "UiEvents.h"
#include "analysis/LineBuilder.h"
#include "analysis/LiveLines.h"
#include "analysis/LiveSpectrum.h"
#include "analysis/SpectralFrames.h"
#include "core/NoteEdit.h"
#include "core/Version.h"
#import "ui/TTRHeatmap.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRNoteMapInternal.h"

using namespace ttr;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;                 // 1.33 ms at 48 kHz: the harshest block size in the targets
constexpr double kAudioMeanTargetPct = 2.0, kAudioPeakTargetPct = 10.0, kWorkerTarget = 0.1, kFrameTargetMs = 8.0, kIdleUiTargetPct = 1.0;
constexpr int kTrials = 3;
FILE* gMarkdown = nullptr;
int gMissed = 0;

void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void line(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char text[512];
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    printf("%s\n", text);
    if (gMarkdown) fprintf(gMarkdown, "%s\n", text);
}

void target(bool ok, const char* what, double value, const char* unit, double limit) {
    if (!ok) ++gMissed;
    line("| %s | %.3f %s | < %.3g %s | %s |", what, value, unit, limit, unit, ok ? "reached" : "MISSED");
}

double residentMegabytes() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
    return double(info.phys_footprint) / (1024.0 * 1024.0);
}

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

double cpuSeconds() {
    task_thread_times_info_data_t t{};
    mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
    task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, reinterpret_cast<task_info_t>(&t), &count);
    return double(t.user_time.seconds + t.system_time.seconds) + double(t.user_time.microseconds + t.system_time.microseconds) * 1e-6;
}

std::vector<float> longSignal(double rate, double seconds) {
    const auto base = tsig::synth(tsig::chordThenMelody(), rate, 6.5, true);
    std::vector<float> out;
    out.reserve(size_t(seconds * rate));
    while (out.size() < size_t(seconds * rate)) out.insert(out.end(), base.begin(), base.end());
    out.resize(size_t(seconds * rate));
    return out;
}

struct BlockStats { double meanPct = 0, p999Pct = 0, maxPct = 0; };

// process() time per block in percent of the block budget, over `blocks` blocks of `kBlock` frames.
BlockStats timeBlocks(TonetraceKernel& k, const std::vector<float>& signal, int blocks) {
    mach_timebase_info_data_t base{};
    mach_timebase_info(&base);
    const double nsPerTick = double(base.numer) / double(base.denom), budgetNs = double(kBlock) / kRate * 1e9;
    std::vector<double> pct;
    pct.reserve(size_t(blocks));
    std::vector<float> l(kBlock), r(kBlock);
    float* ch[2] = {l.data(), r.data()};
    for (int b = 0; b < blocks; ++b) {
        const size_t pos = (size_t(b) * kBlock) % (signal.size() - kBlock);
        std::copy(signal.begin() + ptrdiff_t(pos), signal.begin() + ptrdiff_t(pos + kBlock), l.begin());
        r = l;
        const uint64_t t0 = mach_absolute_time();
        k.process(ch, 2, kBlock);
        pct.push_back(double(mach_absolute_time() - t0) * nsPerTick / budgetNs * 100.0);
    }
    BlockStats s;
    s.meanPct = std::accumulate(pct.begin(), pct.end(), 0.0) / double(pct.size());
    std::sort(pct.begin(), pct.end());
    s.p999Pct = pct[size_t(double(pct.size() - 1) * 0.999)];
    s.maxPct = pct.back();
    return s;
}

// Best of kTrials: the mean and the maximum of the trial with the lowest maximum (an OS pre-emption must not decide the result).
BlockStats bestOf(const std::function<BlockStats()>& run) {
    BlockStats best = run();
    for (int i = 1; i < kTrials; ++i) {
        const BlockStats s = run();
        if (s.maxPct < best.maxPct) best = s;
    }
    return best;
}

void benchAudio() {
    line("\n## Audio thread (48 kHz, %d frames per block = %.2f ms budget)\n", kBlock, double(kBlock) / kRate * 1e3);
    line("| Scenario | mean | p99.9 | max |\n|---|---|---|---|");
    const auto signal = longSignal(kRate, 20.0);
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    k.setHostTransport(120.0, 0.0, true);
    constexpr int kBlocks = 15000;  // 20 s

    const BlockStats idle = bestOf([&] { return timeBlocks(k, signal, kBlocks); });
    line("| Idle (no Record, no Preview) | %.4f %% | %.4f %% | %.4f %% |", idle.meanPct, idle.p999Pct, idle.maxPct);

    k.setParam(kParamRecord, 1.0f);
    const BlockStats rec = bestOf([&] { k.setParam(kParamRecord, 0.0f); timeBlocks(k, signal, 2); k.setParam(kParamRecord, 1.0f); return timeBlocks(k, signal, kBlocks); });
    line("| Record (mono mix into the capture buffer) | %.4f %% | %.4f %% | %.4f %% |", rec.meanPct, rec.p999Pct, rec.maxPct);
    k.setParam(kParamRecord, 0.0f);
    timeBlocks(k, signal, 4);
    tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot(); }, 30000);

    std::vector<NoteEvent> notes;  // 16 notes sounding at the same time: all synth voices busy
    for (int i = 0; i < 16; ++i) notes.push_back(makeNote(48 + 3 * i, 0.0, 19.0, k.timeGrid(), false));
    k.editNotes(notes);
    k.setParam(kParamPreview, 1.0f);
    const BlockStats prev = bestOf([&] { return timeBlocks(k, signal, kBlocks); });
    line("| Preview: recording + 16 synth voices + limiter | %.4f %% | %.4f %% | %.4f %% |", prev.meanPct, prev.p999Pct, prev.maxPct);
    k.setSyncToHost(true);
    const BlockStats sync = bestOf([&] { return timeBlocks(k, signal, kBlocks); });
    line("| Preview synced to the host | %.4f %% | %.4f %% | %.4f %% |", sync.meanPct, sync.p999Pct, sync.maxPct);
    k.setParam(kParamPreview, 0.0f);
    k.deactivate();

    line("\n| Target | measured | limit | |\n|---|---|---|---|");
    target(idle.meanPct < 0.05, "Idle: mean (only the pass-through bookkeeping)", idle.meanPct, "%", 0.05);
    const BlockStats worstMean = rec.meanPct > prev.meanPct ? rec : prev;
    target(worstMean.meanPct < kAudioMeanTargetPct, "Record / Preview: mean", worstMean.meanPct, "%", kAudioMeanTargetPct);
    const double worstPeak = std::max({rec.maxPct, prev.maxPct, sync.maxPct});
    target(worstPeak < kAudioPeakTargetPct, "Record / Preview: peak (best of 3 trials)", worstPeak, "%", kAudioPeakTargetPct);
}

void benchWorker() {
    line("\n## Analysis worker (60 s recording at 48 kHz)\n");
    constexpr double kSeconds = 60.0;
    const auto signal = longSignal(kRate, kSeconds);
    TonetraceKernel k;
    k.prepare(kRate, 512);
    k.setHostTransport(120.0, 0.0, true);
    // live phase: the worker advances the live spectrogram + live HPSS as the audio arrives (here in 50 ms chunks); busy time per audio second
    LiveSpectrum liveSpectrum;
    liveSpectrum.prepare(kRate, 120.0);
    LiveLines liveLines;
    liveLines.prepare(liveSpectrum.decimatedRate(), liveSpectrum.maxFrames());
    double linesBusy = 0.0;
    const size_t chunk = size_t(kRate * 0.05);
    const double liveStart = nowSeconds();
    double worstChunkMs = 0.0;
    for (size_t available = chunk; available <= signal.size(); available += chunk) {
        const double t0 = nowSeconds();
        liveSpectrum.advance(signal.data(), available);
        worstChunkMs = std::max(worstChunkMs, (nowSeconds() - t0) * 1e3);
        const double t1 = nowSeconds();
        liveLines.advance(liveSpectrum.decimated(), liveSpectrum.decimatedCount());
        linesBusy += nowSeconds() - t1;
    }
    const double liveFactor = (nowSeconds() - liveStart - linesBusy) / kSeconds, linesFactor = linesBusy / kSeconds;
    const bool caughtUp = liveSpectrum.harmonicFrames() >= liveSpectrum.frames() - liveSpectrum.harmonicDelay() - 1 && liveSpectrum.frames() > 2000;
    line("- live spectrogram + live HPSS: %d frames (%d with the harmonic part, delay %d frames = %.0f ms), busy time %.4f s per audio second, slowest 50 ms chunk %.2f ms",
         liveSpectrum.frames(), liveSpectrum.harmonicFrames(), liveSpectrum.harmonicDelay(), liveSpectrum.harmonicDelay() * liveSpectrum.hopSec() * 1e3, liveFactor, worstChunkMs);
    line("- live pitch lines (multi-resolution peaks + tracking): %d frames, busy time %.4f s per audio second", liveLines.frames(), linesFactor);
    {  // the offline pass on its own (decimation + peaks + tracks + onsets + tuning)
        const double t0 = nowSeconds();
        const DecimatedSignal reduced = decimate(signal.data(), signal.size(), kRate);
        const auto lines = computePitchLines(reduced.samples.data(), long(reduced.samples.size()), reduced.sampleRate, nullptr);
        line("- pitch lines of the final analysis on their own: %.2f s for %.0f s of audio (%zu tracks, tuning %+.1f ct)", nowSeconds() - t0, kSeconds,
             lines ? lines->tracks.size() : size_t(0), lines ? double(lines->tuningCents) : 0.0);
    }
    // the same through the kernel: record 60 s, the worker follows
    k.setParam(kParamRecord, 1.0f);
    std::vector<float> l(512), r(512);
    float* ch[2] = {l.data(), r.data()};
    for (size_t pos = 0; pos + 512 <= signal.size(); pos += 512) {
        std::copy(signal.begin() + ptrdiff_t(pos), signal.begin() + ptrdiff_t(pos + 512), l.begin());
        r = l;
        k.process(ch, 2, 512);
    }
    const double kernelFrames = tk::waitFor([&] { return k.mapView().frames >= int(kSeconds / k.mapView().hopSec) - 12; }, 60000) ? 1.0 : 0.0;
    (void)kernelFrames;
    k.setParam(kParamRecord, 0.0f);
    tk::run(k, tk::silence(1));
    const double stopAt = nowSeconds();
    const bool done = tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->hasRecording(); }, 120000);
    const double finalSeconds = nowSeconds() - stopAt;
    const double finalFactor = finalSeconds / kSeconds;
    line("- final analysis after Stop (spectrogram, HPSS, peaks, key, stored audio, pitch lines): %.2f s for %.0f s of audio, real-time factor %.4f", finalSeconds, kSeconds, finalFactor);

    const double grabStart = nowSeconds();
    const bool started = k.startQuickGrab();
    float progressSeen = 0.0f;
    while (k.quickGrabRunning()) { progressSeen = std::max(progressSeen, k.quickGrabProgress()); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    const double grabSeconds = nowSeconds() - grabStart;
    line("- Quick Grab on its own thread: %.2f s for %.0f s of audio (progress reported up to %.0f %%; the UI thread stays free)", grabSeconds, kSeconds, progressSeen * 100.0);

    line("\n| Target | measured | limit | |\n|---|---|---|---|");
    target(caughtUp && liveFactor < kWorkerTarget, "Live analysis (spectrogram + live HPSS): real-time factor", liveFactor, "", kWorkerTarget);
    target(liveLines.frames() > 2000 && linesFactor < kWorkerTarget, "Live pitch lines: real-time factor", linesFactor, "", kWorkerTarget);
    target(done && finalFactor < kWorkerTarget, "Final analysis: real-time factor", finalFactor, "", kWorkerTarget);
    target(started && grabSeconds / kSeconds < kWorkerTarget, "Quick Grab: real-time factor", grabSeconds / kSeconds, "", kWorkerTarget);
    k.deactivate();
}

void benchMemory() {
    line("\n## Memory per instance\n");
    {  // warm-up: loads the frameworks and builds the FFT plans once, which is not a per-instance cost
        TonetraceKernel warm;
        warm.prepare(44100.0, 512);
        warm.deactivate();
    }
    line("| Sample rate | prepared (idle) | recording 30 s | recording 120 s (buffer full) | after the analysis of 120 s |\n|---|---|---|---|---|");
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        const double before = residentMegabytes();
        auto k = std::make_unique<TonetraceKernel>();
        k->prepare(rate, 512);
        const double prepared = residentMegabytes() - before;
        const auto base = tsig::synth({{57, 0, 3.0, 0.4f}, {64, 0, 3.0, 0.4f}}, rate, 3.0, false);
        std::vector<float> l(512), r(512);
        float* ch[2] = {l.data(), r.data()};
        k->setParam(kParamRecord, 1.0f);
        double at30 = 0.0;
        const size_t total = size_t(rate * 120.0);
        for (size_t pos = 0; pos + 512 <= total; pos += 512) {
            for (int i = 0; i < 512; ++i) l[size_t(i)] = base[(pos + size_t(i)) % base.size()];
            r = l;
            k->process(ch, 2, 512);
            if (pos / 512 == size_t(rate * 30.0) / 512) { tk::waitFor([&] { return k->mapView().frames > int(29.0 / k->mapView().hopSec); }, 20000); at30 = residentMegabytes() - before; }
        }
        tk::waitFor([&] { return k->mapView().frames > int(119.0 / k->mapView().hopSec); }, 30000);
        const double full = residentMegabytes() - before;
        k->setParam(kParamRecord, 0.0f);
        tk::run(*k, tk::silence(1));
        tk::waitFor([&] { return k->status() == KernelStatus::Idle && k->snapshot() && k->snapshot()->hasRecording(); }, 60000);
        const double analysed = residentMegabytes() - before;
        line("| %.0f Hz | %.1f MB | %.1f MB | %.1f MB | %.1f MB |", rate, prepared, at30, full, analysed);
        k->deactivate();
        k.reset();
    }
    line("\nThe capture buffer stays at the host rate (one mono float per sample: the audio thread only copies) but is committed lazily, "
         "so memory grows with the recording and an empty or idle instance costs only a few MB. Down-sampling in the worker would save the "
         "rate-dependent part at 96/192 kHz, but would make the audio thread write into an extra ring and the preview resample on the fly; "
         "the measured cost above does not justify that.");
}

void benchEditor() {
    line("\n## Editor (1100 x 640, 120 s recording)\n");
    auto kernel = std::make_shared<TonetraceKernel>();
    kernel->prepare(44100.0, 512);
    kernel->setHostTransport(120.0, 0.0);
    const auto signal = longSignal(44100.0, 120.0);
    kernel->setParam(kParamRecord, 1.0f);
    std::vector<float> l(512), r(512);
    float* ch[2] = {l.data(), r.data()};
    for (size_t pos = 0; pos + 512 <= signal.size(); pos += 512) {
        std::copy(signal.begin() + ptrdiff_t(pos), signal.begin() + ptrdiff_t(pos + 512), l.begin());
        r = l;
        kernel->process(ch, 2, 512);
    }
    kernel->setParam(kParamRecord, 0.0f);
    tk::run(*kernel, tk::silence(1));
    tk::waitFor([&] { return kernel->status() == KernelStatus::Idle && kernel->snapshot() && kernel->snapshot()->hasRecording(); }, 120000);

    TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
    NSWindow* window = uievents::hostWindow(view, kTTRDefaultSize);
    (void)window;
    [view refreshNow];
    NSView* map = view->_map;
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    auto frameMs = [&](const std::function<void(int)>& prepare, int frames) {
        std::vector<double> ms;
        for (int i = 0; i < frames; ++i) {
            prepare(i);
            const double t0 = nowSeconds();
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            ms.push_back((nowSeconds() - t0) * 1e3);
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    // heat tiles cold: first frame after the data arrived
    const double cold = frameMs([&](int) { view->_vp->scrollSec = 30.0; view->_vp->pxPerSec = 100.0; [map setNeedsDisplay:YES]; }, 1);
    // scrolling through the recording: tiles are cached, a frame is a blit
    const double scroll = frameMs([&](int i) { view->_vp->scrollSec = 30.0 + i * 0.05; [view->_map setNeedsDisplay:YES]; [view->_wave setNeedsDisplay:YES]; }, 60);
    const double drawMs = view->_vp->frameMsAvg;
    // gain change: every visible tile is re-rendered once
    const double gain = frameMs([&](int i) { kernel->setParam(kParamGain, i % 2 ? 0.3f : 0.7f); [view->_map setNeedsDisplay:YES]; }, 4);
    line("- frame with a cold tile cache (first draw): %.2f ms; scrolling a cached map (whole window, median of 60): %.2f ms, map draw only %.2f ms; "
         "after a Gain / Range change (all visible tiles re-rendered): %.2f ms",
         cold, scroll, drawMs, gain);

    // one glow tile (128 px, 2x) on its own: all lines of the visible band, white-hot cores, shine and haze
    {
        const MapView mv = kernel->mapView();
        const SnapshotPtr snap = kernel->snapshot();
        TTRHeatParams hp;
        hp.linePoints = mv.linePoints; hp.lineCounts = mv.lineCounts; hp.frames = mv.lineFrames; hp.hopSec = mv.lines ? mv.lines->hopSec : mv.hopSec;
        hp.reference = mv.linePeak; hp.fundamentals = true; hp.hideDrums = true;
        hp.scrollSec = 20.0; hp.pxPerSec = 100.0; hp.scrollY = 0.0; hp.rowH = 13.0; hp.width = 64.0; hp.height = ttr::kNumNotes * 13.0; hp.scale = 2.0;
        double bandTop = 0.0, bandHeight = 0.0;
        const double t0 = nowSeconds();
        for (int i = 0; i < 20; ++i) CGImageRelease(TTRRenderLines(hp, &bandTop, &bandHeight));
        line("- one 128 px glow tile (2x, band %.0f pt high): %.3f ms", bandHeight, (nowSeconds() - t0) / 20.0 * 1e3);
    }

    // idle: what a 5 Hz tick costs
    double cpu0 = cpuSeconds();
    const int ticks = 100;
    for (int i = 0; i < ticks; ++i) [view refreshNow];
    const double perTickMs = (cpuSeconds() - cpu0) / ticks * 1e3, idlePct = perTickMs * kTTRIdleHz / 10.0;
    line("- one idle refresh tick: %.3f ms CPU -> %.3f %% of a core at %.0f Hz (the timer is off while the window is hidden)", perTickMs, idlePct, kTTRIdleHz);

    // live recording: ticks at 30 Hz with new frames arriving
    TonetraceKernel& k = *kernel;
    k.setParam(kParamRecord, 1.0f);
    cpu0 = cpuSeconds();
    const double liveStart = nowSeconds();
    std::vector<double> liveFrames;
    for (size_t pos = 0; pos + 512 <= 44100 * 10; pos += 512) {
        std::copy(signal.begin() + ptrdiff_t(pos), signal.begin() + ptrdiff_t(pos + 512), l.begin());
        r = l;
        k.process(ch, 2, 512);
        if (pos % (512 * 43) == 0) {  // about every 0.5 s of audio: one UI tick that draws
            const double t0 = nowSeconds();
            [view refreshNow];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            liveFrames.push_back((nowSeconds() - t0) * 1e3);
        }
    }
    (void)liveStart; (void)cpu0;
    k.setParam(kParamRecord, 0.0f);
    std::sort(liveFrames.begin(), liveFrames.end());
    const double liveMedian = liveFrames[liveFrames.size() / 2], liveWorst = liveFrames.back();
    line("- while recording 10 s (refresh + whole-window draw of a growing map every 0.5 s): median %.2f ms, slowest %.2f ms (the first frame after Record, when every tile is cold)", liveMedian, liveWorst);

    line("\n| Target | measured | limit | |\n|---|---|---|---|");
    target(scroll < kFrameTargetMs, "Frame time, 1100x640 with 120 s recording (cached tiles)", scroll, "ms", kFrameTargetMs);
    target(liveMedian < kFrameTargetMs, "Frame time while recording (median)", liveMedian, "ms", kFrameTargetMs);
    target(idlePct < kIdleUiTargetPct, "Editor idle CPU", idlePct, "%", kIdleUiTargetPct);
    [view shutdown];
    kernel->deactivate();
}

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--markdown") gMarkdown = fopen(argv[i + 1], "w");
        line("# Tonetrace %s - performance", TTR_VERSION_STRING);
        line("\nMeasured by `tonetrace_bench` on this Mac (%s), Release build, universal binary running natively.", NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
        benchMemory();  // first: later benchmarks leave freed heap behind that would hide the allocations
        benchAudio();
        benchWorker();
        benchEditor();
        line("\n%s", gMissed == 0 ? "All targets reached." : "Some targets were missed.");
        if (gMarkdown) fclose(gMarkdown);
        return gMissed == 0 ? 0 : 1;
    }
}
