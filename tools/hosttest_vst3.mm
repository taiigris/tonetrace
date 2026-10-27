// Headless VST3 host test: loads the installed Tonetrace.vst3 with the SDK's hosting classes, records
// synthetic audio through the processor, checks spectrogram / peaks, notes set through the model, preview, MIDI export,
// state round trip and the editor view.
//   tonetrace_hosttest_vst3 [path-to.vst3]
#include "HostHarness.h"
#include "TestDrums.h"
#include "TestSignals.h"
#include "core/NoteEdit.h"
#include "core/Snapshot.h"
#include "core/Version.h"
#include "midi/ExportPlanner.h"
#include "midi/MidiFileWriter.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace hosttest;

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const std::string path = argc > 1 ? argv[1] : std::string(getenv("HOME")) + "/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3";
        NSString* midiPath = [NSTemporaryDirectory() stringByAppendingPathComponent:@"tonetrace_hosttest.mid"];

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
        bool controllerVersion = false;
        for (const auto& info : classes) if (info.category() == kVstComponentControllerClass) controllerVersion = info.version() == TTR_VERSION_STRING;
        check(controllerVersion, "the controller class reports the same version");

        printf("2. Instantiate and check the parameters\n");
        Instance a;
        if (!a.open(factory, *audioClass)) { check(false, "component + controller created and connected, processing set up"); return 1; }
        check(true, "component + controller created and connected, processing set up");
        check(a.controller->getParameterCount() == ttr::kNumParams, "13 parameters exported");
        int automatable = 0;
        bool flagsRight = true;
        for (int id = 0; id < ttr::kNumParams; ++id) {
            ParameterInfo pi{};
            a.controller->getParameterInfo(id, pi);
            const bool canAutomate = (pi.flags & ParameterInfo::kCanAutomate) != 0;
            automatable += canAutomate ? 1 : 0;
            flagsRight &= canAutomate == ttr::isAutomatable(id) && pi.id == ParamID(id);
            if (!ttr::isAutomatable(id)) flagsRight &= (pi.flags & ParameterInfo::kIsHidden) != 0;
        }
        check(automatable == 5 && flagsRight, "only Preview, Sound, Mix, Solo and Bypass are automatable; view settings are hidden");
        ParameterInfo info{};
        check(a.controller->getParameterInfo(ttr::kParamBypass, info) == kResultOk && (info.flags & ParameterInfo::kIsBypass), "Bypass parameter is flagged kIsBypass");
        check(std::fabs(a.controller->getParamNormalized(ttr::kParamHideDrums) - 0.0) < 1e-9, "Hide drums defaults to off");
        check(std::fabs(a.controller->getParamNormalized(ttr::kParamSnap) - 0.6) < 1e-6, "Snap defaults to 1/16 (entry 3 of 0..5)");

        printf("3. Record on / off, spectrogram and peaks\n");
        const double length = 6.5;
        const auto signal = tsig::synth(tsig::chordThenMelody(), kRate, length, true);
        const size_t totalBlocks = size_t(length * kRate) / kBlock;
        std::vector<float> outL, outR;
        for (size_t b = 0; b < 40; ++b) a.process(&signal[b * kBlock], &outL, &outR);
        bool dry = true;
        for (size_t i = 0; i < outL.size(); ++i) dry &= outL[i] == signal[i] && outR[i] == 0.5f * signal[i];
        check(dry, "record off: output equals input bit-exactly");
        check(a.state().snapshot == nullptr, "record off: nothing captured");

        a.setParam(ttr::kParamRecord, 1.0);
        outL.clear(); outR.clear();
        for (size_t b = 0; b < totalBlocks; ++b) a.process(&signal[b * kBlock], &outL, &outR);
        dry = true;
        for (size_t i = 0; i < outL.size(); ++i) dry &= outL[i] == signal[i];
        check(dry, "record on: the signal still passes through unchanged");
        a.setParam(ttr::kParamRecord, 0.0);
        for (int b = 0; b < 4; ++b) a.process(nullptr);
        ttr::DecodedState state;
        check(waitFor([&] {
                  a.process(nullptr);
                  state = a.state();
                  return state.snapshot != nullptr;
              }), "analysis finishes in the background and shows up in the state");
        const ttr::SnapshotPtr rec = state.snapshot;
        if (!rec) { check(false, "a recording exists (cannot continue without it)"); a.close(); return 1; }
        check(rec && rec->spectrogram && rec->spectrogram->frames > 200 && rec->spectrogram->peak > 0.0f, "spectrogram is filled");
        float strongest = 0;
        if (rec && rec->spectrogram) for (float v : rec->spectrogram->raw) strongest = std::max(strongest, v);
        check(strongest > 0.0f, "spectrogram contains energy");
        float wave = 0;
        if (rec && rec->peaks) for (float v : rec->peaks->minMax) wave = std::max(wave, std::fabs(v));
        check(rec && rec->peaks && std::abs(rec->peaks->count() - int(length * ttr::kPeaksPerSecond)) <= 3 && wave > 0.1f, "waveform peaks are filled");
        check(rec && rec->audio && !rec->audio->samples.empty(), "audio is stored for the preview");
        check(rec && rec->notes.empty(), "no notes appear automatically");
        check(rec && rec->bpm == 100.0 && rec->startPpq >= 8.0, "tempo and position come from ProcessContext");

        printf("4. Notes set through the model, preview\n");
        ttr::TimeGrid grid;
        grid.bpm = rec->bpm;
        grid.startPpq = rec->startPpq;
        std::vector<ttr::NoteEvent> notes;
        for (const tsig::Tone& t : tsig::chordThenMelody()) notes = ttr::withNoteAdded(notes, ttr::makeNote(t.pitch, t.start, t.end, grid, true));
        notes = ttr::withSelection(notes, {0, 1}, ttr::SelectMode::Replace);
        {
            MemoryStream stream;
            const auto bytes = ttr::encodeState(state.params, ttr::withNotes(*rec, notes).get());
            stream.write(const_cast<uint8_t*>(bytes.data()), int32(bytes.size()), nullptr);
            int64 pos = 0;
            stream.seek(0, IBStream::kIBSeekSet, &pos);
            check(a.component->setState(&stream) == kResultOk, "setState with the drawn notes");
        }
        state = a.state();
        if (!state.snapshot) { check(false, "the notes are in the plug-in's model (cannot continue)"); a.close(); return 1; }
        check(state.snapshot && state.snapshot->notes.size() == notes.size() && ttr::selectedCount(state.snapshot->notes) == 2, "notes are in the plug-in's model");

        const std::vector<float> quiet(kBlock * 60, 0.0f);
        auto render = [&](size_t blocks) {
            std::vector<float> out;
            for (size_t b = 0; b < blocks; ++b) a.process(&quiet[0], &out);
            return out;
        };
        const double before = rms(render(50));
        a.setParam(ttr::kParamPreview, 1.0);
        a.setParam(ttr::kParamMix, 0.5);
        const double both = rms(render(50));
        a.setParam(ttr::kParamMix, 1.0);
        a.setParam(ttr::kParamPreview, 0.0);
        render(2);
        a.setParam(ttr::kParamPreview, 1.0);
        const double notesOnly = rms(render(50));
        a.setParam(ttr::kParamMix, 0.0);
        a.setParam(ttr::kParamPreview, 0.0);
        render(2);
        a.setParam(ttr::kParamPreview, 1.0);
        const double sampleOnly = rms(render(50));
        check(before == 0.0 && both > 0.01, "preview on: the output changes (silence before, sample + notes after)");
        check(notesOnly > 0.0 && sampleOnly > 0.01 && notesOnly != sampleOnly, "Mix selects notes only / sample only");
        a.setParam(ttr::kParamPreview, 0.0);
        a.setParam(ttr::kParamMix, 0.5);
        a.setParam(ttr::kParamBypass, 1.0);
        a.setParam(ttr::kParamPreview, 1.0);
        check(rms(render(30)) == 0.0, "bypass: preview and processing are off");
        a.setParam(ttr::kParamBypass, 0.0);
        a.setParam(ttr::kParamPreview, 0.0);
        render(2);

        printf("5. MIDI export of the drawn notes\n");
        const auto midi = ttr::buildMidiForNotes(*state.snapshot, int(state.params[ttr::kParamSnap]), ttr::KeyResult{});
        const ttr::ParsedMidi parsed = ttr::parseMidiFile(midi);
        check(parsed.valid && parsed.notes.size() == 2 && std::fabs(parsed.bpm - 100.0) < 0.01, "MIDI of the selected notes parses (2 notes, 100 bpm)");
        const NSData* data = [NSData dataWithBytes:midi.data() length:midi.size()];
        check([data writeToFile:midiPath atomically:YES], "MIDI file written to the temporary directory");
        [NSFileManager.defaultManager removeItemAtPath:midiPath error:nil];

        printf("6. Parameters and state round trip into a second instance\n");
        check(a.controller->setParamNormalized(ttr::kParamGain, 0.25) == kResultOk && a.state().params[ttr::kParamGain] == 0.25f,
              "controller parameter change reaches the kernel (Gain)");
        a.setParam(ttr::kParamMagnet, 0.0);
        a.process(nullptr);
        check(a.state().params[ttr::kParamMagnet] == 0.0f, "host parameter change reaches the kernel (Magnet off)");
        String128 text;
        ParamValue parsedValue = 0;
        a.controller->getParamStringByValue(ttr::kParamMagnet, 1.0, text);
        check(a.controller->getParamValueByString(ttr::kParamMagnet, text, parsedValue) == kResultOk && parsedValue == 1.0, "value strings round-trip (On)");
        a.controller->getParamStringByValue(ttr::kParamSpeed, toNormalizedSpeed(2.0), text);
        check(a.controller->getParamValueByString(ttr::kParamSpeed, text, parsedValue) == kResultOk && std::fabs(parsedValue - toNormalizedSpeed(2.0)) < 1e-3,
              "Speed value string round-trips (2x)");

        MemoryStream stream;
        check(a.component->getState(&stream) == kResultOk && stream.getSize() > 100, "getState");
        Instance b;
        check(b.open(factory, *audioClass), "second instance");
        int64 pos = 0;
        stream.seek(0, IBStream::kIBSeekSet, &pos);
        check(b.component->setState(&stream) == kResultOk, "setState on the component");
        stream.seek(0, IBStream::kIBSeekSet, &pos);
        check(b.controller->setComponentState(&stream) == kResultOk, "setComponentState on the controller");
        const ttr::DecodedState restored = b.state();
        const ttr::SnapshotPtr rs = restored.snapshot;
        check(rs && rs->notes.size() == notes.size() && ttr::selectedCount(rs->notes) == 2, "notes restored (with selection)");
        check(rs && rs->spectrogram && rs->spectrogram->frames == rec->spectrogram->frames, "spectrogram restored");
        check(rs && rs->peaks && rs->peaks->count() == rec->peaks->count(), "peaks restored");
        check(rs && rs->audio && rs->audio->samples.size() == rec->audio->samples.size(), "audio restored");
        check(std::fabs(b.controller->getParamNormalized(ttr::kParamGain) - 0.25) < 1e-6, "controller parameters follow the restored state");
        b.setParam(ttr::kParamPreview, 1.0);
        b.setParam(ttr::kParamMix, 0.0);
        std::vector<float> restoredOut;
        for (int i = 0; i < 50; ++i) b.process(nullptr, &restoredOut);
        check(rms(restoredOut) > 0.01, "the restored recording plays in the preview");
        b.setParam(ttr::kParamPreview, 0.0);

        printf("7. Editor view (NSView)\n");
        IPtr<IPlugView> view = owned(b.controller->createView(ViewType::kEditor));
        check(view != nullptr, "createView(editor)");
        if (view) {
            check(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue && view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue,
                  "supports NSView only");
            ViewRect size;
            view->getSize(&size);
            check(size.right - size.left == 1280 && size.bottom - size.top == 720, "default size 1280x720");
            check(view->canResize() == kResultTrue, "resizable");
            ViewRect tiny(0, 0, 100, 100);
            view->checkSizeConstraint(&tiny);
            check(tiny.right - tiny.left == 1024 && tiny.bottom - tiny.top == 600, "size constraint enforces the minimum 1024x600");

            NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.right, size.bottom) styleMask:NSWindowStyleMaskBorderless
                                                             backing:NSBackingStoreBuffered defer:NO];
            IPtr<TestFrame> frame = owned(new TestFrame);
            view->setFrame(frame);
            check(view->attached((__bridge void*)window.contentView, kPlatformTypeNSView) == kResultOk, "attached to an NSView");
            check(frame->calls >= 1 && frame->last.right - frame->last.left == 1280, "IPlugFrame::resizeView announces the size");
            check(window.contentView.subviews.count == 1, "view embedded as subview");
            for (int i = 0; i < 6; ++i) [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            NSView* content = window.contentView.subviews.firstObject;
            check([NSStringFromClass(content.class) hasSuffix:@"_v1"], "view class carries the _v1 suffix");
            NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
            [content cacheDisplayInRect:content.bounds toBitmapImageRep:rep];
            check(rep.pixelsWide > 0, "the editor draws");
            ViewRect bigger(0, 0, 1200, 700);
            check(view->onSize(&bigger) == kResultOk && std::fabs(content.frame.size.width - 1200) < 0.5, "onSize resizes the embedded view");
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            check(view->removed() == kResultOk && window.contentView.subviews.count == 0, "removed detaches the view");
            view->setFrame(nullptr);
        }
        view.reset();

        printf("8. Teardown\n");
        a.close();
        b.close();
        check(true, "both instances closed");

        runStressTests(factory, *audioClass, signal);
        printf("%s\n", gFailures == 0 ? "HOSTTEST VST3 PASSED" : "HOSTTEST VST3 FAILED");
        return gFailures == 0 ? 0 : 1;
    }
}
