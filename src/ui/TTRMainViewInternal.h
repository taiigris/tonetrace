#pragma once
#import "ui/TTRAbout.h"
#import "ui/TTRControls.h"
#import "ui/TTRInstrumentPicker.h"
#import "ui/TTRInstrumentStrip.h"
#import "ui/TTRLogo.h"
#import "ui/TTRMainView.h"
#import "ui/TTROverlays.h"
#import "ui/TTRPianoRoll.h"
#import "ui/TTRWaveLane.h"
#import "ui/TTRWidgets.h"

#include <map>
#include <memory>

@interface TTRMainView_v1 () {
@public
    std::shared_ptr<ttr::TTRHostBridge> _bridge;
    std::shared_ptr<ttr::TonetraceKernel> _kernel;
    std::shared_ptr<TTRTimeline> _tl;
    NSTimer* _timer;
    // top bar
    TTRLogoView_v1* _logo;
    TTRGlyphButton_v1 *_rewindButton, *_playButton, *_followButton, *_recordButton, *_muteButton, *_optionsButton;
    TTRPill_v1* _modelPill;      // opens the model-size menu
    TTRLineSlider_v1 *_mixSlider, *_volumeSlider;
    NSRect _timeRect;
    // sidebar
    TTRPill_v1* _instrumentPill;  // opens the instrument picker
    NSView* _strips;  // container of the instrument strips (scrolls with the wheel)
    std::map<int, TTRInstrumentStrip_v1*> _stripByProgram;
    CGFloat _stripScroll;
    TTRMeter_v1* _masterMeter;
    // song header + main area
    TTRStepper_v1* _tempoStepper;
    TTRGlyphButton_v1 *_openButton, *_exportButton, *_clearButton;
    TTRGlyphDragButton_v1* _dragButton;
    TTRWaveLane_v1* _wave;
    TTRPianoRoll_v1* _roll;
    TTRScrollBar_v1* _scroll;
    TTRLineSlider_v1* _zoomSlider;
    // overlays
    TTRBannerView_v1* _banner;
    TTRDiagnosticsView_v1* _diag;
    TTRInstrumentPicker_v1* _picker;
    TTRAboutView_v1* _about;
    // state of the last refresh
    ttr::DocumentPtr _doc;
    uint64_t _shownDocId;
    BOOL _isDropTarget;
    NSString* _transientMessage;
    CFTimeInterval _transientUntil;
    BOOL _isWindowVisible;
    double _tickHz;
    unsigned long _lastDrawCount;
    CFTimeInterval _lastFpsTime;
    double _fps;
    ttr::UiSettings _storedUi;
    NSSize _storedSize;
}
- (void)showMessage:(NSString*)message;
@end

// Layout constants shared by construction, layout and drawing.
extern const CGFloat kTTRTopBarHeight, kTTRSidebarWidth, kTTRSongHeaderHeight, kTTRWaveHeight, kTTRStatusHeight, kTTRScrollHeight, kTTRMasterHeight;
extern const double kTTRActiveHz, kTTRIdleHz;

@interface TTRMainView_v1 (Build)
- (void)buildSubviews;
@end

@interface TTRMainView_v1 (Layout)
- (void)layoutControls;
- (NSRect)noteAreaRect;    // waveform + piano roll (the drop zone)
- (NSRect)songHeaderRect;
- (NSRect)statusRect;
@end

@interface TTRMainView_v1 (Actions)
- (void)openSongPanel;
- (void)importSongAtPath:(NSString*)path;
- (void)toggleRecord;
- (void)togglePlay;
- (void)rewind;
- (void)clearSong;
- (NSURL*)writeMidiToMusicFolder;
- (void)exportMidiWithPanel;
- (void)seekTo:(double)sec;
- (void)handleWheel:(NSEvent*)event noteAreaX:(CGFloat)x;
- (void)handleMagnify:(NSEvent*)event noteAreaX:(CGFloat)x;
- (void)zoomTo:(double)pxPerSec aroundX:(CGFloat)x;
- (void)rebuildStripsIfNeeded;
- (void)updateStrips;
- (void)setParam:(int)paramId value:(float)value;
@end

// The model choices: size (menu on the model pill) and the instruments it listens for (picker from the sidebar).
@interface TTRMainView_v1 (Choices)
- (void)showModelMenu;
- (void)chooseModelSize:(int)size;  // ttr::ModelSize; re-analyses a loaded song
- (void)showInstrumentPicker;
- (void)applyInstrumentSelection:(const std::vector<int>&)programs focused:(BOOL)isFocused;  // re-analyses when needed
@end

@interface TTRMainView_v1 (System)
- (void)showBanner:(NSString*)text level:(ttr::NoticeLevel)level;
- (void)pollNotices;
- (void)updateTimer;
- (void)toggleDiagnostics;
- (void)showAbout;
- (void)updateDiagnostics;
- (void)restoreUiSettings;
- (void)storeUiSettingsIfChanged;
@end
