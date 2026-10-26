// The audio path must not allocate, lock or log: malloc counter, and the logger never being written by the audio thread.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include "AllocCounter.h"
#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "core/Logger.h"
#include "core/NoteEdit.h"
#include "core/TonetraceKernel.h"

using namespace ttr;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;

std::string makeTempDir() {
    char templ[] = "/tmp/tonetrace_test_XXXXXX";
    return mkdtemp(templ);
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Runs `blocks` blocks of 64 frames on the calling thread with the allocation counter armed.
long countAllocations(TonetraceKernel& k, int blocks) {
    std::vector<float> l(kBlock, 0.1f), r(kBlock, 0.05f);
    float* ch[2] = {l.data(), r.data()};
    for (int i = 0; i < 4; ++i) k.process(ch, 2, kBlock);  // warm-up: first-use initialisation does not count
    tt::AllocCounter::start();
    for (int i = 0; i < blocks; ++i) {
        k.setParam(kParamSoundVolume, 0.5f + 0.001f * float(i % 7));  // parameter changes arrive on the audio thread too
        k.process(ch, 2, kBlock);
    }
    return tt::AllocCounter::stop();
}

}  // namespace

TEST(allocation_counter_works) {
    tt::AllocCounter::start();
    void* a = std::malloc(1000);
    void* b = std::calloc(10, 100);
    asm volatile("" : : "r"(a), "r"(b) : "memory");  // keep the compiler from eliding the pair
    std::free(a);
    std::free(b);
    std::vector<int>* v = new std::vector<int>(1000);
    asm volatile("" : : "r"(v) : "memory");
    delete v;
    CHECK_MSG(tt::AllocCounter::stop() >= 6, "counter saw too few calls");  // otherwise the zero results below would prove nothing
}

TEST(audio_path_makes_no_allocations_idle_recording_and_preview) {
    TonetraceKernel k;
    k.prepare(kRate, 512);
    k.setHostTransport(120.0, 0.0, true);
    CHECK(tk::record(k, tsig::synth({{57, 0, 3.0, 0.4f}, {64, 0, 3.0, 0.4f}}, kRate, 3.0, true)));  // blocks of 512 are fine here
    k.editNotes({makeNote(57, 0.0, 1.0, k.timeGrid(), true), makeNote(64, 1.0, 2.0, k.timeGrid(), true), makeNote(69, 2.0, 3.0, k.timeGrid(), true)});

    CHECK_MSG(countAllocations(k, 2000) == 0, "idle block allocated");

    k.setParam(kParamRecord, 1.0f);
    CHECK_MSG(countAllocations(k, 2000) == 0, "recording allocated");
    k.setParam(kParamRecord, 0.0f);
    tk::run(k, tk::silence(2));
    CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle; }));

    k.setParam(kParamPreview, 1.0f);
    k.setParam(kParamSolo, 1.0f);
    CHECK_MSG(countAllocations(k, 4000) == 0, "preview allocated");  // 5 s: wraps the loop a few times
    k.audition(60);
    CHECK_MSG(countAllocations(k, 300) == 0, "audition allocated");
    k.setSyncToHost(true);
    CHECK_MSG(countAllocations(k, 1000) == 0, "synced preview allocated");
    k.setParam(kParamPreview, 0.0f);
    CHECK_MSG(countAllocations(k, 200) == 0, "preview stop (fade-out) allocated");
    k.setParam(kParamBypass, 1.0f);
    CHECK_MSG(countAllocations(k, 200) == 0, "bypass allocated");
    k.deactivate();
}

TEST(logger_writes_and_rotates_at_one_megabyte_keeping_three_files) {
    const std::string dir = makeTempDir();
    Logger::instance().configure(dir, 1024 * 1024, 3);
    const std::string line(500, 'x');
    for (int i = 0; i < 9000; ++i) logInfo(7, "line %d %s", i, line.c_str());  // ~4.5 MB
    struct stat st{};
    CHECK(stat((dir + "/Tonetrace.log").c_str(), &st) == 0 && st.st_size > 0 && st.st_size <= 1024 * 1024);
    CHECK(stat((dir + "/Tonetrace.1.log").c_str(), &st) == 0 && st.st_size > 900 * 1024 && st.st_size <= 1024 * 1024);
    CHECK(stat((dir + "/Tonetrace.2.log").c_str(), &st) == 0 && st.st_size > 900 * 1024 && st.st_size <= 1024 * 1024);
    CHECK(stat((dir + "/Tonetrace.3.log").c_str(), &st) != 0);  // 1 MB x 3 files, the oldest is dropped
    const std::string current = slurp(dir + "/Tonetrace.log");
    CHECK(current.find("[#7] INFO  line 8999") != std::string::npos);  // newest line is in the current file
    CHECK(current.size() > 20 && current[4] == '-' && current[10] == 'T' && current[19] == '.');  // ISO timestamp with milliseconds
    Logger::instance().configure("", 1024 * 1024, 3);
    std::system(("rm -rf " + dir).c_str());
}

TEST(logger_is_written_by_the_worker_never_by_the_audio_thread) {
    const std::string dir = makeTempDir();
    Logger::instance().configure(dir, 1024 * 1024, 3);
    std::thread::id audioThread;
    {
        TonetraceKernel k;
        k.prepare(kRate, 512);
        k.setHostTransport(120.0, 2.0);
        std::thread audio([&] {
            audioThread = std::this_thread::get_id();
            k.setParam(kParamRecord, 1.0f);
            tk::run(k, tsig::synth({{57, 0, 1.0, 0.4f}}, kRate, 1.0, true));
            k.setParam(kParamRecord, 0.0f);
            tk::run(k, tk::silence(2));
        });
        audio.join();
        CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot(); }));
        k.deactivate();
    }
    const std::string log = slurp(dir + "/Tonetrace.log");
    CHECK(log.find("record start at 48000 Hz") != std::string::npos);  // events of the audio thread reached the log ...
    CHECK(log.find("record stop") != std::string::npos);
    CHECK(log.find("first block: 512 frames") != std::string::npos);
    CHECK(log.find("analysis:") != std::string::npos);
    CHECK(log.find("Tonetrace 4.1.0") != std::string::npos);
    bool audioWroteLog = false;
    for (std::thread::id id : Logger::instance().writerThreads()) audioWroteLog |= id == audioThread;
    CHECK(!audioWroteLog);  // ... but only through the worker / main thread
    Logger::instance().configure("", 1024 * 1024, 3);
    std::system(("rm -rf " + dir).c_str());
}
