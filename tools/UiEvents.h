// Helpers for the UI tools: an offscreen window host, synthetic mouse / key events and a view finder.
#pragma once
#import <Cocoa/Cocoa.h>

namespace uievents {

inline NSWindow* hostWindow(NSView* view, NSSize size) {
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height) styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    w.contentView = view;
    view.frame = NSMakeRect(0, 0, size.width, size.height);
    return w;
}

// Location in `view` coordinates, converted to window coordinates for the event.
inline NSEvent* mouse(NSEventType type, NSView* view, NSPoint inView, NSEventModifierFlags flags = 0, NSInteger clickCount = 1) {
    const NSPoint inWindow = [view convertPoint:inView toView:nil];
    return [NSEvent mouseEventWithType:type location:inWindow modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime
                          windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:clickCount pressure:1.0];
}

inline NSEvent* key(NSView* view, NSString* characters, unsigned short keyCode, NSEventModifierFlags flags = 0) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime
                        windowNumber:view.window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:keyCode];
}

inline NSView* find(NSView* root, Class cls) {
    if ([root isKindOfClass:cls]) return root;
    for (NSView* sub in root.subviews)
        if (NSView* hit = find(sub, cls)) return hit;
    return nil;
}

inline void pump(double seconds = 0.05) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]]; }

}  // namespace uievents
