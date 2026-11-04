#import "ui/TTRInstrumentPicker.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

#import "ui/TTRTheme.h"
#import "ui/TTRWidgets.h"

#include "core/Instruments.h"

namespace {

constexpr CGFloat kMaxPanelWidth = 920, kMargin = 24, kPad = 24, kHeaderH = 150, kFooterH = 68, kFamilyH = 30, kRowH = 26, kColumnGap = 20;
constexpr CGFloat kModeH = 30;
constexpr CGFloat kBox = 14, kChipW = 36, kChipH = 18, kButtonH = 34;
constexpr CGFloat kNarrowPanel = 760;  // below this the families share three columns instead of four
constexpr uint32_t kBoxOff = 0x4A4F57;

enum class HitKind { Program, Family, All, None, Cancel, Apply, KeepMode, FocusMode };

struct Hit {
    NSRect rect;
    HitKind kind;
    int program;  // Program: the instrument; Family: any member
};

using FamilyList = std::vector<std::pair<ttr::InstrumentFamily, std::vector<int>>>;

// The families in display order with their instruments.
FamilyList families() {
    FamilyList out;
    for (int p : ttr::selectableInstruments()) {
        const ttr::InstrumentFamily f = ttr::instrumentFamily(p);
        if (out.empty() || out.back().first != f) out.push_back({f, {}});
        out.back().second.push_back(p);
    }
    return out;
}

CGFloat blockHeight(const std::vector<int>& members) { return kFamilyH + CGFloat(members.size()) * kRowH + 8; }

}  // namespace

@implementation TTRInstrumentPicker_v1 {
    std::set<int> _chosen;  // every instrument when all are chosen
    std::vector<Hit> _hits;
    NSRect _panel;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.hidden = YES;
        _applyTitle = @"Apply";
        self.wantsLayer = YES;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)isShowing { return !self.hidden; }

- (void)setIsFocused:(BOOL)isFocused {
    _isFocused = isFocused;
    self.needsDisplay = YES;
}

- (void)showWithSelection:(const std::vector<int>&)programs focused:(BOOL)isFocused {
    _isFocused = isFocused;
    const std::vector<int> normal = ttr::normalizedSelection(programs);
    const std::vector<int>& all = ttr::selectableInstruments();
    _chosen = normal.empty() ? std::set<int>(all.begin(), all.end()) : std::set<int>(normal.begin(), normal.end());
    self.hidden = NO;
    [self.window makeFirstResponder:self];
    self.needsDisplay = YES;
}

- (void)dismiss {
    if (self.hidden) return;
    self.hidden = YES;
    if (self.window.firstResponder == self) [self.window makeFirstResponder:self.superview];
    if (_onClose) _onClose();
}

- (std::vector<int>)selection { return ttr::normalizedSelection(std::vector<int>(_chosen.begin(), _chosen.end())); }

- (void)toggleProgram:(int)program {
    if (_chosen.count(program)) _chosen.erase(program);
    else _chosen.insert(program);
    self.needsDisplay = YES;
}

- (void)toggleFamilyOf:(int)program {
    const ttr::InstrumentFamily family = ttr::instrumentFamily(program);
    std::vector<int> members;
    for (int p : ttr::selectableInstruments())
        if (ttr::instrumentFamily(p) == family) members.push_back(p);
    const bool isFull = std::all_of(members.begin(), members.end(), [&](int p) { return _chosen.count(p) > 0; });
    for (int p : members) {
        if (isFull) _chosen.erase(p);
        else _chosen.insert(p);
    }
    self.needsDisplay = YES;
}

- (void)selectAll:(BOOL)all {
    const std::vector<int>& programs = ttr::selectableInstruments();
    _chosen = all ? std::set<int>(programs.begin(), programs.end()) : std::set<int>();
    self.needsDisplay = YES;
}

- (void)apply {
    if (_chosen.empty()) return;  // the model needs at least one instrument
    const std::vector<int> programs = [self selection];
    if (_onApply) _onApply(programs, _isFocused);
    [self dismiss];
}

// ------------------------------------------------------------ layout

