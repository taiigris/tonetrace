// Live bridge, plug-in side: the request JSON, the atomic inbox write, the heartbeat and the answers of the script.
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "TestUtil.h"
#include "bridge/BridgeClient.h"
#include "midi/ExportPlanner.h"

using namespace ttr;

namespace {

std::string makeTempDir() {
    const char* tmp = getenv("TMPDIR");
    std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/tonetrace_bridge_test_XXXXXX";
    return mkdtemp(&path[0]) ? path : std::string();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void spit(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

int filesIn(const std::string& dir) {
    int n = 0;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) n += e->d_name[0] != '.' || (e->d_name[1] && e->d_name[1] != '.') ? 1 : 0;
        closedir(d);
    }
    return n;
}

}  // namespace

TEST(bridge_request_json_has_the_agreed_format) {
    SamplerRequest r;
    r.name = "Tonetrace A minor";
    r.bpm = 120.0;
    r.lengthBeats = 16.0;
    r.notes = {{57, 0.0, 1.0, 100}, {60, 1.5, 0.25, 90}};
    const std::string json = samplerRequestJson(r);
    CHECK_MSG(json == "{\"version\":1,\"action\":\"create_sampler_track\",\"name\":\"Tonetrace A minor\",\"bpm\":120,\"length_beats\":16,\"notes\":"
                      "[{\"pitch\":57,\"start\":0,\"duration\":1,\"velocity\":100},{\"pitch\":60,\"start\":1.5,\"duration\":0.25,\"velocity\":90}]}",
              "%s", json.c_str());
    r.name = "a \"quoted\"\nname\\";
    CHECK(samplerRequestJson(r).find("\"name\":\"a \\\"quoted\\\"\\nname\\\\\"") != std::string::npos);
    r.notes.clear();
    CHECK(samplerRequestJson(r).find("\"notes\":[]") != std::string::npos);
}

TEST(bridge_request_comes_from_the_selected_notes_in_beats) {
    Snapshot snap;
    snap.bpm = 120.0;
    snap.key = {9, true, 1.0f};
    NoteEvent a, b;
    a.pitch = 57; a.startSec = 0.5; a.endSec = 1.0;       // beat 1, one beat
    b.pitch = 64; b.startSec = 1.0; b.endSec = 1.5; b.selected = true;
    snap.notes = {a, b};
    SamplerRequest r = samplerRequestFor(snap, 0);
    CHECK(r.notes.size() == 1 && r.notes[0].pitch == 64);  // only the selection
    snap.notes[1].selected = false;
    r = samplerRequestFor(snap, 3);
    CHECK(r.notes.size() == 2 && r.name == "Tonetrace A minor" && r.bpm == 120.0);
    CHECK(r.lengthBeats == 4.0);  // whole bars
    CHECK(r.notes[0].startBeats == 0.0 && r.notes[0].durationBeats == 1.0 && r.notes[1].startBeats == 1.0);  // origin = the beat below the first note
}

TEST(bridge_request_is_written_atomically_into_the_inbox) {
    const std::string dir = makeTempDir();
    CHECK(!dir.empty());
    std::string error;
    const std::string id = newRequestId();
    CHECK(newRequestId() != id);
    CHECK_MSG(writeBridgeRequest(dir + "/deep/bridge", id, "{\"x\":1}", &error), "%s", error.c_str());
    CHECK(slurp(dir + "/deep/bridge/inbox/" + id + ".json") == "{\"x\":1}");
    CHECK(filesIn(dir + "/deep/bridge/inbox") == 1);  // no temp file left behind
    // a hidden temp file is what the script ignores while the write is in flight
    CHECK(writeBridgeRequest(dir + "/deep/bridge", id, "{\"x\":2}", &error) && slurp(dir + "/deep/bridge/inbox/" + id + ".json") == "{\"x\":2}");
    CHECK(cancelBridgeRequest(dir + "/deep/bridge", id) && slurp(dir + "/deep/bridge/inbox/" + id + ".json").empty());  // withdrawn
    CHECK(!cancelBridgeRequest(dir + "/deep/bridge", id) && !cancelBridgeRequest(dir, "../x"));
    CHECK(!writeBridgeRequest(dir, "../evil", "{}", &error) && !error.empty());
    CHECK(!writeBridgeRequest(dir, "", "{}", &error));
    CHECK(!writeBridgeRequest("", "a", "{}", &error));
    spit(dir + "/blocked", "a file");
    CHECK(!writeBridgeRequest(dir + "/blocked", "a", "{}", &error) && !error.empty());  // the folder cannot be created
    std::system(("rm -rf '" + dir + "'").c_str());
}

