// Main view: the "fit" zoom (the whole recording fills the map, the whole keyboard fills its height), the options popover and the help text.
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRViewportInput.h"

@implementation TTRMainView_v1 (Fit)

// Vertical fit: every semitone row of the keyboard is visible without scrolling. Time fit happens in the refresh (it needs the recording length).
- (void)applyFitToView {
    if (!_fitRows) return;
    const CGFloat height = _map.bounds.size.height;
    if (height < 1.0) return;
    _fitRowH = MAX(kTTRMinRowHeight, height / double(ttr::kNumNotes));
    _vp->rowH = _fitRowH;
    _vp->scrollY = 0.0;
}

- (void)toggleOptions {
    if (_options.hidden) {
        _options.hidden = NO;
        _optionsButton.isOn = YES;
        __weak TTRMainView_v1* weak = self;
        _outsideClickMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown handler:^NSEvent*(NSEvent* event) {
            TTRMainView_v1* self = weak;
            if (!self || event.window != self.window) return event;
            const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
            if (!NSPointInRect(p, self->_options.frame) && !NSPointInRect(p, self->_optionsButton.frame)) [self closeOptions];
            return event;
        }];
    } else {
        [self closeOptions];
    }
    [self refreshNow];
}

- (void)closeOptions {
    if (_outsideClickMonitor) { [NSEvent removeMonitor:_outsideClickMonitor]; _outsideClickMonitor = nil; }
    _options.hidden = YES;
    _optionsButton.isOn = NO;
}

- (void)showHelp {
    [self showMessage:@"Draw: click or drag on the map · Space: preview · Cmd+Z: undo · right-click: menu · hover a control for help"];
}

@end
