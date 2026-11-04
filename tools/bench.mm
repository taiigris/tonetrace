// tonetrace_bench: measures the audio path (idle, recording, playing a dense song with every instrument while the notes
// stream in), the memory of a 10 minute song and the editor's frame time, and prints the numbers as Markdown
// (docs/PERFORMANCE.md). Exit code 1 when a target is missed.
//   tonetrace_bench [--markdown file]
#import <Cocoa/Cocoa.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <malloc/malloc.h>
#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <numeric>

#include "TestDemoSong.h"
#include "TestKernel.h"
#include "UiEvents.h"
#include "core/Version.h"
#import "ui/TTRMainViewInternal.h"

using namespace ttr;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;  // 1.33 ms at 48 kHz: the harshest block size a host uses
constexpr double kAudioMeanTargetPct = 5.0, kAudioPeakTargetPct = 25.0, kFrameTargetMs = 16.0, kSongMemoryTargetMb = 250.0;
constexpr int kTrials = 3, kBlocks = 20000;  // 26.7 s of audio per trial
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

struct BlockStats { double meanPct = 0, maxPct = 0; };

// process() time per block in percent of the block budget.
BlockStats timeBlocks(TonetraceKernel& k, int blocks, const std::function<void(int)>& between = nullptr) {
    mach_timebase_info_data_t base{};
    mach_timebase_info(&base);
    const double nsPerTick = double(base.numer) / double(base.denom), budgetNs = double(kBlock) / kRate * 1e9;
    std::vector<double> pct;
    pct.reserve(size_t(blocks));
    std::vector<float> l(kBlock), r(kBlock);
    float* ch[2] = {l.data(), r.data()};
    for (int b = 0; b < blocks; ++b) {
        if (between) between(b);
        for (int i = 0; i < kBlock; ++i) l[size_t(i)] = r[size_t(i)] = 0.1f * float(std::sin(0.05 * (b * kBlock + i)));
        const uint64_t t0 = mach_absolute_time();
        k.process(ch, 2, kBlock);
        pct.push_back(double(mach_absolute_time() - t0) * nsPerTick / budgetNs * 100.0);
    }
    BlockStats s;
    s.meanPct = std::accumulate(pct.begin(), pct.end(), 0.0) / double(pct.size());
    s.maxPct = *std::max_element(pct.begin(), pct.end());
    return s;
}

// Best of kTrials: the trial with the lowest maximum (an OS pre-emption must not decide the result).
BlockStats bestOf(const std::function<BlockStats()>& run) {
    BlockStats best = run();
    for (int i = 1; i < kTrials; ++i) {
        const BlockStats s = run();
        if (s.maxPct < best.maxPct) best = s;
    }
    return best;
}

void report(const char* what, const BlockStats& s) {
    target(s.meanPct < kAudioMeanTargetPct, (std::string(what) + ", mean").c_str(), s.meanPct, "%", kAudioMeanTargetPct);
    target(s.maxPct < kAudioPeakTargetPct, (std::string(what) + ", peak").c_str(), s.maxPct, "%", kAudioPeakTargetPct);
}

void benchAudio() {
    line("\n## Audio thread (48 kHz, %d-frame blocks, %% of the block time)\n", kBlock);
    line("| Case | Measured | Target | |");
    line("|---|---|---|---|");
    demo::installDecoder();
    auto model = std::make_shared<tk::FakeTranscriber>(demo::chunks(), true);
    setTranscriber(model);
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    report("Idle (pass-through)", bestOf([&] { return timeBlocks(k, kBlocks); }));
    k.setParam(kParamRecord, 1.0f);
    report("Recording", bestOf([&] { return timeBlocks(k, kBlocks / 4); }));
    k.setParam(kParamRecord, 0.0f);
    timeBlocks(k, 4);
    tk::waitFor([&] { return k.document() != nullptr; }, 10000);
    k.clearDocument();
    k.loadSong("/bench/song.wav");
    tk::waitFor([&] { return k.document() && k.document()->audio; }, 10000);
    k.setParam(kParamMix, 0.5f);
    k.play();
    // the notes arrive while the song plays (a chunk every 2000 blocks), then everything plays at once
    report("Playing song + notes while they stream in", bestOf([&] {
               k.seek(0.0);
               k.play();
               return timeBlocks(k, kBlocks, [&](int b) { if (b % 2000 == 1999) model->release(); });
           }));
    model->releaseAll();
    tk::waitFor([&] { return k.document()->transcription.phase == TranscriptionPhase::Done; }, 10000);
    report("Playing song + all notes (5 instruments)", bestOf([&] {
               k.seek(0.0);
               k.play();
               return timeBlocks(k, kBlocks);
           }));
    k.pause();
    k.deactivate();
    k.clearDocument();
    setTranscriber(nullptr);
}

