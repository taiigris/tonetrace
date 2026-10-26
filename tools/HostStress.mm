// Host test, part 2: what a real DAW does to a plug-in. Controller state (Live's "not implemented" messages), setProcessing,
// every sample rate and block size, offline rendering, parallel instances and an editor that opens and closes under load.
#include "HostHarness.h"
#include "TestSignals.h"
#include "core/NoteEdit.h"

IMPLEMENT_FUNKNOWN_METHODS(hosttest::TestFrame, Steinberg::IPlugFrame, Steinberg::IPlugFrame::iid)

namespace hosttest {

namespace {

constexpr int kEditorCycles = 200;
constexpr int kParallelInstances = 8;

bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

// The v3 state format (no header checksum, no sync flag): signature, version 3, 13 parameters, "no snapshot".
std::vector<uint8_t> legacyV3State(float gain) {
    std::vector<uint8_t> bytes = {0x54, 0x54, 0x52, 0x31, 3, 0, 0, 0};
    for (int i = 0; i < ttr::kNumParams; ++i) {
        const float v = i == ttr::kParamGain ? gain : ttr::paramSpec(i).defaultValue;
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + 4);
    }
    bytes.push_back(0);
    return bytes;
}

MemoryStream* streamOf(const std::vector<uint8_t>& bytes) {
    auto* s = new MemoryStream();
    s->write(const_cast<uint8_t*>(bytes.data()), int32(bytes.size()), nullptr);
    rewind(*s);
    return s;
}

// ---------------------------------------------------------------- Live's log: controller state and setProcessing

void testControllerStateAndProcessing(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& cls) {
    printf("9. Controller state, setProcessing and old (v3) projects (the messages in Live's Log.txt)\n");
    Instance a;
    if (!a.open(factory, cls)) { check(false, "instance opens"); return; }
    check(a.processor->setProcessing(false) == kResultOk && a.processor->setProcessing(true) == kResultOk, "setProcessing returns kResultOk (was \"not implemented\")");

    IPtr<IPlugView> view = owned(a.controller->createView(ViewType::kEditor));
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1100, 640) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    IPtr<TestFrame> frame = owned(new TestFrame);
    view->setFrame(frame);
    view->attached((__bridge void*)window.contentView, kPlatformTypeNSView);
    ViewRect bigger(0, 0, 1234, 701);
    view->onSize(&bigger);
    a.controller->setParamNormalized(ttr::kParamGain, 0.3);
    a.controller->setParamNormalized(ttr::kParamSpeed, toNormalizedSpeed(4.0));
    a.controller->setParamNormalized(ttr::kParamSnap, 0.25);
    a.controller->setParamNormalized(ttr::kParamHideDrums, 0.0);
    pumpRunLoop(0.1);

    MemoryStream controllerState;
    check(a.controller->getState(&controllerState) == kResultOk && controllerState.getSize() > 16, "controller getState works (was \"couldn't get controller state: not implemented\")");
    MemoryStream componentState;
    check(a.component->getState(&componentState) == kResultOk, "component getState works");
    view->removed();
    view->setFrame(nullptr);
    view.reset();

    Instance b;
    if (!b.open(factory, cls)) { check(false, "second instance opens"); return; }
    rewind(componentState);
    check(b.component->setState(&componentState) == kResultOk, "component setState");
    rewind(componentState);
    check(b.controller->setComponentState(&componentState) == kResultOk, "controller setComponentState from the processor state");
    check(std::fabs(b.controller->getParamNormalized(ttr::kParamGain) - 0.3) < 1e-6, "setComponentState syncs the controller (Gain)");
    rewind(controllerState);
    check(b.controller->setState(&controllerState) == kResultOk, "controller setState works");
    check(std::fabs(b.controller->getParamNormalized(ttr::kParamSnap) - 0.25) < 1e-6 && b.controller->getParamNormalized(ttr::kParamHideDrums) == 0.0 &&
              std::fabs(b.controller->getParamNormalized(ttr::kParamSpeed) - toNormalizedSpeed(4.0)) < 1e-6,
          "controller state restores Snap, Hide drums and Speed");
    b.process(nullptr);
    check(b.state().params[ttr::kParamGain] == 0.3f && b.state().params[ttr::kParamHideDrums] == 0.0f, "the restored view settings reach the processor");
    IPtr<IPlugView> restoredView = owned(b.controller->createView(ViewType::kEditor));
    ViewRect size;
    restoredView->getSize(&size);
    check(size.right - size.left == 1234 && size.bottom - size.top == 701, "the window size is restored from the controller state");
    restoredView.reset();

    MemoryStream hostile;
    const uint8_t junk[] = {1, 2, 3};
    hostile.write(const_cast<uint8_t*>(junk), 3, nullptr);
    rewind(hostile);
    check(b.controller->setState(&hostile) != kResultOk, "a corrupt controller state is rejected cleanly");