// Families flow top to bottom into the columns; a column ends once it holds its share of the total height.
- (std::vector<std::vector<size_t>>)columnsFor:(const FamilyList&)all {
    const int count = _panel.size.width < kNarrowPanel ? 3 : 4;
    CGFloat total = 0;
    for (const auto& f : all) total += blockHeight(f.second);
    const CGFloat share = total / count;
    std::vector<std::vector<size_t>> columns(1);
    CGFloat filled = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        const CGFloat h = blockHeight(all[i].second);
        if (!columns.back().empty() && filled + h / 2 > share && int(columns.size()) < count) {
            columns.emplace_back();
            filled = 0;
        }
        columns.back().push_back(i);
        filled += h;
    }
    return columns;
}

- (void)layoutPanel {
    const NSRect b = self.bounds;
    const CGFloat width = std::min(kMaxPanelWidth, b.size.width - 2 * kMargin);
    _panel = NSMakeRect(0, 0, width, 0);  // the width decides the column count
    const FamilyList all = families();
    CGFloat body = 0;
    for (const auto& column : [self columnsFor:all]) {
        CGFloat h = 0;
        for (size_t i : column) h += blockHeight(all[i].second);
        body = std::max(body, h);
    }
    const CGFloat height = std::min(b.size.height - 2 * kMargin, kHeaderH + body + kFooterH);
    _panel = NSMakeRect(std::floor((b.size.width - width) / 2), std::max(kMargin, std::floor((b.size.height - height) / 2)), width, height);
}

// ------------------------------------------------------------ drawing

- (void)drawBox:(NSRect)r state:(int)state {  // 0 off, 1 some, 2 on
    if (state == 0) {
        TTRStrokeRounded(r, 3, TTRHex(kBoxOff), 1.2);
        return;
    }
    TTRFillRounded(r, 3, TTR_ACCENT);
    NSBezierPath* p = [NSBezierPath bezierPath];
    p.lineWidth = 1.8;
    p.lineCapStyle = NSLineCapStyleRound;
    p.lineJoinStyle = NSLineJoinStyleRound;
    if (state == 1) {
        [p moveToPoint:NSMakePoint(NSMinX(r) + 3.5, NSMidY(r))];
        [p lineToPoint:NSMakePoint(NSMaxX(r) - 3.5, NSMidY(r))];
    } else {
        [p moveToPoint:NSMakePoint(NSMinX(r) + 3, NSMidY(r))];
        [p lineToPoint:NSMakePoint(NSMinX(r) + 6, NSMaxY(r) - 3.5)];
        [p lineToPoint:NSMakePoint(NSMaxX(r) - 3, NSMinY(r) + 3.5)];
    }
    [TTR_ON_ACCENT setStroke];
    [p stroke];
}

- (void)drawRadio:(NSRect)r isOn:(BOOL)isOn {
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(r, 0.6, 0.6)];
    ring.lineWidth = 1.2;
    [(isOn ? TTR_ACCENT : TTRHex(kBoxOff)) setStroke];
    [ring stroke];
    if (!isOn) return;
    [TTR_ACCENT setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(r, 3.5, 3.5)] fill];
}

- (void)drawMode:(NSString*)title detail:(NSString*)detail y:(CGFloat)y isOn:(BOOL)isOn kind:(HitKind)kind {
    const NSRect p = _panel;
    const NSRect row = NSMakeRect(p.origin.x + kPad, y, p.size.width - 2 * kPad, kModeH);
    [self drawRadio:NSMakeRect(row.origin.x, y + (kModeH - kBox) / 2, kBox, kBox) isOn:isOn];
    NSFont* font = TTRFont(13, NSFontWeightSemibold);
    const CGFloat titleW = TTRTextWidth(title, font);
    TTRDrawText(title, NSMakeRect(row.origin.x + kBox + 10, y, titleW + 4, kModeH), font, isOn ? TTR_TEXT : TTR_DIM);
    const CGFloat detailX = row.origin.x + kBox + 10 + titleW + 12;
    TTRDrawText(detail, NSMakeRect(detailX, y, std::max<CGFloat>(0, NSMaxX(row) - detailX), kModeH), TTRFont(12), TTR_DIM);
    _hits.push_back({row, kind, 0});
}

