// Renders the plugin UI (real TTRMainView_v1, real kernel, analysed synthetic audio) into PNG files.
// Usage: tonetrace_uishot <output-dir>
#import <Cocoa/Cocoa.h>

#include "TestDrums.h"
#include "TestKernel.h"
#include "TestSignals.h"
#include "UiEvents.h"
#import "ui/TTRAbout.h"
#import "ui/TTRControls.h"
#import "ui/TTRMainView.h"
#import "ui/TTRNoteMapView.h"

using namespace ttr;

// AppKit swallows exceptions thrown while drawing and aborts; print them so layout bugs are diagnosable.
@interface TTRShotApp : NSApplication
@end
@implementation TTRShotApp
- (void)reportException:(NSException*)e {
    fprintf(stderr, "exception: %s: %s\n%s\n", e.name.UTF8String, e.reason.UTF8String, e.callStackSymbols.description.UTF8String);
}
@end

namespace {

constexpr double kRate = 44100.0;
constexpr double kDemoSeconds = 8.0;

std::vector<tsig::Tone> demoTones() {
    struct Chord { double start; int bass; std::vector<int> pad; int lead; };
    const Chord chords[] = {{0, 52, {57, 60, 64}, 76}, {2, 53, {57, 60, 65}, 74}, {4, 55, {59, 62, 67}, 72}, {6, 52, {56, 59, 64}, 71}};
    std::vector<tsig::Tone> tones;
    for (const Chord& c : chords) {
        tones.push_back({c.bass, c.start, c.start + 1.95, 0.22f, true});
        for (int p : c.pad) tones.push_back({p, c.start, c.start + 1.95, 0.6f});
        tones.push_back({c.lead, c.start + 0.25, c.start + 0.9, 0.8f});
        tones.push_back({c.lead + 2, c.start + 1.0, c.start + 1.6, 0.8f});
    }
    return tones;
}

// The notes a user would draw: the lead melody and the first two chords.
std::vector<NoteEvent> drawnNotes(const std::vector<tsig::Tone>& tones, const TimeGrid& grid) {
    std::vector<NoteEvent> notes;
    for (const tsig::Tone& t : tones)
        if (!t.pure && (t.amp > 0.7f || t.start < 3.9)) notes = withNoteAdded(notes, makeNote(t.pitch, t.start, t.end, grid, true));
    return notes;
}

void probeDrawing(NSView* view) {
    NSImage* image = [[NSImage alloc] initWithSize:view.bounds.size];
    [image lockFocusFlipped:YES];
    @try {
        [view drawRect:view.bounds];
    } @catch (NSException* e) {
        fprintf(stderr, "exception in %s: %s: %s\n%s\n", NSStringFromClass(view.class).UTF8String, e.name.UTF8String, e.reason.UTF8String, e.callStackSymbols.description.UTF8String);
    }
    [image unlockFocus];
    for (NSView* sub in view.subviews) probeDrawing(sub);
}

BOOL writePng(NSView* view, NSString* path) {
    if (getenv("TTR_PROBE")) probeDrawing(view);
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    @try {
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    } @catch (NSException* e) {
        fprintf(stderr, "exception: %s: %s\n%s\n", e.name.UTF8String, e.reason.UTF8String, e.callStackSymbols.description.UTF8String);
        return NO;
    }
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:path atomically:YES];
}

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSString* dir = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"docs";
        [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        [TTRShotApp sharedApplication];
        int failures = 0;

        // ---- recording (live map), analysed, notes drawn
        auto kernel = std::make_shared<TonetraceKernel>();
        kernel->prepare(kRate, tk::kBlock);
        kernel->setHostTransport(120.0, 0.0);
        const auto tones = demoTones();
        std::vector<float> signal = tsig::synth(tones, kRate, kDemoSeconds, true);
        const std::vector<float> drums = tdrum::render(tdrum::pattern(120.0, kDemoSeconds), kRate, kDemoSeconds, 0.6f);
        for (size_t i = 0; i < signal.size(); ++i) signal[i] += drums[i];
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
        NSWindow* win = uievents::hostWindow(view, kTTRDefaultSize);
        (void)win;

        kernel->setParam(kParamRecord, 1.0f);
        tk::run(*kernel, signal, 0, size_t(4.6 * kRate));
        tk::waitFor([&] { return kernel->mapView().frames > 150; });
        [view refreshNow];

        tk::run(*kernel, signal, size_t(4.6 * kRate), signal.size());
        kernel->setParam(kParamRecord, 0.0f);
        tk::run(*kernel, tk::silence(2));
        tk::waitFor([&] { return kernel->status() == KernelStatus::Idle && kernel->snapshot() && kernel->snapshot()->hasRecording(); });
        kernel->editNotes(drawnNotes(tones, kernel->timeGrid()));
        const SnapshotPtr snap = kernel->snapshot();
        std::vector<int> picks;
        for (size_t i = 0; i < snap->notes.size(); ++i) if (snap->notes[i].pitch >= 71 && snap->notes[i].startSec >= 4.0) picks.push_back(int(i));
        kernel->setNotes(withSelection(snap->notes, picks, SelectMode::Replace));
        kernel->setPreviewStart(1.0);
        kernel->setPreviewLoop(2.0, 5.0);
        kernel->setParam(kParamPreview, 1.0f);
        tk::run(*kernel, tk::silence(160));  // advances the playhead into the loop
        [view refreshNow];
        TTRNoteMapView_v1* map = (TTRNoteMapView_v1*)uievents::find(view, TTRNoteMapView_v1.class);
        const NSPoint cursor = [map pointForTime:4.55 pitch:72];
        [map mouseMoved:uievents::mouse(NSEventTypeMouseMoved, map, cursor)];
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot.png"]);

        // ---- diagnostics panel open, with a banner behind it closed
        {
            kernel->diagnostics().setHost("Ableton Live 12.4.7");
            kernel->setChannels(2);
            kernel->diagnostics().setLastMidiPath("~/Music/Tonetrace/Tonetrace A minor 120bpm.mid");
            NSArray<NSView*>* buttons = view.subviews;
            for (NSView* v in buttons)
                if ([v isKindOfClass:TTRButton_v1.class] && [((TTRButton_v1*)v).title isEqualToString:@"ⓘ"]) ((TTRButton_v1*)v).onClick();
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-diagnostics.png"]);
            for (NSView* v in buttons)
                if ([v isKindOfClass:TTRButton_v1.class] && [((TTRButton_v1*)v).title isEqualToString:@"ⓘ"]) ((TTRButton_v1*)v).onClick();
            [view refreshNow];
        }

        // ---- About overlay (click on the logo)
        {
            TTRLogoView_v1* logo = (TTRLogoView_v1*)uievents::find(view, TTRLogoView_v1.class);
            TTRAboutView_v1* about = (TTRAboutView_v1*)uievents::find(view, TTRAboutView_v1.class);
            logo.onClick();
            uievents::pump(0.3);
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-about.png"]);
            [about dismiss];
            uievents::pump(0.4);  // the fade-out has to finish before the next picture
        }

        // ---- smallest window
        win.contentView = nil;
        NSWindow* small = uievents::hostWindow(view, kTTRMinimumSize);
        (void)small;
        [view refreshNow];
        [map mouseMoved:uievents::mouse(NSEventTypeMouseMoved, map, [map pointForTime:4.55 pitch:72])];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-min.png"]);

        [view shutdown];
        kernel->deactivate();
        printf("%zu notes drawn, %zu selected\n", snap->notes.size(), picks.size());
        printf("%s\n", failures ? "FAILED to write some images" : "screenshots written");
        return failures ? 1 : 0;
    }
}
