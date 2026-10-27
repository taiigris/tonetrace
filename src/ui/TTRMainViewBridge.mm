// "New Sampler Track": asks the Live bridge (a MIDI Remote Script) to create a track with Simpler and a clip with the notes.
// The request is a file in ~/Library/Application Support/Tonetrace/bridge; the file I/O runs on a background queue, never on the audio thread.
#import <dispatch/dispatch.h>

#import "bridge/BridgeClient.h"
#import "core/Logger.h"
#import "ui/TTRMainViewInternal.h"

static const double kBridgePollSeconds = 1.0, kReplyPollSeconds = 0.25;
static const int kReplyMaxPolls = 40;  // 10 s

@implementation TTRMainView_v1 (Bridge)

- (void)updateBridgeStatus {
    const CFTimeInterval now = CACurrentMediaTime();
    if (now - _bridgeChecked >= kBridgePollSeconds) {
        _bridgeChecked = now;
        _bridgeUp = ttr::bridgeConnected(ttr::bridgeDirectory());
    }
    _bridgeReadout.value = _bridgeUp ? @"connected" : @"not installed";
    _bridgeReadout.isHighlighted = _bridgeUp;
}

- (void)samplerClicked {
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    if (!snap || snap->notes.empty()) { [self showMessage:@"Draw at least one note first"]; return; }
    _bridgeUp = ttr::bridgeConnected(ttr::bridgeDirectory());
    _bridgeChecked = CACurrentMediaTime();
    if (!_bridgeUp) {
        [self showBanner:@"Live bridge not active: in Live open Settings > Link, Tempo & MIDI, choose the Control Surface TonetraceBridge (Input and Output: None), then restart Live. Details are in the README."
                   level:ttr::NoticeLevel::Warning];
        return;
    }
    [self sendSamplerRequest];
}

- (void)sendSamplerRequest {
    if (_samplerPending) return;
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    if (!snap || snap->notes.empty()) return;
    const std::string json = ttr::samplerRequestJson(ttr::samplerRequestFor(*snap, int(_kernel->param(ttr::kParamSnap))));
    const std::string dir = ttr::bridgeDirectory(), requestId = ttr::newRequestId();
    const int instance = _kernel->instanceId();
    _samplerPending = YES;
    __weak TTRMainView_v1* weak = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string error;
        const bool isWritten = ttr::writeBridgeRequest(dir, requestId, json, &error);
        dispatch_async(dispatch_get_main_queue(), ^{
            TTRMainView_v1* self = weak;
            if (!self) return;
            if (!isWritten) {
                ttr::logError(instance, "Live bridge: %s", error.c_str());
                [self finishSampler:[NSString stringWithFormat:@"The request for Live could not be written: %s", error.c_str()] isError:YES];
                return;
            }
            ttr::logInfo(instance, "Live bridge: request %s sent", requestId.c_str());
            [self awaitReply:requestId directory:dir polls:0];
        });
    });
    [self showMessage:@"Asking Live for a sampler track…"];
}

// The script answers within a fraction of a second while Live is idle; give it a few seconds.
- (void)awaitReply:(const std::string&)requestId directory:(const std::string&)dir polls:(int)polls {
    const ttr::BridgeReply reply = ttr::readBridgeReply(dir, requestId);
    if (reply.isFound) {
        if (reply.isOk) {
            NSString* note = reply.warning.empty() ? @"" : [NSString stringWithFormat:@" (%s)", reply.warning.c_str()];
            [self finishSampler:[NSString stringWithFormat:@"Sampler track created in Live%@", note] isError:NO];
        } else {
            [self finishSampler:[NSString stringWithFormat:@"Live could not create the track: %s", reply.error.c_str()] isError:YES];
        }
        return;
    }
    if (polls >= kReplyMaxPolls) {
        ttr::cancelBridgeRequest(dir, requestId);  // so Live does not create the track long after the user has given up
        [self finishSampler:@"Live did not answer - is the TonetraceBridge control surface still selected and Live running?" isError:YES];
        return;
    }
    __weak TTRMainView_v1* weak = self;
    const std::string reqId = requestId, directory = dir;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(kReplyPollSeconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [weak awaitReply:reqId directory:directory polls:polls + 1]; });
}

- (void)finishSampler:(NSString*)text isError:(BOOL)isError {
    _samplerPending = NO;
    ttr::logInfo(_kernel->instanceId(), "Live bridge: %s", text.UTF8String);
    if (isError) [self showBanner:text level:ttr::NoticeLevel::Error];
    else [self showMessage:text];
    [self refreshNow];
}

@end
