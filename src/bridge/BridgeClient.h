// Client side of the Live bridge: a MIDI Remote Script ("TonetraceBridge") creates the sampler track, the plug-in only
// exchanges small JSON files with it in ~/Library/Application Support/Tonetrace/bridge. Message thread / worker only (file I/O).
//   inbox/<id>.json    request written by the plug-in (atomically: temp file, then rename)
//   outbox/<id>.json   answer of the script: {"ok":true} or {"ok":false,"error":"..."}
//   heartbeat.json     written by the script every second: {"timestamp":<epoch seconds>,"live_version":"..."}
#pragma once
#include <string>
#include <vector>

#include "core/Types.h"

namespace ttr {

constexpr int kBridgeProtocolVersion = 1;
constexpr double kHeartbeatMaxAgeSec = 3.0;  // older than this: Live is closed or the script is not active

struct SamplerNote {
    int pitch = 60;
    double startBeats = 0.0, durationBeats = 1.0;
    int velocity = 100;
};

struct SamplerRequest {
    std::string name;
    double bpm = kDefaultBpm;
    double lengthBeats = 4.0;
    std::vector<SamplerNote> notes;
};

// The selected notes (or all) of the snapshot as a request: beats from the recording tempo, quantised like the MIDI export.
SamplerRequest samplerRequestFor(const Snapshot& snapshot, int snapMode);
// {"version":1,"action":"create_sampler_track","name":...,"bpm":...,"length_beats":...,"notes":[{"pitch":..,"start":..,"duration":..,"velocity":..}]}
std::string samplerRequestJson(const SamplerRequest& request);

// Bridge folder (TONETRACE_BRIDGE_DIR overrides it for tests); empty when HOME is unknown.
std::string bridgeDirectory();
std::string newRequestId();  // unique file name stem (time + counter + random)
// Writes <dir>/inbox/<id>.json atomically; false with `error` when the folder or the file cannot be written.
bool writeBridgeRequest(const std::string& dir, const std::string& id, const std::string& json, std::string* error);

// Withdraws a request nobody has picked up (the user gave up waiting); true when it was still in the inbox.
bool cancelBridgeRequest(const std::string& dir, const std::string& id);

// True when the heartbeat text holds a timestamp that is at most `maxAgeSec` older than `nowEpochSec` (and not in the far future).
bool heartbeatIsFresh(const std::string& heartbeatJson, double nowEpochSec, double maxAgeSec = kHeartbeatMaxAgeSec);
bool bridgeConnected(const std::string& dir);  // reads <dir>/heartbeat.json against the clock

struct BridgeReply {
    bool isFound = false;  // the outbox holds an answer for the id
    bool isOk = false;
    std::string error, warning;
};
BridgeReply readBridgeReply(const std::string& dir, const std::string& id);

// Minimal JSON field readers for the files above (flat objects written by the script).
bool jsonNumberField(const std::string& json, const std::string& key, double* out);
bool jsonBoolField(const std::string& json, const std::string& key, bool* out);
bool jsonStringField(const std::string& json, const std::string& key, std::string* out);

}  // namespace ttr
