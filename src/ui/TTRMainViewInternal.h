#pragma once
#import "ui/TTRAbout.h"
#import "ui/TTRControls.h"
#import "ui/TTRMainView.h"
#import "ui/TTRNoteMapView.h"
#import "ui/TTROverlays.h"
#import "ui/TTRPianoKeys.h"
#import "ui/TTRWaveformView.h"

@interface TTRMainView_v1 () {
@public
    std::shared_ptr<ttr::TTRHostBridge> _bridge;
    std::shared_ptr<ttr::TonetraceKernel> _kernel;
    std::shared_ptr<TTRViewport> _vp;
    NSTimer* _timer;
    TTRWaveformView_v1* _wave;
    TTRNoteMapView_v1* _map;
    TTRPianoKeys_v1* _piano;
    TTRScrollBar_v1* _scroll;
    TTRKnob_v1 *_gainKnob, *_rangeKnob, *_quickGrabKnob, *_soundKnob, *_mixKnob;
    TTRButton_v1 *_speedMinus, *_speedPlus, *_hideDrumsButton, *_magnetButton, *_grabButton, *_resetButton, *_clearButton, *_previewButton, *_soloButton, *_saveButton;
    TTRButton_v1 *_undoButton, *_redoButton, *_deleteButton, *_duplicateButton, *_infoButton, *_syncButton;
    TTRBannerView_v1* _banner;
    TTRDiagnosticsView_v1* _diag;
    TTRLogoView_v1* _logoView;
    TTRAboutView_v1* _about;
    TTRReadout_v1 *_speedReadout, *_keyReadout, *_bpmReadout;
    TTRDropdown_v1* _snap;
    TTRRecordButton_v1* _recordButton;
    TTRDragButton_v1* _dragButton;
    TTRInfoBox_v1* _info;
    NSString* _statusText;
    NSString* _transientMessage;
    CFTimeInterval _transientUntil;
    NSString* _hoverHelp;
    double _tickHz;
    BOOL _isWindowVisible;
    unsigned long _lastDrawCount;
    CFTimeInterval _lastFpsTime;
    double _fps;
    double _storedScrollSec, _storedScrollY;
    NSSize _storedSize;
}
- (void)showMessage:(NSString*)message;
- (void)editParam:(int)paramId value:(float)value;  // begin + set + end: one discrete edit
- (void)viewportChanged;
@end

// Layout constants shared by construction, layout and drawing.
extern const CGFloat kTTRPanelWidth, kTTRMargin, kTTRPianoWidth, kTTRStatusHeight, kTTRScrollHeight;
extern const double kTTRActiveHz, kTTRIdleHz;

@interface TTRMainView_v1 (Layout)
- (void)layoutControls;
@end

@interface TTRMainView_v1 (Actions)
- (void)toggleRecord;
- (void)togglePreview;
- (void)stepSpeed:(int)direction;
- (void)performReset;
- (void)resetClicked;
- (void)grabClicked;
- (NSArray<NSString*>*)infoLines;
- (NSString*)statusLineForSnapshot:(const ttr::SnapshotPtr&)snap status:(ttr::KernelStatus)status;
@end

@interface TTRMainView_v1 (Export)
- (void)reportExportError:(NSString*)text;
- (NSURL*)writeNotesToMidiFile;
- (void)saveNotesAsMidi;
@end

// Timer rate, hover help, notices, diagnostics panel and the editor state (everything that is not a control).
@interface TTRMainView_v1 (System)
- (void)showBanner:(NSString*)text level:(ttr::NoticeLevel)level;
- (void)updateTimer;                 // 30 Hz while something moves, 5 Hz idle, off while the window is not visible
- (void)pollNotices;                 // kernel banners (state errors, recording full, ...)
- (void)toggleDiagnostics;
- (void)showAbout;
- (void)openDiagnostics;  // from the About overlay: opens the panel if it is closed
- (void)openLogFolder;
- (void)updateDiagnostics;
- (void)restoreUiSettings;
- (void)storeUiSettingsIfChanged;
- (void)updateEditStates;            // enabled state of Undo / Redo / Delete / Duplicate
- (void)undoClicked;
- (void)redoClicked;
- (void)deleteClicked;
- (void)duplicateClicked;
- (void)syncClicked;
- (void)drawStatusBar:(NSRect)status;
- (void)layoutStatusButtons;
@end