- (void)drawButton:(NSRect)r title:(NSString*)title isAccent:(BOOL)isAccent isEnabled:(BOOL)isEnabled kind:(HitKind)kind {
    TTRFillRounded(r, kTTRRadius, isAccent && isEnabled ? TTR_ACCENT : TTR_PANEL_RAISED);
    if (!isAccent) TTRStrokeRounded(r, kTTRRadius, TTR_BORDER);
    NSColor* color = !isEnabled ? TTRHex(0x5A5F67) : isAccent ? TTR_ON_ACCENT : TTR_TEXT;
    TTRDrawText(title, r, TTRFont(13, NSFontWeightSemibold), color, NSTextAlignmentCenter);
    if (isEnabled) _hits.push_back({r, kind, 0});
}

- (CGFloat)drawFamily:(const FamilyList::value_type&)family x:(CGFloat)x y:(CGFloat)y width:(CGFloat)w {
    const auto& members = family.second;
    const size_t on = size_t(std::count_if(members.begin(), members.end(), [&](int p) { return _chosen.count(p) > 0; }));
    [self drawBox:NSMakeRect(x, y + (kFamilyH - kBox) / 2, kBox, kBox) state:on == 0 ? 0 : on == members.size() ? 2 : 1];
    NSString* name = [[NSString stringWithUTF8String:ttr::instrumentFamilyName(family.first).c_str()] uppercaseString];
    TTRDrawText(name, NSMakeRect(x + kBox + 10, y, w - kBox - 10, kFamilyH), TTRFont(11, NSFontWeightBold), TTR_DIM, NSTextAlignmentLeft, 1.5);
    _hits.push_back({NSMakeRect(x, y, w, kFamilyH), HitKind::Family, members.front()});
    y += kFamilyH;
    for (int p : members) {
        const BOOL isOn = _chosen.count(p) > 0;
        [self drawBox:NSMakeRect(x, y + (kRowH - kBox) / 2, kBox, kBox) state:isOn ? 2 : 0];
        NSString* chip = [NSString stringWithUTF8String:ttr::instrumentAbbreviation(p).c_str()];
        TTRDrawChip(NSMakeRect(x + kBox + 10, y + (kRowH - kChipH) / 2, kChipW, kChipH), chip, TTRInstrumentRGB(p), !isOn);
        NSString* label = [NSString stringWithUTF8String:ttr::instrumentName(p).c_str()];
        TTRDrawText(label, NSMakeRect(x + kBox + kChipW + 20, y, w - kBox - kChipW - 20, kRowH), TTRFont(13), isOn ? TTR_TEXT : TTR_DIM);
        _hits.push_back({NSMakeRect(x, y, w, kRowH), HitKind::Program, p});
        y += kRowH;
    }
    return y + 8;
}

- (void)drawFooter:(CGFloat)top {
    const NSRect p = _panel;
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(p.origin.x, top, p.size.width, 1));
    const CGFloat y = top + (kFooterH - kButtonH) / 2;
    CGFloat x = p.origin.x + kPad;
    [self drawButton:NSMakeRect(x, y, 64, kButtonH) title:@"All" isAccent:NO isEnabled:YES kind:HitKind::All];
    x += 64 + 8;
    [self drawButton:NSMakeRect(x, y, 72, kButtonH) title:@"None" isAccent:NO isEnabled:YES kind:HitKind::None];
    x += 72 + 16;
    CGFloat right = NSMaxX(p) - kPad;
    const CGFloat applyW = std::max<CGFloat>(120, TTRTextWidth(_applyTitle, TTRFont(13, NSFontWeightSemibold)) + 36);
    [self drawButton:NSMakeRect(right - applyW, y, applyW, kButtonH) title:_applyTitle isAccent:YES isEnabled:!_chosen.empty() kind:HitKind::Apply];
    right -= applyW + 8;
    [self drawButton:NSMakeRect(right - 90, y, 90, kButtonH) title:@"Cancel" isAccent:NO isEnabled:YES kind:HitKind::Cancel];
    right -= 90 + 16;
    const size_t total = ttr::selectableInstruments().size();
    NSString* summary = [NSString stringWithFormat:@"%zu of %zu instruments", _chosen.size(), total];
    if (_chosen.empty()) summary = @"Choose at least one instrument";
    else if (_chosen.size() == total) summary = [NSString stringWithFormat:@"All %zu instruments", total];
    else if (_chosen.size() <= 2) summary = [NSString stringWithUTF8String:ttr::selectionSummary([self selection]).c_str()];
    TTRDrawText(summary, NSMakeRect(x, top, std::max<CGFloat>(0, right - x), kFooterH), TTRFont(12, NSFontWeightSemibold), _chosen.empty() ? TTRHex(0xFFB35C) : TTR_DIM);
}