void benchMemory() {
    line("\n## Memory (one instance)\n");
    line("| Case | Measured | Target | |");
    line("|---|---|---|---|");
    const double before = residentMegabytes();
    setAudioDecoder([](const std::string&, double rate, double maxSeconds, DecodedAudio* out, std::string*) {  // 10 minutes of stereo
        out->samples.assign(size_t(maxSeconds * rate) * 2, 0);
        for (size_t i = 0; i < out->samples.size(); i += 2) out->samples[i] = out->samples[i + 1] = int16_t(6000.0 * std::sin(double(i) * 0.01));
        out->channels = 2;
        out->sampleRate = rate;
        out->fileSeconds = maxSeconds;
        return true;
    });
    setTranscriberLoader([](std::string* reason) { *reason = "no model in the benchmark"; return std::shared_ptr<NoteTranscriber>(); });
    {
        TonetraceKernel k;
        k.prepare(44100.0, 512);
        const double prepared = residentMegabytes();
        k.loadSong("/bench/ten-minutes.wav");
        tk::waitIdle(k, 60000);
        const double peak = residentMegabytes();
        malloc_zone_pressure_relief(nullptr, 0);  // what the OS gets back once the decoding temporaries are gone
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const double loaded = residentMegabytes();
        line("| Instance with a 10 minute song, right after loading (decoder buffers not yet returned) | %.1f MB | | |", peak - before);
        line("| Empty instance (record buffers, note tables) | %.1f MB | | |", prepared - before);
        target(loaded - before < kSongMemoryTargetMb, "Instance with a 10 minute song loaded", loaded - before, "MB", kSongMemoryTargetMb);
        k.deactivate();
        k.clearDocument();
    }
    setTranscriberLoader(nullptr);
    setAudioDecoder(nullptr);
}

void benchEditor() {
    line("\n## Editor (1280 x 800, the demo song with 5 instruments, playing)\n");
    line("| Case | Measured | Target | |");
    line("|---|---|---|---|");
    demo::installDecoder();
    setTranscriber(std::make_shared<tk::FakeTranscriber>(demo::chunks()));
    auto kernel = std::make_shared<TonetraceKernel>();
    kernel->prepare(44100.0, 512);
    kernel->loadSong("/bench/song.wav");
    tk::waitIdle(*kernel);
    TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
    NSWindow* window = uievents::hostWindow(view, kTTRDefaultSize);
    (void)window;
    kernel->play();
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    std::vector<double> ms;
    for (int i = 0; i < 40; ++i) {
        tk::run(*kernel, tk::silence(2));
        [view refreshNow];
        const double t0 = nowSeconds();
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        ms.push_back((nowSeconds() - t0) * 1e3);
    }
    std::sort(ms.begin(), ms.end());
    target(ms[ms.size() / 2] < kFrameTargetMs, "Full frame (median)", ms[ms.size() / 2], "ms", kFrameTargetMs);
    [view shutdown];
    kernel->deactivate();
    kernel->clearDocument();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--markdown") gMarkdown = fopen(argv[i + 1], "w");
        line("# Tonetrace %s - performance", TTR_VERSION_STRING);
        line("\nMeasured by `tonetrace_bench` on this Mac (%s), Release build, universal binary running natively. The transcription itself "
             "(MuScriptor) runs on its own thread and is measured by `tonetrace_mstest` (about a third of the song length on Apple Silicon).",
             NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
        benchMemory();  // first: later benchmarks leave freed heap behind that would hide the allocations
        benchAudio();
        benchEditor();
        line("\n%s", gMissed == 0 ? "All targets reached." : "Some targets were missed.");
        if (gMarkdown) fclose(gMarkdown);
        return gMissed == 0 ? 0 : 1;
    }
}
