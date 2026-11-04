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
    TTRKnob_v1 *_gainKnob, *_rangeKnob, *_soundKnob, *_mixKnob;
    TTRSlider_v1* _quickGrabSlider;
    TTRButton_v1 *_speedMinus, *_speedPlus, *_hideDrumsButton, *_magnetButton, *_grabButton, *_resetButton, *_clearButton, *_previewButton, *_soloButton, *_saveButton;
    TTRButton_v1 *_waveButton, *_fundamentalsButton, *_alignButton, *_tuningMinus, *_tuningPlus, *_engineButton;
    TTRReadout_v1 *_tuningReadout, *_bridgeReadout;
    TTRButton_v1 *_grabOnClickButton, *_samplerButton, *_samplerDragButton;
    TTRButton_v1 *_helpButton, *_viewButton, *_optionsButton;
    BOOL _samplerPending, _bridgeUp;  // a request waits for Live's answer / the heartbeat is fresh
    CFTimeInterval _bridgeChecked;
    TTRButton_v1* _loadSongButton;  // 4.7: load a whole song file
    TTRButton_v1 *_undoButton, *_redoButton, *_deleteButton, *_duplicateButton, *_infoButton, *_syncButton;
    TTRPopoverView_v1* _options;
    id _outsideClickMonitor;      // closes the options popover on a click anywhere else
    BOOL _fitZoom, _fitRows;      // the whole recording fills the map width / the whole keyboard fills its height
    double _fitRowH;              // row height the last vertical fit applied
    NSRect _headerClip;           // centre of the header (status text)
    NSString* _headerText;        // Key · BPM · Tuning · notes
    TTRBannerView_v1* _banner;
    TTRDiagnosticsView_v1* _diag;
    TTRLogoView_v1* _logoView;
    TTRAboutView_v1* _about;
    TTRReadout_v1 *_speedReadout, *_keyReadout, *_bpmReadout;  // key and BPM only feed the header
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
    BOOL _showWave;               // waveform lane visible (editor state)
}
- (void)showMessage:(NSString*)message;
- (void)viewportChanged;
@end

// Layout constants shared by construction, layout and drawing.
extern const CGFloat kTTRPanelWidth, kTTRMargin, kTTRPianoWidth, kTTRHeaderHeight, kTTRScrollHeight, kTTRWaveLaneHeight;
extern const double kTTRActiveHz, kTTRIdleHz;

@interface TTRMainView_v1 (Layout)
- (void)layoutControls;
- (void)layoutHeader:(CGFloat)width;
- (void)layoutSidebar:(CGFloat)width height:(CGFloat)height;
- (void)layoutOptions:(CGFloat)width;
- (void)drawHeaderText;
@end

@interface TTRMainView_v1 (Build)
- (void)buildSubviews;
- (void)editParam:(int)paramId value:(float)value;
@end

// Fit zoom (whole recording in the map), options popover and help.
@interface TTRMainView_v1 (Fit)
- (void)applyFitToView;  // sets the zoom of the shared viewport while a fit is on
- (void)toggleOptions;
- (void)closeOptions;
- (void)showHelp;
@end

@interface TTRMainView_v1 (Actions)
- (void)toggleRecord;
- (void)togglePreview;
- (void)stepSpeed:(int)direction;
- (void)toggleWave;
- (void)applyWaveVisibility:(BOOL)visible;
- (void)performReset;
- (void)resetClicked;
- (void)grabClicked;
- (NSArray<NSString*>*)infoLines;
- (NSString*)statusLineForSnapshot:(const ttr::SnapshotPtr&)snap status:(ttr::KernelStatus)status;
@end

// Live bridge: New Sampler Track, heartbeat status.
@interface TTRMainView_v1 (Bridge)
- (void)updateBridgeStatus;
- (void)samplerClicked;
- (void)sendSamplerRequest;
@end

// 4.7: whole-song import (Load Song button, an audio file dropped on the editor).
@interface TTRMainView_v1 (Song)
- (void)registerSongDrop;
- (void)loadSongClicked;
- (void)importSongAtPath:(NSString*)path;
- (NSString*)songStatusLine;
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
- (void)lineViewChanged:(ttr::LineView)view;  // kernel + editor state
- (void)nudgeTuning:(int)direction;
- (NSString*)tuningText;
@end
