// UI interaction test: drives the real view (real kernel, the demo song through a fake model) with synthetic mouse and
// key events: load, play while the notes arrive, strips per instrument, mute / solo / fader, seek, zoom, export tempo,
// model size and instrument choice (keep / focus), mute input, record, clear, diagnostics, About, banners, and a layout
// check at the default and the minimum size.
#import <Cocoa/Cocoa.h>

#include <cmath>

#include "TestDemoSong.h"
#include "TestKernel.h"
#include "UiEvents.h"
#include "core/Instruments.h"
#include "core/Version.h"
#include "midi/MidiFileWriter.h"
#import "ui/TTRAbout.h"
#import "ui/TTRMainView.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTROverlays.h"

using namespace ttr;
using namespace uievents;

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

// Every visible control lies inside the window and no two visible siblings overlap (overlays excluded).
int layoutProblems(TTRMainView_v1* view, const char* where) {
    int problems = 0;
    NSMutableArray<NSView*>* visible = [NSMutableArray array];
    for (NSView* v in view.subviews)
        if (!v.hidden && v != view->_banner && v != view->_diag && v != view->_about && v != view->_picker) [visible addObject:v];
    for (NSUInteger i = 0; i < visible.count; ++i) {
        const NSRect a = visible[i].frame;
        if (!NSContainsRect(view.bounds, a)) {
            printf("     %s: %s %s lies outside the window\n", where, NSStringFromClass(visible[i].class).UTF8String, NSStringFromRect(a).UTF8String);
            ++problems;
        }
        for (NSUInteger j = i + 1; j < visible.count; ++j)
            if (NSIntersectsRect(a, visible[j].frame)) {
                printf("     %s: %s %s overlaps %s %s\n", where, NSStringFromClass(visible[i].class).UTF8String, NSStringFromRect(a).UTF8String,
                       NSStringFromClass(visible[j].class).UTF8String, NSStringFromRect(visible[j].frame).UTF8String);
                ++problems;
            }
    }
    return problems;
}

void click(NSView* v, NSPoint p) {
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, p)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, p)];
}
void clickCentre(NSView* v) { click(v, NSMakePoint(NSMidX(v.bounds), NSMidY(v.bounds))); }

NSEvent* wheel(int32_t dy, CGEventFlags flags) {
    CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, dy);
    CGEventSetFlags(cg, flags);
    NSEvent* e = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    return e;
}

TTRInstrumentStrip_v1* stripFor(TTRMainView_v1* view, int program) {
    const auto it = view->_stripByProgram.find(program);
    return it == view->_stripByProgram.end() ? nil : it->second;
}

