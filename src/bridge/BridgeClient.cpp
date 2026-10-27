#include "bridge/BridgeClient.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#include "midi/ExportPlanner.h"

namespace ttr {

namespace {

constexpr size_t kMaxBridgeFileBytes = 1u << 20;

std::string escaped(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += char(c);
        }
    }
    return out;
}

std::string number(double v) {
    char buf[40];
    snprintf(buf, sizeof buf, "%.6g", std::isfinite(v) ? v : 0.0);
    return buf;
}

bool makeDirectory(const std::string& path) {
    if (mkdir(path.c_str(), 0755) == 0) return true;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Creates every missing folder of an absolute path.
bool makeDirectories(const std::string& path) {
    for (size_t pos = 1;;) {
        const size_t end = path.find('/', pos);
        if (!makeDirectory(path.substr(0, end))) return false;
        if (end == std::string::npos) return true;
        pos = end + 1;
    }
}

bool readSmallFile(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    if (text.str().size() > kMaxBridgeFileBytes) return false;
    *out = text.str();
    return true;
}

// Position just behind `"key"` and its colon, or npos.
size_t valueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        size_t i = pos + needle.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
            return i;
        }
        pos = json.find(needle, pos + 1);
    }
    return std::string::npos;
}

}  // namespace

SamplerRequest samplerRequestFor(const Snapshot& snapshot, int snapMode) {
    TimeGrid grid;
    grid.bpm = snapshot.bpm;
    grid.startPpq = snapshot.startPpq;
    grid.snapMode = snapMode;
    const std::vector<MidiNote> planned = planExport(notesForExport(snapshot.notes), grid);
    SamplerRequest request;
    request.name = midiTrackName(snapshot.key);
    request.bpm = snapshot.bpm;
    request.lengthBeats = clipLengthBeats(planned);
    for (const MidiNote& n : planned) request.notes.push_back({n.pitch, n.startBeat, n.lengthBeat, n.velocity});
    return request;
}

std::string samplerRequestJson(const SamplerRequest& r) {
    std::string out = "{\"version\":" + std::to_string(kBridgeProtocolVersion) + ",\"action\":\"create_sampler_track\",\"name\":\"" + escaped(r.name) +
                      "\",\"bpm\":" + number(r.bpm) + ",\"length_beats\":" + number(r.lengthBeats) + ",\"notes\":[";
    for (size_t i = 0; i < r.notes.size(); ++i) {
        const SamplerNote& n = r.notes[i];
        if (i > 0) out += ",";
        out += "{\"pitch\":" + std::to_string(n.pitch) + ",\"start\":" + number(n.startBeats) + ",\"duration\":" + number(n.durationBeats) +
               ",\"velocity\":" + std::to_string(n.velocity) + "}";
    }
    return out + "]}";
}

std::string bridgeDirectory() {
    if (const char* override = std::getenv("TONETRACE_BRIDGE_DIR")) if (*override) return override;
    const char* home = std::getenv("HOME");
    return home && *home ? std::string(home) + "/Library/Application Support/Tonetrace/bridge" : std::string();
}

std::string newRequestId() {
    static std::atomic<unsigned> counter{0};
    static std::random_device device;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    char buf[64];
    snprintf(buf, sizeof buf, "%lld-%04x-%08x", ms, counter.fetch_add(1) & 0xFFFFu, device());
    return buf;
}

bool writeBridgeRequest(const std::string& dir, const std::string& id, const std::string& json, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (dir.empty()) return fail("no bridge folder (HOME is not set)");
    if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos) return fail("invalid request id");
    const std::string inbox = dir + "/inbox";
    if (!makeDirectories(inbox)) return fail("cannot create " + inbox);
    const std::string tmp = inbox + "/." + id + ".tmp", target = inbox + "/" + id + ".json";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot write " + tmp);
        out << json;
        out.flush();
        if (!out) { std::remove(tmp.c_str()); return fail("cannot write " + tmp); }
    }
    if (std::rename(tmp.c_str(), target.c_str()) != 0) {  // the script never sees a half-written file: it ignores dot files
        std::remove(tmp.c_str());
        return fail("cannot move the request into " + inbox);
    }
    return true;
}

bool cancelBridgeRequest(const std::string& dir, const std::string& id) {
    if (dir.empty() || id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos) return false;
    return std::remove((dir + "/inbox/" + id + ".json").c_str()) == 0;
}

bool jsonNumberField(const std::string& json, const std::string& key, double* out) {
    const size_t i = valueStart(json, key);
    if (i == std::string::npos) return false;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool jsonBoolField(const std::string& json, const std::string& key, bool* out) {
    const size_t i = valueStart(json, key);
    if (i == std::string::npos) return false;
    if (json.compare(i, 4, "true") == 0) { *out = true; return true; }
    if (json.compare(i, 5, "false") == 0) { *out = false; return true; }
    return false;
}

bool jsonStringField(const std::string& json, const std::string& key, std::string* out) {
    size_t i = valueStart(json, key);
    if (i == std::string::npos || i >= json.size() || json[i] != '"') return false;
    std::string text;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;
            text += json[i] == 'n' ? '\n' : json[i] == 't' ? '\t' : json[i];
        } else {
            text += json[i];
        }
    }
    if (i >= json.size()) return false;
    *out = text;
    return true;
}

bool heartbeatIsFresh(const std::string& json, double now, double maxAge) {
    double stamp = 0.0;
    if (!jsonNumberField(json, "timestamp", &stamp)) return false;
    const double age = now - stamp;
    return age >= -maxAge && age <= maxAge;
}

bool bridgeConnected(const std::string& dir) {
    std::string text;
    if (dir.empty() || !readSmallFile(dir + "/heartbeat.json", &text)) return false;
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    return heartbeatIsFresh(text, now);
}

BridgeReply readBridgeReply(const std::string& dir, const std::string& id) {
    BridgeReply reply;
    std::string text;
    if (dir.empty() || id.empty() || id.find('/') != std::string::npos || !readSmallFile(dir + "/outbox/" + id + ".json", &text)) return reply;
    if (!jsonBoolField(text, "ok", &reply.isOk)) { reply.isFound = true; reply.error = "unreadable answer of the Live bridge"; return reply; }
    reply.isFound = true;
    jsonStringField(text, "warning", &reply.warning);
    if (!reply.isOk && !jsonStringField(text, "error", &reply.error)) reply.error = "unknown error";
    return reply;
}

}  // namespace ttr