- (void)drawRect:(NSRect)dirty {
    _hits.clear();
    [TTRHex(0x0B0C0E, 0.78) setFill];
    NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    [self layoutPanel];
    const NSRect p = _panel;
    TTRFillRounded(p, 10, TTR_PANEL);
    TTRStrokeRounded(p, 10, TTR_BORDER);
    TTRDrawText(@"Instruments", NSMakeRect(p.origin.x + kPad, p.origin.y + 16, p.size.width - 2 * kPad, 26), TTRFont(18, NSFontWeightSemibold), TTR_TEXT);
    TTRDrawText(@"Only the notes of the chosen instruments are shown, played and exported.", NSMakeRect(p.origin.x + kPad, p.origin.y + 44, p.size.width - 2 * kPad, 20),
                TTRFont(12), TTR_DIM);
    [self drawMode:@"Keep only these" detail:@"the model hears everything, the rest is hidden - exact instruments, changes apply at once"
                 y:p.origin.y + 72 isOn:!_isFocused kind:HitKind::KeepMode];
    [self drawMode:@"Model listens only for these" detail:@"digs out quiet parts, but notes of other instruments can end up on them - analyses again"
                 y:p.origin.y + 72 + kModeH isOn:_isFocused kind:HitKind::FocusMode];
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(p.origin.x + kPad, p.origin.y + kHeaderH - 12, p.size.width - 2 * kPad, 1));
    const FamilyList all = families();
    const auto columns = [self columnsFor:all];
    const CGFloat columnW = (p.size.width - 2 * kPad - kColumnGap * CGFloat(columns.size() - 1)) / CGFloat(columns.size());
    const CGFloat top = p.origin.y + kHeaderH, bottom = NSMaxY(p) - kFooterH;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(p.origin.x, top, p.size.width, std::max<CGFloat>(0, bottom - top)));
    for (size_t c = 0; c < columns.size(); ++c) {
        const CGFloat x = p.origin.x + kPad + CGFloat(c) * (columnW + kColumnGap);
        CGFloat y = top;
        for (size_t i : columns[c]) y = [self drawFamily:all[i] x:x y:y width:columnW];
    }
    [NSGraphicsContext restoreGraphicsState];
    _hits.erase(std::remove_if(_hits.begin(), _hits.end(), [&](const Hit& h) { return NSMaxY(h.rect) > bottom; }), _hits.end());  // clipped rows
    [self drawFooter:bottom];
}

// ------------------------------------------------------------ input

- (void)mouseDown:(NSEvent*)event {
    const NSPoint at = [self convertPoint:event.locationInWindow fromView:nil];
    if (!NSPointInRect(at, _panel)) { [self dismiss]; return; }  // a click beside the panel cancels
    for (const Hit& h : _hits) {
        if (!NSPointInRect(at, h.rect)) continue;
        switch (h.kind) {
            case HitKind::Program: [self toggleProgram:h.program]; break;
            case HitKind::Family: [self toggleFamilyOf:h.program]; break;
            case HitKind::All: [self selectAll:YES]; break;
            case HitKind::None: [self selectAll:NO]; break;
            case HitKind::Cancel: [self dismiss]; break;
            case HitKind::Apply: [self apply]; break;
            case HitKind::KeepMode: self.isFocused = NO; break;
            case HitKind::FocusMode: self.isFocused = YES; break;
        }
        return;
    }
}

- (void)keyDown:(NSEvent*)event {
    if (event.keyCode == 53) { [self dismiss]; return; }                       // Escape
    if (event.keyCode == 36 || event.keyCode == 76) { [self apply]; return; }  // Return, Enter
    [super keyDown:event];
}

- (void)scrollWheel:(NSEvent*)event {}  // the notes behind do not scroll while the panel is open

@end
