// Headless VST3 host test: loads the installed Tonetrace.vst3 with the SDK's hosting classes and checks what a host
// relies on: classes and parameters, bit-exact pass-through, recording into a document, Mute input and Bypass, the state
// round trip (notes with instruments, recording audio, mix) into a second instance, and the editor view.
// The transcription model is pointed at a missing file so the test never downloads it (MuScriptor is tested by mstest).
//   tonetrace_hosttest_vst3 [path-to.vst3]
#include <stdlib.h>

#include "HostHarness.h"
#include "TestSignals.h"
#include "core/Instruments.h"
#include "core/Version.h"
#include "midi/ExportPlanner.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace hosttest;

namespace {

// A document as a host would restore it: a recording with notes of three instruments.
std::vector<uint8_t> projectState(const ttr::DecodedState& base, const ttr::MixerState& mix) {
    auto doc = std::make_shared<ttr::Document>(*base.document);
    doc->notes = {{45, 0.1, 1.0, 33}, {36, 0.5, 0.6, ttr::kDrumProgram}, {69, 1.0, 2.0, 0}};
    doc->transcription.phase = ttr::TranscriptionPhase::Done;
    return ttr::encodeState(base.params, doc.get(), mix, 110.0);
}

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        setenv("TONETRACE_MUSCRIPTOR_MODEL", "/nonexistent/tonetrace-hosttest-model.gguf", 1);
        const std::string path = argc > 1 ? argv[1] : std::string(getenv("HOME")) + "/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3";

        printf("1. Load the module and find the classes\n");
        HostApplication host;
        PluginContextFactory::instance().setPluginContext(static_cast<IHostApplication*>(&host));
        std::string error;
        auto module = VST3::Hosting::Module::create(path, error);
        check(module != nullptr, "module loads (bundleEntry, GetPluginFactory)");
        if (!module) { printf("  %s\n", error.c_str()); return 1; }
        const auto& factory = module->getFactory();
        const VST3::Hosting::ClassInfo* audioClass = nullptr;
        int classCount = 0;
        auto classes = factory.classInfos();
        for (const auto& info : classes) {
            ++classCount;
            if (info.category() == kVstAudioEffectClass) audioClass = &info;
        }
        check(classCount == 2 && audioClass != nullptr, "factory has the processor and the controller class");
        if (!audioClass) return 1;
        check(audioClass->subCategoriesString() == "Fx|Analyzer", "category Fx|Analyzer");
        check(audioClass->version() == TTR_VERSION_STRING, "the VST3 ClassInfo reports the VERSION file version");

        printf("2. Instantiate and check the parameters\n");
        Instance a;
        if (!a.open(factory, *audioClass)) { check(false, "component + controller created and connected, processing set up"); return 1; }
        check(true, "component + controller created and connected, processing set up");
        check(a.controller->getParameterCount() == ttr::kNumParams, "5 parameters exported");
        int automatable = 0;
        bool flagsRight = true;
        for (int id = 0; id < ttr::kNumParams; ++id) {
            ParameterInfo pi{};
            a.controller->getParameterInfo(id, pi);
            const bool canAutomate = (pi.flags & ParameterInfo::kCanAutomate) != 0;
            automatable += canAutomate ? 1 : 0;
            flagsRight &= canAutomate == ttr::isAutomatable(id) && pi.id == ParamID(id);
        }
        check(automatable == 4 && flagsRight, "Mix, Volume, Mute input and Bypass are automatable, Record is not");
        ParameterInfo info{};
        check(a.controller->getParameterInfo(ttr::kParamBypass, info) == kResultOk && (info.flags & ParameterInfo::kIsBypass), "Bypass parameter is flagged kIsBypass");
        check(std::fabs(a.controller->getParamNormalized(ttr::kParamMix) - 0.5) < 1e-6 && std::fabs(a.controller->getParamNormalized(ttr::kParamVolume) - 0.8) < 1e-6,
              "defaults: Mix in the middle, Volume at 0 dB");

        printf("3. Pass-through and recording\n");
        const double length = 3.0;
        const auto signal = tsig::synth({{57, 0.0, 3.0, 0.4f}, {64, 0.5, 2.5, 0.3f}}, kRate, length, true);
        const size_t totalBlocks = size_t(length * kRate) / kBlock;
        std::vector<float> outL, outR;
        for (size_t b = 0; b < 40; ++b) a.process(&signal[b * kBlock], &outL, &outR);
        bool dry = true;
        for (size_t i = 0; i < outL.size(); ++i) dry &= outL[i] == signal[i] && outR[i] == 0.5f * signal[i];
        check(dry, "nothing loaded: output equals input bit-exactly");
        check(a.state().document == nullptr, "no document yet");
        a.setParam(ttr::kParamRecord, 1.0);
        outL.clear();
        for (size_t b = 0; b < totalBlocks; ++b) a.process(&signal[b * kBlock], &outL);
        dry = true;
        for (size_t i = 0; i < outL.size(); ++i) dry &= outL[i] == signal[i];
        check(dry, "while recording the signal passes through unchanged");
        a.setParam(ttr::kParamRecord, 0.0);
        ttr::DecodedState state;
        check(waitFor([&] {
                  a.process(nullptr);
                  state = a.state();
                  return state.document && state.document->transcription.phase == ttr::TranscriptionPhase::Failed;
              }), "the recording becomes the document; without a model the transcription reports why");
        if (!state.document) { a.close(); return 1; }
        check(state.document->isRecording && state.document->audio && std::fabs(state.document->lengthSec - length) < 0.05, "the recording keeps its audio (state)");

