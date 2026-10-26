// Real-time behaviour of the kernel: sample rates, block sizes, offline rendering, setProcessing, parallel instances.
#include <thread>

#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "core/NoteEdit.h"
#include "core/TonetraceKernel.h"

using namespace ttr;

namespace {

constexpr int kMaxBlock = 4096;

std::vector<float> chordAt(double rate, double length) {
    return tsig::synth({{57, 0, length, 0.4f}, {61, 0, length, 0.4f}, {64, 0, length, 0.4f}}, rate, length, true);
}

// A kernel with a short recording, three notes and the preview ready to run.
void prepareWithNotes(TonetraceKernel& k, double rate) {
    k.prepare(rate, kMaxBlock);
    k.setHostTransport(120.0, 0.0);
    CHECK(tk::record(k, chordAt(rate, 1.5)));
    k.editNotes({makeNote(57, 0.0, 0.5, k.timeGrid(), true), makeNote(61, 0.5, 1.0, k.timeGrid(), true), makeNote(64, 1.0, 1.5, k.timeGrid(), true)});
}

}  // namespace

TEST(all_sample_rates_and_variable_block_sizes_record_correctly) {
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        for (int channels : {2, 1}) {
            TonetraceKernel k;
            k.prepare(rate, kMaxBlock);
            const double length = 2.5;
            const auto signal = chordAt(rate, length);
            k.setParam(kParamRecord, 1.0f);
            const tk::Rendered out = tk::runBlocks(k, signal, tk::variableBlocks(), channels);
            CHECK_MSG(out.fed == signal.size() && tk::bitEqual(out.left, signal), "%.0f Hz, %d ch: dry output differs while recording", rate, channels);
            CHECK_MSG(std::fabs(k.recordedSeconds() - length) < 1e-6, "%.0f Hz: captured %.4f s", rate, k.recordedSeconds());
            k.setParam(kParamRecord, 0.0f);
            tk::run(k, tk::silence(1));
            CHECK_MSG(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->hasRecording(); }), "%.0f Hz: analysis did not finish", rate);
            const SnapshotPtr snap = k.snapshot();
            CHECK_MSG(snap && snap->spectrogram && snap->spectrogram->frames > 50 && std::fabs(snap->lengthSec - length) < 0.02, "%.0f Hz: wrong analysis result", rate);
            CHECK(snap && snap->peaks && std::abs(snap->peaks->count() - int(length * kPeaksPerSecond)) <= 3);
            k.deactivate();
        }
    }
}

TEST(zero_frame_blocks_and_parameter_flush_do_nothing) {
    TonetraceKernel k;
    k.prepare(48000.0, kMaxBlock);
    float l[4] = {1, 2, 3, 4}, r[4] = {5, 6, 7, 8};
    float* ch[2] = {l, r};
    k.setParam(kParamRecord, 1.0f);
    for (int i = 0; i < 100; ++i) k.process(ch, 2, 0);
    CHECK(l[0] == 1 && r[3] == 8);
    CHECK(k.recordedSeconds() == 0.0);
    k.process(ch, 2, 4);
    CHECK(k.recordedSeconds() > 0.0);
    k.process(ch, 0, 4);   // no channels
    k.process(ch, 2, -3);  // nonsense
    k.process(ch, 2, kMaxBlock + 1);  // above the prepared maximum: ignored, never overruns the buffers
    k.setParam(kParamRecord, 0.0f);
    k.deactivate();
}

TEST(idle_and_offline_render_are_bit_exact_and_deterministic) {
    const double rate = 48000.0;
    auto signal = chordAt(rate, 1.0);
    signal[10] = -0.0f;  // sign of zero and denormals must survive the pass-through untouched
    signal[11] = 1.0e-40f;
    signal[12] = -1.0e-40f;
    for (bool offline : {false, true}) {
        TonetraceKernel k;
        k.prepare(rate, kMaxBlock);
        k.setOffline(offline);
        const tk::Rendered idle = tk::runBlocks(k, signal, tk::variableBlocks());
        CHECK_MSG(tk::bitEqual(idle.left, signal), "idle %s render is not bit-exact", offline ? "offline" : "realtime");
    }

    // Offline record: pass-through stays exact and two identical renders capture identical audio.
    std::vector<std::vector<int16_t>> clips;
    for (int run = 0; run < 2; ++run) {
        TonetraceKernel k;
        k.prepare(rate, kMaxBlock);
        k.setOffline(true);
        k.setHostTransport(120.0, 4.0);
        k.setParam(kParamRecord, 1.0f);
        const tk::Rendered out = tk::runBlocks(k, signal, tk::variableBlocks());
        CHECK(tk::bitEqual(out.left, signal));
        k.setParam(kParamRecord, 0.0f);
        tk::run(k, tk::silence(1));
        CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->audio; }));
        if (k.snapshot() && k.snapshot()->audio) clips.push_back(k.snapshot()->audio->samples);
    }
    CHECK(clips.size() == 2 && clips[0] == clips[1]);

    // Offline preview: the same state renders the same bits twice, whatever the block pattern of the start.
    TonetraceKernel source;
    prepareWithNotes(source, rate);
    const std::vector<uint8_t> state = source.saveState();
    std::vector<std::vector<float>> renders;
    for (int run = 0; run < 2; ++run) {
        TonetraceKernel k;
        k.prepare(rate, kMaxBlock);
        k.setOffline(true);
        CHECK(k.loadState(state.data(), state.size()));
        k.setParam(kParamPreview, 1.0f);
        const tk::Rendered out = tk::runBlocks(k, std::vector<float>(size_t(rate * 2), 0.0f), tk::variableBlocks());
        renders.push_back(out.left);
    }
    CHECK(renders[0].size() == renders[1].size() && tk::bitEqual(renders[0], renders[1]));
    CHECK(tk::rms(renders[0]) > 0.01);
}

