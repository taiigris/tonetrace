// Overlay that chooses the instruments: the 35 instruments the model knows, grouped by family (a family name toggles the
// whole family), All / None, how the choice is used (keep only these from the full analysis, or let the model listen
// only for them) and Apply.
#pragma once
#import <Cocoa/Cocoa.h>

#include <vector>

@interface TTRInstrumentPicker_v1 : NSView
// Empty = every instrument. `isFocused`: the model listens only for them (else: keep them from the full analysis).
@property(nonatomic, copy) void (^onApply)(const std::vector<int>& programs, BOOL isFocused);
@property(nonatomic, copy) void (^onClose)(void);
@property(nonatomic, copy) NSString* applyTitle;  // the main button ("Apply")
@property(nonatomic, readonly) BOOL isShowing;
- (void)showWithSelection:(const std::vector<int>&)programs focused:(BOOL)isFocused;
- (void)dismiss;
// For tests and keyboard use: toggles one instrument or a whole family, as a click would.
- (void)toggleProgram:(int)program;
- (void)toggleFamilyOf:(int)program;
- (void)selectAll:(BOOL)all;
@property(nonatomic) BOOL isFocused;
- (void)apply;
- (std::vector<int>)selection;  // normalised (empty = all)
@end
