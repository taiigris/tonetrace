// 4.7: whole-song import from the editor: the "Load Song…" button (open panel) and an audio file dropped anywhere on the
// editor. The kernel does the work in the background (decode, analysis, MuScriptor); this only picks the file.
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "core/SongImport.h"
#import "ui/TTRMainViewInternal.h"

static NSArray<NSString*>* songExtensions() {
    NSMutableArray<NSString*>* all = [NSMutableArray array];
    for (const std::string& ext : ttr::songFileExtensions()) [all addObject:[NSString stringWithUTF8String:ext.c_str()]];
    return all;
}

// The first dragged file the import can read, or nil.
static NSString* songPathInPasteboard(NSPasteboard* board) {
    NSArray<NSURL*>* urls = [board readObjectsForClasses:@[ NSURL.class ] options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL* url in urls)
        if (url.path && ttr::isSongFile(url.path.UTF8String)) return url.path;
    return nil;
}

@implementation TTRMainView_v1 (Song)

- (void)registerSongDrop {
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
}

- (void)loadSongClicked {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a song: Tonetrace transcribes every instrument (one MIDI track each)";
    if (@available(macOS 11.0, *)) {
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* ext in songExtensions()) {
            UTType* type = [UTType typeWithFilenameExtension:ext];
            if (type) [types addObject:type];
        }
        panel.allowedContentTypes = types;
    }
    __weak TTRMainView_v1* weakSelf = self;
    void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
        if (response == NSModalResponseOK && panel.URL.path) [weakSelf importSongAtPath:panel.URL.path];
    };
    if (self.window) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}

- (void)importSongAtPath:(NSString*)path {
    if (!ttr::isSongFile(path.UTF8String)) {
        [self showMessage:@"Not a song file - use wav, aiff, mp3, m4a, flac or caf"];
        return;
    }
    if (_kernel->status() != ttr::KernelStatus::Idle || _kernel->quickGrabRunning()) {
        [self showMessage:@"Busy - stop the recording or the running Grab first"];
        return;
    }
    if (!_kernel->startSongImport(path.UTF8String)) {
        [self showMessage:@"The song could not be started"];
        return;
    }
    _fitZoom = YES;  // the whole song fills the map once it is loaded
    [self showMessage:[NSString stringWithFormat:@"Loading %@ …", path.lastPathComponent]];
    [self refreshNow];
}

- (NSString*)songStatusLine {
    const int percent = int(_kernel->quickGrabProgress() * 100.0f);
    switch (_kernel->songImportStage()) {
        case ttr::TonetraceKernel::SongStage::Loading: return @"Song: loading the file – click Cancel to stop";
        case ttr::TonetraceKernel::SongStage::Analysing: return @"Song: analysing the spectrum – click Cancel to stop";
        case ttr::TonetraceKernel::SongStage::Transcribing: break;
    }
    return [NSString stringWithFormat:@"Song: transcribing every instrument %d %% – click Cancel to stop", percent];
}

// ------------------------------------------------------------ drop target

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    if (!songPathInPasteboard(sender.draggingPasteboard)) return NSDragOperationNone;
    [self showMessage:@"Drop to load the whole song"];
    return NSDragOperationCopy;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return songPathInPasteboard(sender.draggingPasteboard) ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString* path = songPathInPasteboard(sender.draggingPasteboard);
    if (!path) return NO;
    [self importSongAtPath:path];
    return YES;
}

@end