TEST(set_processing_ends_a_recording_and_resets_the_preview) {
    TonetraceKernel k;
    k.prepare(44100.0, kMaxBlock);
    CHECK(!k.diagnostics().snapshot().isProcessing);
    k.setProcessing(true);
    CHECK(k.diagnostics().snapshot().isProcessing);
    k.setParam(kParamRecord, 1.0f);
    tk::run(k, chordAt(44100.0, 1.0));
    CHECK(k.status() == KernelStatus::Recording);
    k.setProcessing(false);  // the transport stopped
    CHECK(k.param(kParamRecord) == 0.0f);
    CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->hasRecording(); }));
    CHECK(!k.diagnostics().snapshot().isProcessing);

    k.setProcessing(true);
    k.editNotes({makeNote(60, 0.0, 0.5, k.timeGrid(), true)});
    k.setParam(kParamPreview, 1.0f);
    tk::run(k, tk::silence(20));
    CHECK(k.playheadSec() > 0.0);
    k.setProcessing(false);
    CHECK(k.playheadSec() == 0.0);  // the preview state starts over
    k.setProcessing(true);
    const auto out = tk::run(k, tk::silence(20));
    CHECK(tk::rms(out) > 0.001);  // and plays again from the start
    k.deactivate();
}

TEST(sample_rate_change_while_recording_discards_the_recording_with_a_notice) {
    TonetraceKernel k;
    k.prepare(44100.0, kMaxBlock);
    k.setParam(kParamRecord, 1.0f);
    tk::run(k, chordAt(44100.0, 0.5));
    CHECK(k.status() == KernelStatus::Recording);
    k.prepare(96000.0, kMaxBlock);  // the host re-activated the plug-in at another rate
    CHECK(k.param(kParamRecord) == 0.0f && k.status() == KernelStatus::Idle);
    Notice notice;
    bool found = false;
    while (k.diagnostics().popNotice(&notice)) found |= notice.text.find("Sample rate changed") != std::string::npos;
    CHECK(found);
    k.deactivate();
}

TEST(eight_instances_run_in_parallel_without_interfering) {
    constexpr int kInstances = 8;
    std::vector<std::unique_ptr<TonetraceKernel>> kernels;
    for (int i = 0; i < kInstances; ++i) kernels.push_back(std::make_unique<TonetraceKernel>());
    std::vector<std::thread> threads;
    std::vector<int> ok(kInstances, 0);
    std::vector<double> lengths(kInstances, 0.0);
    for (int i = 0; i < kInstances; ++i) {
        threads.emplace_back([&, i] {
            TonetraceKernel& k = *kernels[size_t(i)];
            const double rate = i % 2 ? 48000.0 : 44100.0;
            k.prepare(rate, kMaxBlock);
            const double length = 1.0 + 0.25 * i;  // every instance records something different
            const auto signal = chordAt(rate, length);
            k.setParam(kParamRecord, 1.0f);
            const tk::Rendered out = tk::runBlocks(k, signal, tk::variableBlocks(), 2, size_t(i));
            bool exact = tk::bitEqual(out.left, signal);
            k.setParam(kParamRecord, 0.0f);
            tk::run(k, tk::silence(1));
            const bool done = tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->hasRecording(); });
            if (done) {
                lengths[size_t(i)] = k.snapshot()->lengthSec;
                k.editNotes({makeNote(57, 0.0, 0.5, k.timeGrid(), true)});
                k.setParam(kParamPreview, 1.0f);
                exact &= tk::rms(tk::run(k, tk::silence(40))) > 0.001;
            }
            ok[size_t(i)] = exact && done;
        });
    }
    for (std::thread& t : threads) t.join();
    for (int i = 0; i < kInstances; ++i) {
        CHECK_MSG(ok[size_t(i)], "instance %d failed", i);
        CHECK_MSG(std::fabs(lengths[size_t(i)] - (1.0 + 0.25 * i)) < 0.02, "instance %d recorded %.3f s", i, lengths[size_t(i)]);
    }
    for (auto& k : kernels) k->deactivate();
}
