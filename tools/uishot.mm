// Renders the plugin UI (real TTRMainView_v1, real kernel, analysed synthetic audio) into PNG files.
// Usage: tonetrace_uishot <output-dir>
#import <Cocoa/Cocoa.h>

#include <array>

#include "TestDrums.h"
#include "TestKernel.h"
#include "TestSignals.h"
#include "UiEvents.h"
#import "ui/TTRAbout.h"
#import "ui/TTRControls.h"
#import "ui/TTRMainView.h"
#import "ui/TTRMainViewInternal.h"
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
constexpr double kDemoSeconds = 8.4;

// A small A-minor tune in 4 bars at 120 bpm like a real mix: plucked sawtooth melody (one held note with vibrato), a plucked
// bass, soft pad chords, a full drum kit (kick on every beat, snare on 2 and 4, 8th and 16th hats) and a little noise,
// all slightly sharp of A = 440 Hz (like a sample from an old synth).
constexpr double kDetuneCents = 23.0;

struct DemoNote { int pitch; double start, length; bool held; };

const std::vector<DemoNote>& demoMelody() {
    static const std::vector<DemoNote> notes = {
        {69, 0.0, 0.5, false}, {72, 0.5, 0.5, false}, {76, 1.0, 0.5, false}, {74, 1.5, 0.25, false}, {72, 1.75, 0.25, false},
        {69, 2.0, 0.5, false}, {72, 2.5, 0.5, false}, {77, 3.0, 0.5, false}, {76, 3.5, 0.5, false},
        {74, 4.0, 0.5, false}, {71, 4.5, 0.5, false}, {67, 5.0, 0.5, false}, {71, 5.5, 0.5, false},
        {76, 6.0, 1.0, true}, {74, 7.0, 0.5, false}, {71, 7.5, 0.5, false}};
    return notes;
}

std::vector<float> demoSignal() {
    const double tune = std::pow(2.0, kDetuneCents / 1200.0);
    std::vector<tsig::HzTone> tones;
    for (const DemoNote& n : demoMelody())
        tones.push_back({tsig::midiToHz(n.pitch) * tune, n.start, n.start + n.length, 0.45f, true, n.held ? 14.0 : 0.0, 5.2, n.held ? 1.8 : 0.60});
    const int bass[4] = {45, 41, 43, 40};  // A F G E, a pluck on beat 1 and on the "and" of 2
    for (int bar = 0; bar < 4; ++bar) {
        tones.push_back({tsig::midiToHz(bass[bar]) * tune, bar * 2.0, bar * 2.0 + 0.9, 0.5f, true, 0.0, 5.0, 0.9});
        tones.push_back({tsig::midiToHz(bass[bar]) * tune, bar * 2.0 + 1.5, bar * 2.0 + 1.9, 0.4f, true, 0.0, 5.0, 0.7});
    }
    const int padRoots[4] = {57, 53, 55, 52};  // Am F G Em, one chord per bar, quiet
    for (int bar = 0; bar < 4; ++bar)
        for (const int offset : {0, 3, 7}) {
            const int third = padRoots[bar] == 53 || padRoots[bar] == 55 ? (offset == 3 ? 4 : offset) : offset;
            tones.push_back({tsig::midiToHz(padRoots[bar] + third) * tune, bar * 2.0, bar * 2.0 + 2.0, 0.07f, true, 6.0, 4.5, 0.0});
        }
    std::vector<float> signal = tsig::synthHz(tones, kRate, kDemoSeconds);
    std::vector<tdrum::Hit> hits, hats;
    for (int beat = 0; beat * 0.5 < kDemoSeconds - 0.45; ++beat) {
        const double at = beat * 0.5;
        hits.push_back({at, 'k'});
        if (beat % 2 == 1) hits.push_back({at, 's'});
        for (const double off : {0.125, 0.25, 0.375}) hats.push_back({at + off, 'h'});
    }
    const std::vector<float> drums = tdrum::render(hits, kRate, kDemoSeconds, 1.5f), hatTrack = tdrum::render(hats, kRate, kDemoSeconds, 4.0f);  // hats are mixed loud: they are thin
    for (size_t i = 0; i < signal.size() && i < drums.size(); ++i) signal[i] += drums[i] + hatTrack[i];
    tdrum::Noise noise;
    for (float& v : signal) v += 0.003f * noise.next();
    return signal;
}