        printf("4. Mute input and Bypass\n");
        a.setParam(ttr::kParamMuteInput, 1.0);
        std::vector<float> muted;
        for (size_t b = 0; b < 20; ++b) a.process(&signal[b * kBlock], &muted);
        check(rms(muted) == 0.0, "Mute input silences the track's own audio");
        a.setParam(ttr::kParamBypass, 1.0);
        std::vector<float> bypassed;
        for (size_t b = 0; b < 20; ++b) a.process(&signal[b * kBlock], &bypassed);
        check(rms(bypassed) > 0.1, "Bypass passes the input again");
        a.setParam(ttr::kParamBypass, 0.0);
        a.setParam(ttr::kParamMuteInput, 0.0);
        a.process(nullptr);

        printf("5. State round trip into a second instance\n");
        const ttr::MixerState mix = ttr::MixerState{}.with(ttr::kDrumProgram, {true, false, 0.8f});
        {
            MemoryStream stream;
            const auto bytes = projectState(state, mix);
            stream.write(const_cast<uint8_t*>(bytes.data()), int32(bytes.size()), nullptr);
            int64 pos = 0;
            stream.seek(0, IBStream::kIBSeekSet, &pos);
            check(a.component->setState(&stream) == kResultOk, "setState with notes of three instruments");
        }
        check(a.controller->setParamNormalized(ttr::kParamMix, 0.25) == kResultOk && a.state().params[ttr::kParamMix] == 0.25f, "a controller change reaches the kernel (Mix)");
        MemoryStream stream;
        check(a.component->getState(&stream) == kResultOk && stream.getSize() > 1000, "getState");
        Instance b;
        check(b.open(factory, *audioClass), "second instance");
        int64 pos = 0;
        stream.seek(0, IBStream::kIBSeekSet, &pos);
        check(b.component->setState(&stream) == kResultOk, "setState on the component");
        stream.seek(0, IBStream::kIBSeekSet, &pos);
        check(b.controller->setComponentState(&stream) == kResultOk, "setComponentState on the controller");
        const ttr::DecodedState restored = b.state();
        check(restored.document && restored.document->notes.size() == 3 && ttr::instrumentsIn(restored.document->notes).size() == 3, "notes and instruments restored");
        check(restored.document && restored.document->audio && restored.document->isRecording, "the recording's audio restored");
        check(restored.exportBpm == 110.0 && restored.mixer.get(ttr::kDrumProgram).mute, "export tempo and instrument mix restored");
        check(std::fabs(b.controller->getParamNormalized(ttr::kParamMix) - 0.25) < 1e-6, "controller parameters follow the restored state");
        if (restored.document) {
            const ttr::ParsedMidi midi = ttr::parseMidiFile(ttr::buildMidi(ttr::notesForExport(restored.document->notes, restored.mixer, false), 110.0, "Recording"));
            check(midi.valid && midi.tracks == 3 && midi.notes.size() == 2, "the export of the restored document leaves the muted drums out");
        }

        printf("6. Editor view (NSView)\n");
        IPtr<IPlugView> view = owned(b.controller->createView(ViewType::kEditor));
        check(view != nullptr, "createView(editor)");
        if (view) {
            check(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue && view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue,
                  "supports NSView only");
            ViewRect size;
            view->getSize(&size);
            check(size.right - size.left == 1280 && size.bottom - size.top == 800, "default size 1280x800");
            check(view->canResize() == kResultTrue, "resizable");
            ViewRect tiny(0, 0, 100, 100);
            view->checkSizeConstraint(&tiny);
            check(tiny.right - tiny.left == 1024 && tiny.bottom - tiny.top == 640, "size constraint enforces the minimum 1024x640");
            NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.right, size.bottom) styleMask:NSWindowStyleMaskBorderless
                                                             backing:NSBackingStoreBuffered defer:NO];
            IPtr<TestFrame> frame = owned(new TestFrame);
            view->setFrame(frame);
            check(view->attached((__bridge void*)window.contentView, kPlatformTypeNSView) == kResultOk, "attached to an NSView");
            check(window.contentView.subviews.count == 1, "view embedded as subview");
            for (int i = 0; i < 6; ++i) pumpRunLoop(0.05);
            NSView* content = window.contentView.subviews.firstObject;
            check([NSStringFromClass(content.class) hasSuffix:@"_v1"], "view class carries the _v1 suffix");
            NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
            [content cacheDisplayInRect:content.bounds toBitmapImageRep:rep];
            check(rep.pixelsWide > 0, "the editor draws");
            ViewRect bigger(0, 0, 1400, 900);
            check(view->onSize(&bigger) == kResultOk && std::fabs(content.frame.size.width - 1400) < 0.5, "onSize resizes the embedded view");
            pumpRunLoop(0.1);
            check(view->removed() == kResultOk && window.contentView.subviews.count == 0, "removed detaches the view");
            view->setFrame(nullptr);
        }
        view.reset();

        printf("7. Teardown\n");
        a.close();
        b.close();
        check(true, "both instances closed");

        runStressTests(factory, *audioClass, signal);
        printf("%s\n", gFailures == 0 ? "HOSTTEST VST3 PASSED" : "HOSTTEST VST3 FAILED");
        return gFailures == 0 ? 0 : 1;
    }
}