TEST(bridge_heartbeat_is_fresh_for_three_seconds) {
    const std::string beat = "{\"timestamp\": 1000.5, \"live_version\": \"11.3.1\"}";  // json.dumps spacing
    CHECK(heartbeatIsFresh(beat, 1000.5));
    CHECK(heartbeatIsFresh(beat, 1003.0));
    CHECK(!heartbeatIsFresh(beat, 1003.6));
    CHECK(!heartbeatIsFresh(beat, 5000.0));
    CHECK(!heartbeatIsFresh(beat, 100.0));  // a clock far in the past is not "fresh" either
    CHECK(heartbeatIsFresh("{\"timestamp\":1000}", 1001.0));
    CHECK(!heartbeatIsFresh("", 1000.0));
    CHECK(!heartbeatIsFresh("{\"timestamp\": \"soon\"}", 1000.0));
    CHECK(!heartbeatIsFresh("not json at all", 1000.0));
    CHECK(!heartbeatIsFresh("{\"timestamp\": nan}", 1000.0));
    const std::string dir = makeTempDir();
    CHECK(!bridgeConnected(dir));  // no heartbeat file
    spit(dir + "/heartbeat.json", "{\"timestamp\": 1.0}");
    CHECK(!bridgeConnected(dir));  // 1970
    std::system(("rm -rf '" + dir + "'").c_str());
}

TEST(bridge_replies_are_read_from_the_outbox) {
    const std::string dir = makeTempDir();
    mkdir((dir + "/outbox").c_str(), 0755);
    CHECK(!readBridgeReply(dir, "none").isFound);
    spit(dir + "/outbox/ok.json", "{\"ok\": true, \"notes\": 2}");
    spit(dir + "/outbox/warn.json", "{\"ok\": true, \"warning\": \"Simpler was not loaded: x\"}");
    spit(dir + "/outbox/bad.json", "{\"ok\": false, \"error\": \"cannot create the MIDI track: no more tracks\"}");
    spit(dir + "/outbox/junk.json", "{{{");
    const BridgeReply ok = readBridgeReply(dir, "ok"), warn = readBridgeReply(dir, "warn"), bad = readBridgeReply(dir, "bad"), junk = readBridgeReply(dir, "junk");
    CHECK(ok.isFound && ok.isOk && ok.error.empty());
    CHECK(warn.isFound && warn.isOk && warn.warning == "Simpler was not loaded: x");
    CHECK(bad.isFound && !bad.isOk && bad.error == "cannot create the MIDI track: no more tracks");
    CHECK(junk.isFound && !junk.isOk && !junk.error.empty());
    CHECK(!readBridgeReply(dir, "../ok").isFound);
    std::system(("rm -rf '" + dir + "'").c_str());
}

TEST(bridge_directory_honours_the_override) {
    setenv("TONETRACE_BRIDGE_DIR", "/tmp/ttr_override", 1);
    CHECK(bridgeDirectory() == "/tmp/ttr_override");
    unsetenv("TONETRACE_BRIDGE_DIR");
    CHECK(bridgeDirectory().find("Library/Application Support/Tonetrace/bridge") != std::string::npos);
}
