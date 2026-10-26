// UI interaction test: builds the real view with an analysed synthetic signal and sends synthetic NSEvents
// (mouseDown / Dragged / Up, keys) to the note map and the waveform lane.
#import <Cocoa/Cocoa.h>

#include <cmath>

#include "TestKernel.h"
#include "core/NoteEdit.h"
#include "TestSignals.h"
#include "UiEvents.h"
#import "ui/TTRAbout.h"
#import "ui/TTRControls.h"
#import "ui/TTRMainView.h"
#import "ui/TTRNoteMapView.h"
#import "ui/TTROverlays.h"
#import "ui/TTRWaveformView.h"

using namespace ttr;
using namespace uievents;

namespace {

constexpr double kRate = 44100.0;
constexpr double kGridStep = 0.125;  // 1/16 at 120 bpm
int gFailures = 0;

void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

bool onGrid(double t) { return std::fabs(t / kGridStep - std::round(t / kGridStep)) < 1e-6; }

// Left drag from `from` to `to` in `steps` events.
void drag(NSView* view, NSPoint from, NSPoint to, NSEventModifierFlags flags = 0, int steps = 8) {
    [view mouseDown:mouse(NSEventTypeLeftMouseDown, view, from, flags)];
    for (int i = 1; i <= steps; ++i) {
        const NSPoint p = NSMakePoint(from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps);
        [view mouseDragged:mouse(NSEventTypeLeftMouseDragged, view, p, flags)];
    }
    [view mouseUp:mouse(NSEventTypeLeftMouseUp, view, to, flags)];
}

}  // namespace

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        auto kernel = std::make_shared<TonetraceKernel>();
        kernel->prepare(kRate, tk::kBlock);
        kernel->setHostTransport(120.0, 0.0);
        // a held A4 (MIDI 69) from 1.0 s to 3.0 s with a faint C#5 above it
        const auto signal = tsig::synth({{69, 1.0, 3.0, 0.5f}, {73, 1.0, 3.0, 0.05f}}, kRate, 4.0, true);

        printf("1. View with an analysed signal\n");
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:std::make_shared<DirectHostBridge>(kernel)];
        NSWindow* window = hostWindow(view, kTTRDefaultSize);
        (void)window;
        check(tk::record(*kernel, signal), "recorded and analysed (spectrogram and peaks present)");
        [view refreshNow];
        TTRNoteMapView_v1* map = (TTRNoteMapView_v1*)find(view, TTRNoteMapView_v1.class);
        TTRWaveformView_v1* lane = (TTRWaveformView_v1*)find(view, TTRWaveformView_v1.class);
        TTRDragButton_v1* dragButton = (TTRDragButton_v1*)find(view, TTRDragButton_v1.class);
        check(map && lane && dragButton, "map, waveform lane and Drag MIDI button found in the view");
        if (!map || !lane || !dragButton) return 1;
        check(kernel->snapshot()->notes.empty() && !dragButton.isEnabled, "no notes yet: Drag MIDI is inactive");

        printf("2. Draw a note by click-dragging (Snap 1/16, Magnet on)\n");
        drag(map, [map pointForTime:1.04 pitch:70], [map pointForTime:1.55 pitch:70]);  // starts one row too high: the magnet pulls it down
        [view refreshNow];
        auto notes = kernel->snapshot()->notes;
        check(notes.size() == 1, "one note was created");
        if (notes.size() == 1) {
            check(notes[0].pitch == 69, "pitch 69 (A4): the magnet pulled the cursor row 70 down to the bright line");
            check(std::fabs(notes[0].startSec - 1.0) < 1e-6 && std::fabs(notes[0].endSec - 1.5) < 1e-6, "start 1.000 s, end 1.500 s: snapped to 1/16");
        }
        check(dragButton.isEnabled, "Drag MIDI is active now");
        check(kernel->canUndo(), "the drawing is one undo step");

        printf("3. Alt = no snap, magnet off\n");
        kernel->setParam(kParamMagnet, 0.0f);
        drag(map, [map pointForTime:2.013 pitch:72], [map pointForTime:2.337 pitch:72], NSEventModifierFlagOption);
        notes = kernel->snapshot()->notes;
        check(notes.size() == 2, "second note created");
        bool free = false;
        for (const NoteEvent& n : notes) free |= n.pitch == 72 && !onGrid(n.startSec) && std::fabs(n.startSec - 2.013) < 0.02;
        check(free, "pitch 72 stays on the row under the cursor (magnet off) and the start is off the grid (Alt)");
        kernel->setParam(kParamMagnet, 1.0f);

        printf("4. Move, resize, delete\n");
        const NSPoint grab = [map pointForTime:1.25 pitch:69];
        drag(map, grab, NSMakePoint(grab.x + 0.5 * 100.0, [map pointForTime:1.25 pitch:71].y));  // +0.5 s, +2 semitones
        notes = kernel->snapshot()->notes;
        bool moved = false;
        for (const NoteEvent& n : notes) moved |= n.pitch == 71 && std::fabs(n.startSec - 1.5) < 1e-6 && std::fabs(n.endSec - 2.0) < 1e-6 && n.selected;
        check(moved, "dragging the middle moved the note to 1.5-2.0 s, pitch 71, and selected it");

        const NSPoint edge = [map pointForTime:2.0 pitch:71];
        drag(map, NSMakePoint(edge.x - 2, edge.y), NSMakePoint(edge.x - 2 + 25, edge.y));  // right edge +0.25 s
        notes = kernel->snapshot()->notes;
        bool longer = false;
        for (const NoteEvent& n : notes) longer |= n.pitch == 71 && std::fabs(n.startSec - 1.5) < 1e-6 && std::fabs(n.endSec - 2.25) < 1e-6;
        check(longer, "dragging the right edge changed the length to 0.75 s");

        const NSPoint onNote = [map pointForTime:1.8 pitch:71];
        [map mouseDown:mouse(NSEventTypeLeftMouseDown, map, onNote, 0, 1)];
        [map mouseUp:mouse(NSEventTypeLeftMouseUp, map, onNote, 0, 1)];
        [map mouseDown:mouse(NSEventTypeLeftMouseDown, map, onNote, 0, 2)];
        [map mouseUp:mouse(NSEventTypeLeftMouseUp, map, onNote, 0, 2)];
        check(kernel->snapshot()->notes.size() == 1, "double-click deleted the note");
        check([map performKeyEquivalent:key(map, @"z", 6, NSEventModifierFlagCommand)] && kernel->snapshot()->notes.size() == 2, "Cmd+Z undid the deletion");
        check([map performKeyEquivalent:key(map, @"z", 6, NSEventModifierFlagCommand | NSEventModifierFlagShift)] && kernel->snapshot()->notes.size() == 1,
              "Cmd+Shift+Z redid it");
        [map undoEdit];

        const NSPoint rightClick = [map pointForTime:2.1 pitch:72];
        NSMenu* menu = [map menuForEvent:mouse(NSEventTypeRightMouseDown, map, rightClick)];
        check(menu.numberOfItems == 5 && [[menu itemAtIndex:0].title isEqualToString:@"Delete"] && [[menu itemAtIndex:1].title isEqualToString:@"Duplicate"] &&
              [[menu itemAtIndex:3].title isEqualToString:@"Select all"] && [[menu itemAtIndex:4].title isEqualToString:@"Clear notes"],
              "right-click opens the menu: Delete, Duplicate, Select all, Clear notes");
        check(selectedCount(kernel->snapshot()->notes) == 1 && [menu itemAtIndex:0].enabled, "right-click selected the note under the pointer");
        const size_t menuBase = kernel->snapshot()->notes.size();
        [menu performActionForItemAtIndex:1];
        check(kernel->snapshot()->notes.size() == menuBase + 1, "menu Duplicate copied the selected note");
        [menu performActionForItemAtIndex:0];
        check(kernel->snapshot()->notes.size() == menuBase, "menu Delete removed the selected copy");
        [menu performActionForItemAtIndex:3];
        check(size_t(selectedCount(kernel->snapshot()->notes)) == kernel->snapshot()->notes.size(), "menu Select all");
        [menu performActionForItemAtIndex:4];
        check(kernel->snapshot()->notes.empty(), "menu Clear notes removed every note");
        [map undoEdit];
        check(kernel->snapshot()->notes.size() == menuBase, "Clear notes is undoable");

        printf("5. Lasso, keyboard, Drag MIDI button\n");
        [map undoEdit];
        while ([map respondsToSelector:@selector(undoEdit)] && kernel->canUndo()) [map undoEdit];
        kernel->editNotes({makeNote(69, 1.0, 3.0, kernel->timeGrid(), true), makeNote(71, 1.5, 2.25, kernel->timeGrid(), true)});
        drag(map, [map pointForTime:0.5 pitch:80], [map pointForTime:3.5 pitch:60], NSEventModifierFlagCommand);
        check(selectedCount(kernel->snapshot()->notes) == 2, "Cmd-drag lasso selected both notes");
        [map keyDown:key(map, @"\x7f", 51)];  // Backspace
        check(kernel->snapshot()->notes.empty(), "Backspace deleted the selected notes");
        [view refreshNow];
        check(!dragButton.isEnabled, "Drag MIDI is inactive again without notes");
        [map undoEdit];
        [view refreshNow];
        check(dragButton.isEnabled, "Drag MIDI is active after undo");

        printf("6. Waveform lane: preview start and loop\n");
        const NSPoint a = [lane pointForTime:1.0], b = [lane pointForTime:2.5];
        [lane mouseDown:mouse(NSEventTypeLeftMouseDown, lane, a)];
        [lane mouseUp:mouse(NSEventTypeLeftMouseUp, lane, a)];
        check(std::fabs(kernel->previewStart() - 1.0) < 0.011, "a click sets the preview start to 1.0 s");
        drag(lane, a, b);
        double loopA = 0, loopB = 0;
        check(kernel->previewLoop(&loopA, &loopB) && std::fabs(loopA - 1.0) < 0.011 && std::fabs(loopB - 2.5) < 0.011, "dragging sets the loop region 1.0 - 2.5 s");

        printf("7. Panel buttons, diagnostics, banner, sync\n");
        kernel->setNotes(withAllSelected(kernel->snapshot()->notes, false));
        [view refreshNow];
        NSArray<TTRButton_v1*>* buttons = [[view subviews] filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(id v, id) { return [v isKindOfClass:TTRButton_v1.class]; }]];
        auto buttonNamed = [&](NSString* title) -> TTRButton_v1* { for (TTRButton_v1* b in buttons) if ([b.title isEqualToString:title]) return b; return nil; };
        TTRButton_v1 *undoB = buttonNamed(@"Undo"), *redoB = buttonNamed(@"Redo"), *deleteB = buttonNamed(@"Delete"), *dupB = buttonNamed(@"Duplicate");
        TTRButton_v1 *infoB = buttonNamed(@"ⓘ"), *syncB = buttonNamed(@"Sync");
        check(undoB && redoB && deleteB && dupB && infoB && syncB, "Undo, Redo, Delete, Duplicate, ⓘ and Sync buttons exist");
        check(!deleteB.isEnabled && !dupB.isEnabled, "Delete and Duplicate are disabled without a selection");
        kernel->setNotes(withAllSelected(kernel->snapshot()->notes, true));
        [view refreshNow];
        check(deleteB.isEnabled && dupB.isEnabled && undoB.isEnabled, "Delete and Duplicate follow the selection");
        const size_t before = kernel->snapshot()->notes.size();
        dupB.onClick();
        check(kernel->snapshot()->notes.size() == before * 2, "Duplicate button duplicates");
        undoB.onClick();
        check(kernel->snapshot()->notes.size() == before && redoB.isEnabled, "Undo button undoes, Redo becomes available");
        redoB.onClick();
        check(kernel->snapshot()->notes.size() == before * 2, "Redo button redoes");
        deleteB.onClick();
        check(kernel->snapshot()->notes.size() < before * 2 && selectedCount(kernel->snapshot()->notes) == 0, "Delete button deletes the selection");
        undoB.onClick();
        undoB.onClick();

        check(!kernel->syncToHost(), "Sync to host is off by default");
        syncB.onClick();
        check(kernel->syncToHost() && syncB.isOn, "Sync button switches it on");
        syncB.onClick();

        TTRDiagnosticsView_v1* diag = (TTRDiagnosticsView_v1*)find(view, TTRDiagnosticsView_v1.class);
        TTRBannerView_v1* banner = (TTRBannerView_v1*)find(view, TTRBannerView_v1.class);
        check(diag && diag.hidden, "the diagnostics panel starts closed");
        infoB.onClick();
        [view refreshNow];
        check(!diag.hidden, "the ⓘ button opens the diagnostics panel");
        NSString* report = [diag reportText];
        check([report containsString:@"Tonetrace 4.0.0"] && [report containsString:@"Sample rate: 44100"] && [report containsString:@"DSP load"], "the report names version, sample rate and DSP load");
        [diag copyReport];
        check([[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] containsString:@"Tonetrace 4.0.0"], "Copy report puts the report on the clipboard");
        infoB.onClick();
        check(diag.hidden, "the panel closes again");

        // About overlay: logo click opens it, Esc / click outside / close button close it, Diagnostics link opens the panel
        TTRLogoView_v1* logo = (TTRLogoView_v1*)find(view, TTRLogoView_v1.class);
        TTRAboutView_v1* about = (TTRAboutView_v1*)find(view, TTRAboutView_v1.class);
        check(logo && about && about.hidden && !about.isShowing, "the logo exists and the About overlay starts closed");
        logo.onClick();
        pump(0.3);
        check(about.isShowing && !about.hidden, "a click on the logo opens the About overlay");
        [about keyDown:key(about, @"\x1b", 53)];
        check(!about.isShowing, "Esc closes the About overlay");
        pump(0.3);
        check(about.hidden, "the overlay is hidden after the fade-out");
        logo.onClick();
        [about mouseDown:mouse(NSEventTypeLeftMouseDown, about, NSMakePoint(3, 3))];
        check(!about.isShowing, "a click beside the card closes the About overlay");
        pump(0.3);
        logo.onClick();
        about.onOpenDiagnostics();
        check(diag.hidden == NO, "the Diagnostics link opens the diagnostics panel");
        [diag setHidden:YES];
        [about dismiss];
        pump(0.3);

        check(!banner.isShowing, "no banner without a notice");
        kernel->notice("Test notice", NoticeLevel::Error);
        [view refreshNow];
        check(banner.isShowing && [banner.text isEqualToString:@"Test notice"], "a kernel notice becomes a banner");
        [banner hideBanner];

        [view shutdown];
        kernel->deactivate();
        printf("%s\n", gFailures == 0 ? "UI INTERACTION TEST PASSED" : "UI INTERACTION TEST FAILED");
        return gFailures == 0 ? 0 : 1;
    }
}
