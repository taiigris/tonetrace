// Custom-drawn controls: buttons, drag button, knob, dropdown, readout, record button, info box, scroll bar.
#pragma once
#import <Cocoa/Cocoa.h>

typedef NS_ENUM(NSInteger, TTRButtonKind_v1) {
    TTRButtonKindNormal = 0,
    TTRButtonKindPlay,     // triangle glyph, turns accent when on
    TTRButtonKindDrag,     // filled accent, grip glyph (the one highlighted action)
    TTRButtonKindIcon,     // square icon button of the header: `glyph` instead of a title (the title stays as its name)
};

typedef NS_ENUM(NSInteger, TTRIconGlyph_v1) {
    TTRIconNone = 0,
    TTRIconHelp,   // question mark
    TTRIconView,   // spectrum (off) / lines (on)
    TTRIconGear,
    TTRIconInfo,
};

@interface TTRButton_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic) TTRButtonKind_v1 kind;
@property(nonatomic) TTRIconGlyph_v1 glyph;
@property(nonatomic) BOOL isOn;
@property(nonatomic) BOOL isEnabled;
@property(nonatomic, copy) void (^onClick)(void);
- (instancetype)initWithTitle:(NSString*)title kind:(TTRButtonKind_v1)kind;
@end

// Button that starts a file drag session once the mouse moves while pressed.
@interface TTRDragButton_v1 : TTRButton_v1
@property(nonatomic, copy) NSURL* (^dragFileProvider)(void);
@property(nonatomic, copy) void (^onDragRejected)(void);
@end

@interface TTRKnob_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic) double value;  // 0..1
@property(nonatomic) double defaultValue;
@property(nonatomic, copy) NSString* (^valueFormatter)(double value);
@property(nonatomic, copy) void (^onChange)(double value);
@property(nonatomic, copy) void (^onBeginEdit)(void);  // gesture start (drag, wheel step or reset)
@property(nonatomic, copy) void (^onEndEdit)(void);
@property(nonatomic, readonly) BOOL isDragging;
@property(nonatomic) BOOL isCompact;  // dial on top, title and value below (narrow side panel)
@end

// Horizontal slider with a caps title and the value above the track (Quick Grab).
@interface TTRSlider_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic) double value;  // 0..1
@property(nonatomic) double defaultValue;
@property(nonatomic, copy) NSString* (^valueFormatter)(double value);
@property(nonatomic, copy) void (^onChange)(double value);
@property(nonatomic, copy) void (^onBeginEdit)(void);
@property(nonatomic, copy) void (^onEndEdit)(void);
@property(nonatomic, readonly) BOOL isDragging;
@end

// Panel behind the options: a flat surface with a border; its subviews are the option controls.
@interface TTRPopoverView_v1 : NSView
@end

@interface TTRDropdown_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSArray<NSString*>* items;
@property(nonatomic) NSInteger selectedIndex;
@property(nonatomic, copy) void (^onChange)(NSInteger index);
@end

@interface TTRReadout_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* value;
@property(nonatomic) BOOL isHighlighted;
@property(nonatomic) BOOL isStacked;  // title above the value
@end

// Large round record button: red dot, pulses while recording.
@interface TTRRecordButton_v1 : NSView
@property(nonatomic) BOOL isOn;
@property(nonatomic) BOOL isEnabled;
@property(nonatomic, copy) void (^onClick)(void);
@end

// Crosshair readout: note, note frequency, cursor frequency, time and bar position.
@interface TTRInfoBox_v1 : NSView
@property(nonatomic, copy) NSArray<NSString*>* lines;
@end

// Horizontal scroll bar for the shared time window.
@interface TTRScrollBar_v1 : NSView
@property(nonatomic) double contentLength;  // seconds
@property(nonatomic) double visibleLength;
@property(nonatomic) double offset;
@property(nonatomic, copy) void (^onScroll)(double offset);
@end