    Instance c;
    if (c.open(factory, cls)) {
        const std::vector<uint8_t> v3 = legacyV3State(0.8f);
        IPtr<MemoryStream> s1 = owned(streamOf(v3));
        check(c.component->setState(s1) == kResultOk, "a v3 project loads into the processor");
        IPtr<MemoryStream> s2 = owned(streamOf(v3));
        check(c.controller->setComponentState(s2) == kResultOk && std::fabs(c.controller->getParamNormalized(ttr::kParamGain) - 0.8) < 1e-6, "and into the controller");
        std::vector<uint8_t> broken = v3;
        broken.resize(20);
        IPtr<MemoryStream> s3 = owned(streamOf(broken));
        check(c.component->setState(s3) != kResultOk, "a truncated state is refused without a crash");
        std::vector<float> out;
        check(c.process(nullptr, &out) && c.state().params[ttr::kParamGain] == 0.8f, "the instance keeps working");
        c.close();
    }
    a.close();
    b.close();
}

// ---------------------------------------------------------------- sample rates, block sizes, offline

void testRatesBlocksAndOffline(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& cls) {
    printf("10. Sample rates, variable block sizes (incl. 0 frames), mono / stereo and offline rendering\n");
    static const int32 sizes[] = {1, 2, 3, 7, 64, 127, 0, 480, 512, 333, 1024, 0, 5};
    bool allExact = true, allCaptured = true;
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        for (int channels : {2, 1}) {
            Instance inst;
            if (!inst.open(factory, cls, rate, 1024, kRealtime, channels)) { allExact = false; continue; }
            const double seconds = 2.0;
            const auto signal = tsig::synth({{57, 0, seconds, 0.4f}, {64, 0, seconds, 0.4f}}, rate, seconds, true);
            inst.setParam(ttr::kParamRecord, 1.0);
            std::vector<float> out;
            size_t pos = 0, turn = 0;
            while (pos < signal.size()) {
                const int32 n = std::min<int32>(sizes[turn++ % (sizeof sizes / sizeof sizes[0])], int32(signal.size() - pos));
                allExact &= inst.process(&signal[pos], &out, nullptr, n);  // 0 frames: parameter flush only
                pos += size_t(n);
            }
            allExact &= bitEqual(out, signal);
            inst.setParam(ttr::kParamRecord, 0.0);
            inst.process(nullptr, nullptr, nullptr, 64);
            ttr::DecodedState st;
            allCaptured &= waitFor([&] { inst.process(nullptr, nullptr, nullptr, 64); st = inst.state(); return st.snapshot && st.snapshot->hasRecording(); });
            allCaptured &= st.snapshot && std::fabs(st.snapshot->lengthSec - seconds) < 0.05;
            inst.close();
        }
    }
    check(allExact, "44.1 / 48 / 88.2 / 96 / 192 kHz, mono and stereo, odd blocks: output stays bit-exact and process() always succeeds");
    check(allCaptured, "every combination records the full length");

    // offline render (Live freeze / export): same bits twice, pass-through exact, record allowed
    std::vector<float> renders[2];
    std::vector<int16_t> clips[2];
    const auto signal = tsig::synth(tsig::chordThenMelody(), kRate, 3.0, true);
    for (int run = 0; run < 2; ++run) {
        Instance inst;
        if (!inst.open(factory, cls, kRate, kBlock, kOffline)) { check(false, "offline instance opens"); return; }
        inst.isPlaying = true;
        inst.setParam(ttr::kParamRecord, 1.0);
        for (size_t b = 0; (b + 1) * kBlock <= signal.size(); ++b) inst.process(&signal[b * kBlock], &renders[run]);
        inst.setParam(ttr::kParamRecord, 0.0);
        inst.process(nullptr);
        ttr::DecodedState st;
        waitFor([&] { inst.process(nullptr); st = inst.state(); return st.snapshot && st.snapshot->audio; });
        if (st.snapshot && st.snapshot->audio) clips[run] = st.snapshot->audio->samples;
        inst.close();
    }
    check(bitEqual(renders[0], renders[1]) && !renders[0].empty() && bitEqual(renders[0], std::vector<float>(signal.begin(), signal.begin() + ptrdiff_t(renders[0].size()))),
          "offline render with Record: output is bit-exact and identical between runs");
    check(!clips[0].empty() && clips[0] == clips[1], "the offline recording is deterministic");
}

// ---------------------------------------------------------------- parallel instances and the editor under load

