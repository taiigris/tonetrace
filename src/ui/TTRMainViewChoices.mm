// The model choices: the size (Small / Medium / Large, a menu on the model pill) and the instruments the model listens
// for (the picker opened from the sidebar). A new choice re-analyses the loaded song while it keeps playing.
#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/Logger.h"
#import "ui/TTRMainViewInternal.h"

@implementation TTRMainView_v1 (Choices)

- (BOOL)canReanalyse { return _doc && _doc->audio && !_kernel->isRecording(); }

- (NSString*)titleForModelSize:(ttr::ModelSize)size {
    const ttr::ModelSizeInfo& info = ttr::modelSizeInfo(size);
    NSString* where = ttr::isModelAvailable(size) ? @"on this Mac" : [NSString stringWithFormat:@"downloads %s", info.downloadText];
    return [NSString stringWithFormat:@"%s  -  %s  (%@)", info.label, info.character, where];
}

- (void)showModelMenu {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Transcription model"];
    menu.autoenablesItems = NO;
    NSMenuItem* header = [menu addItemWithTitle:@"Transcription model" action:nil keyEquivalent:@""];
    header.enabled = NO;
    const ttr::ModelSize current = ttr::modelSize();
    for (int i = 0; i < ttr::kNumModelSizes; ++i) {
        NSMenuItem* item = [menu addItemWithTitle:[self titleForModelSize:ttr::ModelSize(i)] action:@selector(modelMenuItemChosen:) keyEquivalent:@""];
        item.target = self;
        item.tag = i;
        item.state = ttr::ModelSize(i) == current ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* note = [menu addItemWithTitle:@"Runs on this Mac. Weights CC BY-NC 4.0 (non-commercial use)" action:nil keyEquivalent:@""];
    note.enabled = NO;
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSMaxY(_modelPill.bounds) + 4) inView:_modelPill];
}

- (void)modelMenuItemChosen:(NSMenuItem*)item { [self chooseModelSize:int(item.tag)]; }

- (void)chooseModelSize:(int)size {
    if (size < 0 || size >= ttr::kNumModelSizes) return;
    const ttr::ModelSize chosen = ttr::ModelSize(size);
    if (chosen == ttr::modelSize()) return;
    ttr::setModelSize(chosen);
    const ttr::ModelSizeInfo& info = ttr::modelSizeInfo(chosen);
    ttr::logInfo(_kernel->instanceId(), "model size chosen: %s", info.id);
    if ([self canReanalyse] && _kernel->reanalyse()) {
        [self showMessage:[NSString stringWithFormat:@"Analysing again with the %s model", info.label]];
    } else {
        [self showMessage:[NSString stringWithFormat:@"The %s model transcribes the next song", info.label]];
    }
    [self refreshNow];
}

- (void)showInstrumentPicker {
    _picker.applyTitle = @"Apply";
    [_picker showWithSelection:_kernel->instrumentSelection() focused:_kernel->modelFocus()];
}

// Without focus a new selection only filters the full analysis (at once); a new focus needs the model again.
- (void)applyInstrumentSelection:(const std::vector<int>&)programs focused:(BOOL)isFocused {
    _kernel->setInstrumentSelection(programs);
    _kernel->setModelFocus(isFocused);
    const std::vector<int> selection = _kernel->instrumentSelection();
    NSString* summary = [NSString stringWithUTF8String:ttr::selectionSummary(selection).c_str()];
    if ([self canReanalyse] && _kernel->needsReanalysis() && _kernel->reanalyse()) {
        [self showMessage:isFocused && !selection.empty() ? [NSString stringWithFormat:@"Analysing again, listening only for %@", summary]
                                                          : @"Analysing again with every instrument"];
    } else {
        [self showMessage:[NSString stringWithFormat:@"Showing %@", summary]];
    }
    [self refreshNow];
}

@end
