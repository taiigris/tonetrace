// Renders the plug-in UI (real TTRMainView_v1, real kernel, the synthetic demo song through a fake model) into PNG files:
// the empty window, a song while it is transcribed and played, the finished song, diagnostics, About, the minimum size.
// Usage: tonetrace_uishot <output-dir>
#import <Cocoa/Cocoa.h>

#include "TestDemoSong.h"
#include "TestKernel.h"
#include "UiEvents.h"
#import "ui/TTRAbout.h"
#import "ui/TTRMainView.h"
#import "ui/TTRMainViewInternal.h"

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

BOOL writePng(NSView* view, NSString* path) {
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

// Plays the song from where it is for `seconds` (the host's audio thread, simulated).
void play(TonetraceKernel& k, double seconds) { tk::run(k, tk::silence(size_t(seconds * tk::kRate / tk::kBlock))); }

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSString* dir = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"docs";
        [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        [TTRShotApp sharedApplication];
        int failures = 0;

        demo::installDecoder();
        auto model = std::make_shared<tk::FakeTranscriber>(demo::chunks(), true);
        setTranscriber(model);
        auto kernel = std::make_shared<TonetraceKernel>();
        kernel->prepare(tk::kRate, tk::kBlock);
        kernel->setHostTransport(120.0, 0.0);
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
        NSWindow* win = uievents::hostWindow(view, kTTRDefaultSize);
        (void)win;
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-empty.png"]);

        // A song while it is transcribed: three of seven chunks done, played up to the middle of what is known.
        [view importSongAtPath:@"/demo/Night Drive.wav"];
        tk::waitFor([&] { return kernel->document() && kernel->document()->audio; });
        for (int i = 0; i < 3; ++i) model->release();
        tk::waitFor([&] { return model->chunksDone.load() == 3 && kernel->document()->notes.size() > 100; });
        kernel->setParam(kParamMix, 0.65f);
        kernel->play();
        play(*kernel, 9.4);
        [view refreshNow];
        TTRPianoRoll_v1* roll = (TTRPianoRoll_v1*)uievents::find(view, TTRPianoRoll_v1.class);
        [roll mouseMoved:uievents::mouse(NSEventTypeMouseMoved, roll, NSMakePoint(roll.bounds.size.width * 0.26, roll.bounds.size.height * 0.47))];
        view->_transientUntil = 0;
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot.png"]);

        // Done, the pad muted, the bass a little louder: the strips show their state.
        model->releaseAll();
        tk::waitFor([&] { return kernel->document()->transcription.phase == TranscriptionPhase::Done; });
        kernel->setInstrumentMix(88, {true, false, 0.8f});
        kernel->setInstrumentMix(33, {false, false, 0.95f});
        play(*kernel, 6.0);
        [roll mouseMoved:uievents::mouse(NSEventTypeMouseMoved, roll, NSMakePoint(-50, -50))];  // pointer outside the notes
        view->_transientUntil = 0;
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-done.png"]);
        kernel->pause();

        {
            TTRInstrumentPicker_v1* picker = (TTRInstrumentPicker_v1*)uievents::find(view, TTRInstrumentPicker_v1.class);
            [picker showWithSelection:{26, 29, 33, 52, kDrumProgram} focused:NO];
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-instruments.png"]);
            [picker dismiss];
        }
        {
            kernel->diagnostics().setHost("Ableton Live 12.4.7");
            kernel->setChannels(2);
            [view toggleDiagnostics];
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-diagnostics.png"]);
            [view toggleDiagnostics];
        }
        {
            TTRAboutView_v1* about = (TTRAboutView_v1*)uievents::find(view, TTRAboutView_v1.class);
            [view showAbout];
            uievents::pump(0.3);
            [view refreshNow];
            failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-about.png"]);
            [about dismiss];
            uievents::pump(0.4);
        }

        win.contentView = nil;
        NSWindow* small = uievents::hostWindow(view, kTTRMinimumSize);
        (void)small;
        [view refreshNow];
        failures += !writePng(view, [dir stringByAppendingPathComponent:@"screenshot-min.png"]);

        const DocumentPtr doc = kernel->document();
        printf("%zu notes, %zu instruments shown\n", doc ? doc->notes.size() : size_t(0), view->_stripByProgram.size());
        [view shutdown];
        kernel->deactivate();
        kernel->clearDocument();
        setTranscriber(nullptr);
        printf("%s\n", failures ? "FAILED to write some images" : "screenshots written");
        return failures ? 1 : 0;
    }
}