NSView* toggleIn(NSView* strip, NSString* letter) {
    for (NSView* sub in strip.subviews)
        if ([sub respondsToSelector:@selector(letter)] && [[sub valueForKey:@"letter"] isEqualToString:letter]) return sub;
    return nil;
}

}  // namespace

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        demo::installDecoder();
        auto model = std::make_shared<tk::FakeTranscriber>(demo::chunks(), true);
        setTranscriber(model);
        auto kernel = std::make_shared<TonetraceKernel>();
        kernel->prepare(tk::kRate, tk::kBlock);
        kernel->setHostTransport(124.0, 0.0);
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
        NSWindow* win = hostWindow(view, kTTRDefaultSize);
        [view refreshNow];

        printf("empty window\n");
        check(!view->_openButton.hidden && view->_wave.hidden && view->_roll.hidden, "without a song: the drop zone with Open, no waveform, no piano roll");
        check(!view->_playButton.isEnabled && !view->_dragButton.isEnabled && !view->_exportButton.isEnabled, "play and the MIDI export are inactive");
        check(view->_stripByProgram.empty(), "no instrument strips yet");
        check(layoutProblems(view, "empty 1280x800") == 0, "layout: nothing overlaps, everything inside the window (empty, 1280 x 800)");

        printf("loading: playable before the notes\n");
        [view importSongAtPath:@"/demo/Night Drive.wav"];
        check(tk::waitFor([&] { return kernel->document() && kernel->document()->audio; }), "the song is decoded and shown");
        [view refreshNow];
        check(!view->_wave.hidden && !view->_roll.hidden && view->_openButton.hidden, "waveform and piano roll replace the drop zone");
        check(view->_playButton.isEnabled && kernel->document()->notes.empty(), "Play is active although no note is known yet");
        clickCentre(view->_playButton);
        check(kernel->isPlaying() && view->_playButton.isOn, "the play button starts the playback");
        tk::run(*kernel, tk::silence(40));
        check(kernel->positionSec() > 0.4, "the playhead moves while the model works");
        [view keyDown:key(view, @" ", 49)];
        check(!kernel->isPlaying(), "Space pauses");
        [view keyDown:key(view, @" ", 49)];
        check(kernel->isPlaying(), "Space plays again");
        [view keyDown:key(view, @"\r", 36)];
        check(kernel->positionSec() < 0.05, "Return goes back to the start");

        printf("notes arrive step by step\n");
        model->release();
        check(tk::waitFor([&] { return kernel->document()->notes.size() > 20; }), "the first chunk's notes are in the document");
        [view refreshNow];
        check(view->_stripByProgram.size() >= 3, "strips appear for the instruments found so far");
        check(kernel->document()->transcription.isActive(), "the transcription is still running");
        model->releaseAll();
        check(tk::waitFor([&] { return kernel->document()->transcription.phase == TranscriptionPhase::Done; }), "the transcription finishes");
        [view refreshNow];
        check(view->_stripByProgram.size() == 5 && stripFor(view, kDrumProgram) && stripFor(view, 33) && stripFor(view, 52),
              "five strips: guitar, bass, voice, pad, drums");
        check([stripFor(view, 33).info hasPrefix:@"32 notes"] && [stripFor(view, kDrumProgram).info containsString:@"hits"], "strips name their notes and hits");
        check(view->_dragButton.isEnabled && view->_exportButton.isEnabled, "the MIDI export is active");
        check(layoutProblems(view, "song 1280x800") == 0, "layout with a song (1280 x 800)");

        printf("mixer\n");
        TTRInstrumentStrip_v1* pad = stripFor(view, 88);
        NSView* muteToggle = toggleIn(pad, @"M");
        check(muteToggle != nil, "the pad strip has an M button");
        if (muteToggle) clickCentre(muteToggle);
        check(kernel->mixer().get(88).mute && !kernel->mixer().isAudible(88), "M mutes the instrument");
        [view refreshNow];
        check(!pad.isAudible, "the muted strip is dimmed");
        check(parseMidiFile(kernel->buildMidi()).tracks == 5, "the export leaves the muted instrument out (4 tracks + tempo)");
        if (muteToggle) clickCentre(muteToggle);
        check(!kernel->mixer().get(88).mute && parseMidiFile(kernel->buildMidi()).tracks == 6, "M again unmutes: five instrument tracks");
        NSView* soloToggle = toggleIn(stripFor(view, 33), @"S");
        if (soloToggle) clickCentre(soloToggle);
        [view refreshNow];
        check(kernel->mixer().isAudible(33) && !kernel->mixer().isAudible(26) && !stripFor(view, 26).isAudible, "S solos the bass, the others dim");
        if (soloToggle) clickCentre(soloToggle);
        check(kernel->mixer().isAudible(26), "S again ends the solo");

        printf("model size and instruments\n");
        check([view->_modelPill.text isEqualToString:@"MODEL: MEDIUM"] && [view->_instrumentPill.text isEqualToString:@"ALL"], "the pills name the model and \"all\" instruments");
        setTranscriberLoader([model](std::string*) -> std::shared_ptr<NoteTranscriber> { return model; });  // any size: the demo model
        const int callsBefore = model->callCount();
        [view chooseModelSize:int(ModelSize::Small)];
        check(modelSize() == ModelSize::Small && [view->_modelPill.text isEqualToString:@"MODEL: SMALL"], "choosing Small switches the model");
        check(tk::waitFor([&] { return model->callCount() == callsBefore + 1 && kernel->document()->transcription.phase == TranscriptionPhase::Done; }),
              "a loaded song is analysed again with the new model");
        [view chooseModelSize:int(ModelSize::Medium)];
        tk::waitIdle(*kernel);
        TTRInstrumentPicker_v1* picker = view->_picker;
        clickCentre(view->_instrumentPill);
        check(picker.isShowing && [picker selection].empty() && !picker.isFocused, "the instrument pill opens the picker with every instrument");
        [picker selectAll:NO];
        [picker toggleProgram:33];
        [picker toggleFamilyOf:kDrumProgram];
        check([picker selection] == std::vector<int>({33, kDrumProgram}), "single instruments and families toggle");
        const int callsKeep = model->callCount();
        [picker apply];
        [view refreshNow];
        check(!picker.isShowing && kernel->instrumentSelection() == std::vector<int>({33, kDrumProgram}) && !kernel->isWorking() && model->callCount() == callsKeep,
              "keeping bass and drums applies at once, without a new analysis");
        check(view->_stripByProgram.size() == 2 && [view->_instrumentPill.text isEqualToString:@"BASS + DRUMS"], "only their strips remain, the pill names them");
        check(parseMidiFile(kernel->buildMidi()).tracks == 3, "the export holds the two kept instruments");
        clickCentre(view->_instrumentPill);
        picker.isFocused = YES;
        [picker apply];
        check(kernel->modelFocus() && tk::waitFor([&] { return model->callCount() == callsKeep + 1 && !kernel->isWorking(); }) &&
                  model->programsAsked() == std::vector<int>({33, kDrumProgram}),
              "with focus the model is asked again, for bass and drums only");
        clickCentre(view->_instrumentPill);
        [picker mouseDown:mouse(NSEventTypeLeftMouseDown, picker, NSMakePoint(4, 4))];
        check(!picker.isShowing, "a click beside the panel closes the picker unchanged");
        clickCentre(view->_instrumentPill);
        [picker keyDown:key(picker, @"\x1b", 53)];
        check(!picker.isShowing && kernel->modelFocus(), "Escape closes it unchanged");
        clickCentre(view->_instrumentPill);
        [picker selectAll:YES];
        picker.isFocused = NO;
        [picker apply];
        check(tk::waitFor([&] { return model->callCount() == callsKeep + 2 && !kernel->isWorking(); }) && model->programsAsked().empty(),
              "back to every instrument: the model hears everything again");
        [view refreshNow];
        check(view->_stripByProgram.size() == 5 && [view->_instrumentPill.text isEqualToString:@"ALL"], "all five strips are back");
        setTranscriberLoader(nullptr);
        setTranscriber(model);

        printf("seek, zoom, scroll\n");
        TTRPianoRoll_v1* roll = view->_roll;
        const CGFloat noteW = roll.bounds.size.width - kTTRKeyboardWidth;
        click(roll, NSMakePoint(kTTRKeyboardWidth + noteW * 0.5, 100));
        check(std::fabs(kernel->positionSec() - demo::kSeconds * 0.5) < 0.2, "a click on the roll moves the playhead there");
        click(view->_wave, NSMakePoint(kTTRKeyboardWidth + noteW * 0.25, 40));
        check(std::fabs(kernel->positionSec() - demo::kSeconds * 0.25) < 0.2, "a click on the waveform moves the playhead");
        const double fitPx = view->_tl->pxPerSec;
        [view handleWheel:wheel(10, kCGEventFlagMaskCommand) noteAreaX:100];
        check(!view->_tl->isFit && view->_tl->pxPerSec > fitPx, "Cmd + wheel zooms in");
        const double y0 = view->_tl->scrollY;
        [view handleWheel:wheel(-5, 0) noteAreaX:120];
        check(view->_tl->scrollY != y0, "the wheel scrolls the rows");
        view->_zoomSlider.onChange(0.0);
        check(view->_tl->isFit, "the zoom slider at its left end shows the whole song");

        printf("export tempo, mute input, record, clear\n");
        const double bpm = kernel->exportBpm();
        click(view->_tempoStepper, NSMakePoint(view->_tempoStepper.bounds.size.width - 10, 8));
        check(std::fabs(kernel->exportBpm() - (bpm + 1)) < 1e-9, "the stepper's up arrow raises the export tempo");
        clickCentre(view->_muteButton);
        check(kernel->param(kParamMuteInput) >= 0.5f, "MUTE silences the input");
        clickCentre(view->_muteButton);
        check(kernel->param(kParamMuteInput) < 0.5f, "MUTE again lets it through");
        clickCentre(view->_recordButton);
        check(kernel->param(kParamRecord) >= 0.5f && !kernel->isPlaying(), "the record button arms the recording and stops the playback");
        clickCentre(view->_recordButton);
        check(kernel->param(kParamRecord) < 0.5f, "pressed again it stops");
        clickCentre(view->_clearButton);
        [view refreshNow];
        check(!kernel->document() && !view->_openButton.hidden && view->_stripByProgram.empty(), "the trash button removes the song: the drop zone is back");

        printf("diagnostics, About, banners\n");
        TTRDiagnosticsView_v1* diag = view->_diag;
        check(diag.hidden, "the diagnostics panel starts closed");
        clickCentre(view->_optionsButton);
        check(!diag.hidden, "the options button opens the diagnostics");
        NSString* report = [diag reportText];
        check([report containsString:[@"Tonetrace " stringByAppendingString:@TTR_VERSION_STRING]] && [report containsString:@"Transcription model"],
              "the report names the version and the model status");
        clickCentre(view->_optionsButton);
        check(diag.hidden, "and closes it again");
        TTRAboutView_v1* about = view->_about;
        view->_logo.onClick();
        check(about.isShowing, "the logo opens About");
        [about dismiss];
        pump(0.4);
        check(!about.isShowing, "About closes");
        kernel->notice("Test notice", NoticeLevel::Info);
        [view refreshNow];
        check(view->_banner.isShowing && [view->_banner.text isEqualToString:@"Test notice"], "a kernel notice becomes a banner");

        printf("minimum size\n");
        win.contentView = nil;
        NSWindow* small = hostWindow(view, kTTRMinimumSize);
        (void)small;
        [view refreshNow];
        check(layoutProblems(view, "empty 1024x640") == 0, "layout at the minimum size (empty)");
        [view importSongAtPath:@"/demo/Night Drive.wav"];
        tk::waitFor([&] { return kernel->document() && kernel->document()->transcription.phase == TranscriptionPhase::Done; });
        [view refreshNow];
        check(layoutProblems(view, "song 1024x640") == 0, "layout at the minimum size (song)");

        [view shutdown];
        kernel->deactivate();
        kernel->clearDocument();
        setTranscriber(nullptr);
        printf("%s\n", gFailures ? "UI INTERACTION TEST FAILED" : "UI INTERACTION TEST PASSED");
        return gFailures ? 1 : 0;
    }
}
