#import "ui/TTRInstrumentStrip.h"

#include <cmath>

#include "core/Instruments.h"
#import "ui/TTRTheme.h"
#import "ui/TTRWidgets.h"

static const CGFloat kPad = 20, kChipW = 38, kChipH = 26, kToggleW = 28, kToggleH = 24;

// Small square M / S toggle.
@interface TTRStripToggle_v1 : NSView
@property(nonatomic, copy) NSString* letter;
@property(nonatomic) BOOL isOn;
@property(nonatomic) uint32_t onColor;
@property(nonatomic, copy) void (^onClick)(void);
@end

@implementation TTRStripToggle_v1
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)setIsOn:(BOOL)isOn { if (isOn != _isOn) { _isOn = isOn; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent*)event { if (_onClick) _onClick(); }
- (void)drawRect:(NSRect)dirty {
    TTRFillRounded(self.bounds, 4, _isOn ? TTRHex(_onColor, 0.85) : TTR_PANEL_RAISED);
    TTRDrawText(_letter, self.bounds, TTRMono(11, NSFontWeightBold), _isOn ? TTRHex(0x111214) : TTR_DIM, NSTextAlignmentCenter);
}
@end

@implementation TTRInstrumentStrip_v1 {
    ttr::InstrumentMix _mix;
    TTRStripToggle_v1 *_mute, *_solo;
    TTRLineSlider_v1* _fader;
    TTRMeter_v1* _meter;
    NSString* _name;
    NSString* _chip;
    uint32_t _color;
}

- (instancetype)initWithProgram:(int)program {
    self = [super initWithFrame:NSMakeRect(0, 0, 300, kTTRStripHeight)];
    if (!self) return nil;
    _program = program;
    _name = [NSString stringWithUTF8String:ttr::instrumentName(program).c_str()];
    _chip = [NSString stringWithUTF8String:ttr::instrumentAbbreviation(program).c_str()];
    _color = TTRInstrumentRGB(program);
    _isAudible = YES;
    __weak TTRInstrumentStrip_v1* weak = self;
    _mute = [[TTRStripToggle_v1 alloc] initWithFrame:NSZeroRect];
    _mute.letter = @"M";
    _mute.onColor = 0xE08A2E;
    _mute.toolTip = [NSString stringWithFormat:@"Mute %@ (playback and export)", _name];
    _mute.onClick = ^{ [weak toggleMute]; };
    _solo = [[TTRStripToggle_v1 alloc] initWithFrame:NSZeroRect];
    _solo.letter = @"S";
    _solo.onColor = 0xE5C14D;
    _solo.toolTip = [NSString stringWithFormat:@"Solo %@ (playback and export)", _name];
    _solo.onClick = ^{ [weak toggleSolo]; };
    _fader = [[TTRLineSlider_v1 alloc] initWithFrame:NSZeroRect];
    _fader.trackColor = _color;
    _fader.defaultValue = ttr::kUnityFader;
    _fader.value = _mix.gain;
    _fader.valueText = ^NSString*(double v) {
        const float db = ttr::faderToDb(float(v));
        return std::isfinite(db) ? [NSString stringWithFormat:@"%.1f", db] : @"-inf";
    };
    _fader.toolTip = @"Volume of the notes in the playback (double-click: 0 dB)";
    _fader.onChange = ^(double v) { [weak faderMoved:v]; };
    _meter = [[TTRMeter_v1 alloc] initWithFrame:NSZeroRect];
    for (NSView* v in @[ _mute, _solo, _fader, _meter ]) [self addSubview:v];
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)layout {
    [super layout];
    const CGFloat w = self.bounds.size.width;
    _solo.frame = NSMakeRect(w - kPad - kToggleW, 18, kToggleW, kToggleH);
    _mute.frame = NSMakeRect(w - kPad - 2 * kToggleW - 6, 18, kToggleW, kToggleH);
    const CGFloat textX = kPad + kChipW + 14;
    _fader.frame = NSMakeRect(textX - 6, 62, w - textX - kPad + 6, 20);
    _meter.frame = NSMakeRect(textX, 90, w - textX - kPad - 44, 4);
}

- (void)emit {
    if (_onMixChange) _onMixChange(_program, _mix);
}
- (void)toggleMute { _mix.mute = !_mix.mute; _mute.isOn = _mix.mute; [self emit]; }
- (void)toggleSolo { _mix.solo = !_mix.solo; _solo.isOn = _mix.solo; [self emit]; }
- (void)faderMoved:(double)v { _mix.gain = float(v); [self emit]; }

- (void)setMix:(const ttr::InstrumentMix&)mix {
    _mix = mix;
    _mute.isOn = mix.mute;
    _solo.isOn = mix.solo;
    if (!_fader.isDragging) _fader.value = mix.gain;
}

- (void)setLevel:(float)level { _meter.level = level; }
- (void)setInfo:(NSString*)info { if (![_info isEqualToString:info]) { _info = [info copy]; self.needsDisplay = YES; } }
- (void)setIsAudible:(BOOL)isAudible { if (isAudible != _isAudible) { _isAudible = isAudible; self.needsDisplay = YES; } }

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRDrawChip(NSMakeRect(kPad, 18, kChipW, kChipH), _chip, _color, !_isAudible);
    const CGFloat textX = kPad + kChipW + 14;
    TTRDrawText(_name, NSMakeRect(textX, 12, b.size.width - textX - 90, 22), TTRFont(16, NSFontWeightSemibold), _isAudible ? TTR_TEXT : TTR_DIM);
    TTRDrawText(_info ?: @"", NSMakeRect(textX, 34, b.size.width - textX - 20, 16), TTRMono(11, NSFontWeightRegular), TTR_DIM, NSTextAlignmentLeft, 0.3);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, b.size.height - 1, b.size.width, 1));
}

@end