void testParallelInstances(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& cls, const std::vector<float>& signal) {
    printf("11. Eight instances in parallel (audio threads) while their editors open and close\n");
    std::vector<std::unique_ptr<Instance>> instances;
    for (int i = 0; i < kParallelInstances; ++i) {
        instances.push_back(std::make_unique<Instance>());
        if (!instances.back()->open(factory, cls, i % 2 ? 48000.0 : 44100.0, kBlock)) { check(false, "instance opens"); return; }
    }
    std::vector<std::thread> threads;
    std::vector<int> ok(kParallelInstances, 0);
    std::atomic<int> editorsDone{0};
    for (int i = 0; i < kParallelInstances; ++i) {
        threads.emplace_back([&, i] {
            Instance& inst = *instances[size_t(i)];
            const double rate = inst.sampleRate;
            const auto sig = tsig::synth({{57 + i, 0, 2.0, 0.4f}, {64, 0, 2.0, 0.4f}}, rate, 2.0, true);
            inst.setParam(ttr::kParamRecord, 1.0);
            std::vector<float> out;
            for (size_t b = 0; (b + 1) * kBlock <= sig.size(); ++b) inst.process(&sig[b * kBlock], &out);
            bool exact = bitEqual(out, std::vector<float>(sig.begin(), sig.begin() + ptrdiff_t(out.size())));
            inst.setParam(ttr::kParamRecord, 0.0);
            ttr::DecodedState st;
            const bool done = waitFor([&] { inst.process(nullptr); st = inst.state(); return st.snapshot && st.snapshot->hasRecording(); });
            inst.setParam(ttr::kParamPreview, 1.0);
            std::vector<float> preview;
            for (int b = 0; b < 100; ++b) inst.process(nullptr, &preview);
            ok[size_t(i)] = exact && done && rms(preview) > 0.001;
        });
    }
    int cycles = 0;
    while (editorsDone < kParallelInstances || cycles < 8) {  // main thread: open / close every editor while the audio threads run
        for (auto& inst : instances) {
            IPtr<IPlugView> view = owned(inst->controller->createView(ViewType::kEditor));
            NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1100, 640) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
            view->attached((__bridge void*)w.contentView, kPlatformTypeNSView);
            pumpRunLoop(0.01);
            view->removed();
        }
        ++cycles;
        editorsDone = 0;
        for (int v : ok) editorsDone += v ? 1 : 0;
        if (cycles > 400) break;
    }
    for (std::thread& t : threads) t.join();
    int good = 0;
    for (int v : ok) good += v;
    check(good == kParallelInstances, "8 instances record, analyse and preview independently while editors open and close");
    for (auto& inst : instances) inst->close();
}

void testEditorCycles(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& cls, const std::vector<float>& signal) {
    printf("12. %d editor open / close cycles while recording and previewing\n", kEditorCycles);
    Instance inst;
    if (!inst.open(factory, cls)) { check(false, "instance opens"); return; }
    inst.setParam(ttr::kParamRecord, 1.0);
    std::atomic<bool> run{true};
    std::atomic<long> blocks{0};
    std::thread audio([&] {  // the host's audio thread keeps going the whole time
        size_t pos = 0;
        std::vector<float> sink;
        while (run) {
            inst.process(&signal[pos % (signal.size() - kBlock)], &sink);
            if (sink.size() > 100000) sink.clear();
            pos += kBlock;
            ++blocks;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    std::vector<ttr::NoteEvent> notes;
    int created = 0;
    for (int i = 0; i < kEditorCycles; ++i) {
        if (i == 60) {  // switch from recording to preview halfway: the editors then open on a running preview
            inst.setParam(ttr::kParamRecord, 0.0);
            waitFor([&] { return inst.state().snapshot != nullptr; });
            inst.setParam(ttr::kParamPreview, 1.0);
        }
        IPtr<IPlugView> view = owned(inst.controller->createView(ViewType::kEditor));
        if (!view) continue;
        NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1100, 640) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        IPtr<TestFrame> frame = owned(new TestFrame);
        view->setFrame(frame);
        if (view->attached((__bridge void*)w.contentView, kPlatformTypeNSView) != kResultOk) continue;
        ++created;
        pumpRunLoop(0.004);
        if (i % 20 == 0) {
            NSView* content = w.contentView.subviews.firstObject;
            NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
            [content cacheDisplayInRect:content.bounds toBitmapImageRep:rep];
        }
        view->removed();
        view->setFrame(nullptr);
        view.reset();
    }
    run = false;
    audio.join();
    check(created == kEditorCycles, "all 200 editors were created and attached");
    check(blocks > 100, "the audio thread kept processing during all cycles");
    inst.setParam(ttr::kParamPreview, 0.0);
    inst.process(nullptr);
    std::vector<float> out;
    check(inst.process(&signal[0], &out) && out.size() == size_t(kBlock) && inst.state().snapshot != nullptr, "the plug-in is intact afterwards");
    inst.close();
}

}  // namespace

void runStressTests(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& cls, const std::vector<float>& signal) {
    testControllerStateAndProcessing(factory, cls);
    testRatesBlocksAndOffline(factory, cls);
    testParallelInstances(factory, cls, signal);
    testEditorCycles(factory, cls, signal);
}

}  // namespace hosttest
