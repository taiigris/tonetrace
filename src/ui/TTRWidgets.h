// Controls of the NeuralNote-style layout: transport buttons, a compact slider with end labels, the export-tempo stepper,
// a segmented level meter and a text pill.
#pragma once
#import <Cocoa/Cocoa.h>

typedef NS_ENUM(NSInteger, TTRTransportGlyph_v1) {
    TTRTransportRewind = 0,
    TTRTransportPlay,    // shows pause while isOn
    TTRTransportRecord,  // red dot, pulses while isOn
    TTRTransportFollow,  // [|] keep the playhead in view
    TTRTransportTrash,
    TTRTransportOptions,
    TTRTransportSpeaker,
    TTRTransportFolder,
    TTRTransportDownload,
};

// Square (or wide, with a title) flat button with a vector glyph.
@interface TTRGlyphButton_v1 : NSView
@property(nonatomic) TTRTransportGlyph_v1 glyph;
@property(nonatomic, copy) NSString* title;  // optional, drawn right of the glyph
@property(nonatomic) BOOL isOn;
@property(nonatomic) BOOL isEnabled;
@property(nonatomic) BOOL isAccent;  // outlined in the accent colour (the main action)
@property(nonatomic) BOOL isWarning;  // filled orange while on (MUTE)
@property(nonatomic, copy) void (^onClick)(void);
- (instancetype)initWithGlyph:(TTRTransportGlyph_v1)glyph title:(NSString*)title;
@end

// The same as a file drag source (Drag MIDI out).
@interface TTRGlyphDragButton_v1 : TTRGlyphButton_v1
@property(nonatomic, copy) NSURL* (^dragFileProvider)(void);
@end

// Horizontal slider 0..1 with optional end labels ("ORIG" ... "MIDI") or a value text right of it.
@interface TTRLineSlider_v1 : NSView
@property(nonatomic) double value;
@property(nonatomic) double defaultValue;  // double-click resets
@property(nonatomic, copy) NSString* leftLabel;
@property(nonatomic, copy) NSString* rightLabel;
@property(nonatomic, copy) NSString* (^valueText)(double value);  // drawn right when set
@property(nonatomic) uint32_t trackColor;  // filled part (0 = accent)
@property(nonatomic, copy) void (^onChange)(double value);
@property(nonatomic, copy) void (^onBeginEdit)(void);
@property(nonatomic, copy) void (^onEndEdit)(void);
@property(nonatomic, readonly) BOOL isDragging;
@end

// "EXPORT TEMPO  140 ⇕": drag vertically, scroll, or click the arrows.
@interface TTRStepper_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic) double value;
@property(nonatomic) double minValue, maxValue, step;
@property(nonatomic, copy) void (^onChange)(double value);
@end

// Segmented peak meter (green, yellow, red); `level` is linear peak 0..1+, decays smoothly.
@interface TTRMeter_v1 : NSView
@property(nonatomic) float level;
@end

// Rounded label ("MODEL: MEDIUM"); with onClick a button that opens a choice (hover, a small ▾ at the right).
@interface TTRPill_v1 : NSView
@property(nonatomic, copy) NSString* text;
@property(nonatomic) uint32_t textColor;
@property(nonatomic, copy) void (^onClick)(void);
@end

// Draws the three-letter instrument chip (rounded square, instrument colour) used by the strips and the drop zone.
void TTRDrawChip(NSRect rect, NSString* text, uint32_t rgb, BOOL dim);