// How the user draws after reading the spectrum: a drag along each band, snapped to the 1/16 grid.
void drawByDragging(TTRNoteMapView_v1* map, const std::vector<DemoNote>& notes) {
    for (const DemoNote& n : notes) {
        const NSPoint from = [map pointForTime:n.start + 0.02 pitch:n.pitch], to = [map pointForTime:n.start + n.length - 0.02 pitch:n.pitch];
        [map mouseDown:uievents::mouse(NSEventTypeLeftMouseDown, map, from)];
        for (int i = 1; i <= 4; ++i) [map mouseDragged:uievents::mouse(NSEventTypeLeftMouseDragged, map, NSMakePoint(from.x + (to.x - from.x) * i / 4, from.y))];
        [map mouseUp:uievents::mouse(NSEventTypeLeftMouseUp, map, to)];
    }
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
        std::vector<float> signal = demoSignal();
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
        NSWindow* win = uievents::hostWindow(view, kTTRDefaultSize);
        (void)win;
        TTRNoteMapView_v1* map = (TTRNoteMapView_v1*)uievents::find(view, TTRNoteMapView_v1.class);

        kernel->setParam(kParamRecord, 1.0f);
        tk::run(*kernel, signal, 0, size_t(4.6 * kRate));
        tk::waitFor([&] { return kernel->mapView().lineFrames > 100; });
        [view refreshNow];

        tk::run(*kernel, signal, size_t(4.6 * kRate), signal.size());
        kernel->setParam(kParamRecord, 0.0f);
        tk::run(*kernel, tk::silence(2));
        tk::waitFor([&] { return kernel->status() == KernelStatus::Idle && kernel->snapshot() && kernel->snapshot()->hasRecording() && kernel->snapshot()->lines; });
        [view refreshNow];
        const double detected = kernel->detectedTuningCents();
        printf("detected tuning %+.1f ct (sample was detuned by %+.1f ct)\n", detected, kDetuneCents);

        // draw the notes like a user: the first two bars of the melody and the bass, the sustained E5 by hand
        std::vector<DemoNote> toDraw;
        for (const DemoNote& n : demoMelody()) if (n.start < 4.0 || n.held) toDraw.push_back(n);
        for (int bar = 0; bar < 4; ++bar) toDraw.push_back({std::array<int, 4>{45, 41, 43, 40}[size_t(bar)], bar * 2.0, 0.9, false});
        drawByDragging(map, toDraw);
        const SnapshotPtr snap = kernel->snapshot();
        std::vector<int> picks;
        for (size_t i = 0; i < snap->notes.size(); ++i) if (snap->notes[i].pitch == 72 && snap->notes[i].startSec < 2.0) picks.push_back(int(i));
        kernel->setNotes(withSelection(snap->notes, picks, SelectMode::Replace));
        kernel->setPreviewStart(1.0);
        kernel->setPreviewLoop(2.0, 5.0);
        kernel->setParam(kParamPreview, 1.0f);
        tk::run(*kernel, tk::silence(160));  // advances the playhead into the loop
        [view refreshNow];
        // hover an empty spot: label and ghost note of the snap length
        const NSPoint cursor = [map pointForTime:5.13 pitch:79];
        [map mouseMoved:uievents::mouse(NSEventTypeMouseMoved, map, cursor)];
        [view refreshNow];
        view->_transientUntil = 0;  // no \"Grabbed ...\" message in the picture
        [view refreshNow];
        printf("hover label \"%s\", ghost \"%s\"\n", map.hoverLabel.UTF8String, map.ghostLabel.UTF8String);
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot.png"]);

        // ---- options popover open
        {
            [view toggleOptions];
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-options.png"]);
            [view toggleOptions];
            [view refreshNow];
        }

        // ---- the same document in the Lines view
        {
            LineView v = kernel->lineView();
            v.isSpectrum = false;
            kernel->setLineView(v);
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-lines.png"]);
            v.isSpectrum = true;
            kernel->setLineView(v);
            [view refreshNow];
        }

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
        [map mouseMoved:uievents::mouse(NSEventTypeMouseMoved, map, [map pointForTime:2.8 pitch:72])];
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-min.png"]);

        [view shutdown];
        kernel->deactivate();
        printf("%zu notes drawn, %zu selected\n", snap->notes.size(), picks.size());
        printf("%s\n", failures ? "FAILED to write some images" : "screenshots written");
        return failures ? 1 : 0;
    }
}
