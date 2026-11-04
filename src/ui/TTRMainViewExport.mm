// MIDI export: drag source file and "Save .mid" fallback.
#import <Foundation/Foundation.h>

#import "analysis/KeyEstimator.h"
#import "bridge/BridgeClient.h"
#import "core/Instruments.h"
#import "core/Logger.h"
#import "midi/ExportPlanner.h"
#import "ui/TTRMainViewInternal.h"

static NSData* midiDataForSnapshot(const ttr::Snapshot& snap, int snapMode) {
    const std::vector<uint8_t> bytes = ttr::buildMidiForNotes(snap, snapMode, snap.key);
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

// Same name as an existing file gets " 2", " 3", ... so a clip that Live still holds open is never overwritten.
static NSURL* uniqueFileURL(NSURL* dir, NSString* fileName) {
    NSString* base = fileName.stringByDeletingPathExtension;
    NSURL* url = [dir URLByAppendingPathComponent:fileName];
    for (int n = 2; [NSFileManager.defaultManager fileExistsAtPath:url.path] && n < 1000; ++n)
        url = [dir URLByAppendingPathComponent:[NSString stringWithFormat:@"%@ %d.mid", base, n]];
    return url;
}

static NSString* midiNameForSnapshot(const ttr::Snapshot& snap) {
    NSString* name = [NSString stringWithUTF8String:ttr::midiFileNameFor(snap).c_str()];
    return [name stringByReplacingOccurrencesOfString:@"/" withString:@"-"];
}

@implementation TTRMainView_v1 (Export)

- (void)reportExportError:(NSString*)text {
    _kernel->diagnostics().setLastError(text.UTF8String);
    ttr::logError(_kernel->instanceId(), "MIDI export: %s", text.UTF8String);
    [self showBanner:text level:ttr::NoticeLevel::Error];
}

- (NSURL*)writeNotesToMidiFile {
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    if (!snap || snap->notes.empty()) {
        [self showMessage:@"Draw at least one note before dragging"];
        return nil;
    }
    NSURL* dir = [[NSFileManager.defaultManager URLsForDirectory:NSMusicDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"Tonetrace" isDirectory:YES];
    NSError* error = nil;
    if (!dir || ![NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:&error]) {
        [self reportExportError:[NSString stringWithFormat:@"The MIDI file could not be written: cannot create ~/Music/Tonetrace (%@)", error.localizedDescription ?: @"unknown error"]];
        return nil;
    }
    NSURL* url = uniqueFileURL(dir, midiNameForSnapshot(*snap));
    if (![midiDataForSnapshot(*snap, int(_kernel->param(ttr::kParamSnap))) writeToURL:url options:NSDataWritingAtomic error:&error]) {
        [self reportExportError:[NSString stringWithFormat:@"The MIDI file could not be written: %@", error.localizedDescription]];
        return nil;
    }
    _kernel->diagnostics().setLastMidiPath(url.path.UTF8String);
    ttr::logInfo(_kernel->instanceId(), "MIDI export: %s", url.path.UTF8String);
    const std::vector<ttr::NoteEvent> exported = ttr::notesForExport(snap->notes);
    const int count = int(exported.size()), tracks = int(ttr::instrumentsIn(exported).size());
    if (ttr::hasInstruments(exported) && tracks > 1)
        [self showMessage:[NSString stringWithFormat:@"%d notes in %d instrument tracks written to ~/Music/Tonetrace/%@ – drop into the arrangement", count, tracks, url.lastPathComponent]];
    else
        [self showMessage:[NSString stringWithFormat:@"%d notes written to ~/Music/Tonetrace/%@ – drop on a MIDI track", count, url.lastPathComponent]];
    if (_kernel->lineView().samplerOnDrag) {  // optional: the same notes as a sampler track in Live
        if (ttr::bridgeConnected(ttr::bridgeDirectory())) [self sendSamplerRequest];
        else [self showMessage:@"MIDI written - no sampler track, the Live bridge is not active"];
    }
    return url;
}

- (void)saveNotesAsMidi {
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    if (!snap || snap->notes.empty()) { [self showMessage:@"Draw at least one note first"]; return; }
    NSSavePanel* panel = [NSSavePanel savePanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = @[ @"mid" ];
#pragma clang diagnostic pop
    panel.nameFieldStringValue = midiNameForSnapshot(*snap);
    NSData* data = midiDataForSnapshot(*snap, int(_kernel->param(ttr::kParamSnap)));
    __weak TTRMainView_v1* weakSelf = self;
    auto kernel = _kernel;
    void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSError* error = nil;
        if ([data writeToURL:panel.URL options:NSDataWritingAtomic error:&error]) {
            kernel->diagnostics().setLastMidiPath(panel.URL.path.UTF8String);
            ttr::logInfo(kernel->instanceId(), "MIDI saved: %s", panel.URL.path.UTF8String);
            [weakSelf showMessage:[NSString stringWithFormat:@"Saved %@", panel.URL.lastPathComponent]];
        } else {
            [weakSelf reportExportError:[NSString stringWithFormat:@"The MIDI file could not be saved: %@", error.localizedDescription]];
        }
    };
    if (self.window) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}

@end
