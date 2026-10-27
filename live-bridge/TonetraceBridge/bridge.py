# Control surface that turns Tonetrace requests (JSON files) into a MIDI track with a Simpler and a clip with the notes.
# Only the Python standard library, and syntax that Python 2.7 (Live 10) and Python 3 (Live 11 and newer) both accept.
import json
import os
import time

import Live
from _Framework.ControlSurface import ControlSurface

PROTOCOL_VERSION = 1
POLL_SECONDS = 0.25
HEARTBEAT_SECONDS = 1.0
OUTBOX_KEEP_SECONDS = 600.0
REQUEST_MAX_AGE_SECONDS = 120.0  # an older request was left behind while Live was closed or busy: the user gave up on it long ago
MAX_REQUEST_BYTES = 1024 * 1024
MAX_NOTES = 20000
MAX_REMEMBERED_IDS = 5000
MAX_LENGTH_BEATS = 4096.0
DEVICE_NAME = "Simpler"


def default_bridge_dir():
    override = os.environ.get("TONETRACE_BRIDGE_DIR")
    if override:
        return override
    return os.path.expanduser("~/Library/Application Support/Tonetrace/bridge")


def write_json_atomic(path, obj):
    """Writes obj as JSON to path through a dot-file and a rename, so a reader never sees half a file."""
    tmp = os.path.join(os.path.dirname(path), "." + os.path.basename(path) + ".tmp")
    data = json.dumps(obj).encode("ascii")
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.rename(tmp, path)


class RequestError(Exception):
    pass


def parse_request(raw):
    """Validates a request (bytes) and returns it as a dict; raises RequestError with a readable reason."""
    if len(raw) > MAX_REQUEST_BYTES:
        raise RequestError("request is too large")
    try:
        request = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as error:
        raise RequestError("invalid JSON: %s" % error)
    if not isinstance(request, dict):
        raise RequestError("the request must be a JSON object")
    if request.get("version") != PROTOCOL_VERSION:
        raise RequestError("unsupported protocol version %r" % (request.get("version"),))
    if request.get("action") != "create_sampler_track":
        raise RequestError("unknown action %r" % (request.get("action"),))
    notes = request.get("notes")
    if not isinstance(notes, list) or len(notes) > MAX_NOTES:
        raise RequestError("notes must be a list of at most %d entries" % MAX_NOTES)
    checked = []
    for note in notes:
        try:
            pitch, start = int(note["pitch"]), float(note["start"])
            duration, velocity = float(note["duration"]), int(note.get("velocity", 100))
        except (KeyError, TypeError, ValueError, AttributeError):
            raise RequestError("a note needs pitch, start and duration")
        if not (0 <= pitch <= 127 and start >= 0.0 and duration > 0.0 and 1 <= velocity <= 127):
            raise RequestError("a note is out of range")
        checked.append((pitch, start, duration, velocity))
    try:
        length = float(request.get("length_beats", 4.0))
    except (TypeError, ValueError):
        raise RequestError("length_beats must be a number")
    if not (0.0 < length <= MAX_LENGTH_BEATS):
        raise RequestError("length_beats is out of range")
    name = request.get("name")
    return {"name": name if isinstance(name, (type(u""), str)) and name else "Tonetrace", "length": length, "notes": checked}


def find_device(item, name, depth=0):
    """Depth-first search of a browser tree for the loadable item with exactly this name."""
    if depth > 8:
        return None
    if getattr(item, "name", None) == name and getattr(item, "is_loadable", False):
        return item
    for child in getattr(item, "children", []) or []:
        found = find_device(child, name, depth + 1)
        if found is not None:
            return found
    return None


class TonetraceBridge(ControlSurface):
    def __init__(self, c_instance, bridge_dir=None, clock=None):
        ControlSurface.__init__(self, c_instance)
        self._clock = clock or time.time
        self._dir = bridge_dir or default_bridge_dir()
        self._inbox = os.path.join(self._dir, "inbox")
        self._outbox = os.path.join(self._dir, "outbox")
        self._seen = set()
        self._last_poll = 0.0
        self._last_beat = 0.0
        for folder in (self._inbox, self._outbox):
            if not os.path.isdir(folder):
                os.makedirs(folder)
        self.log_message("TonetraceBridge: started, folder %s" % self._dir)
        self.show_message("Tonetrace Bridge is active")
        self._heartbeat()

    def disconnect(self):
        self.log_message("TonetraceBridge: stopped")
        ControlSurface.disconnect(self)

    # Called by Live about ten times a second.
    def update_display(self):
        ControlSurface.update_display(self)
        now = self._clock()
        try:
            if now - self._last_beat >= HEARTBEAT_SECONDS:
                self._heartbeat()
                self._clean_outbox()
            if now - self._last_poll >= POLL_SECONDS:
                self._last_poll = now
                self._poll()
        except Exception as error:  # nothing may escape into Live
            self.log_message("TonetraceBridge: error in update_display: %r" % (error,))

    def _heartbeat(self):
        self._last_beat = self._clock()
        try:
            app = Live.Application.get_application()
            version = "%d.%d.%d" % (app.get_major_version(), app.get_minor_version(), app.get_bugfix_version())
        except Exception:
            version = "unknown"
        try:
            write_json_atomic(os.path.join(self._dir, "heartbeat.json"), {"timestamp": self._clock(), "live_version": version})
        except (IOError, OSError) as error:
            self.log_message("TonetraceBridge: cannot write the heartbeat: %r" % (error,))

    def _poll(self):
        try:
            names = sorted(os.listdir(self._inbox))
        except OSError as error:
            self.log_message("TonetraceBridge: cannot read the inbox: %r" % (error,))
            return
        for name in names:
            if name.startswith(".") or not name.endswith(".json"):
                continue
            request_id = name[:-5]
            if request_id in self._seen:
                continue
            self._seen.add(request_id)  # exactly once, even when deleting the file fails
            self._handle(request_id, os.path.join(self._inbox, name))
        if len(self._seen) > MAX_REMEMBERED_IDS:
            self._seen.clear()  # the files are gone by now

    def _handle(self, request_id, path):
        try:
            if time.time() - os.path.getmtime(path) > REQUEST_MAX_AGE_SECONDS:
                self.log_message("TonetraceBridge: request %s expired" % request_id)
                reply = {"ok": False, "error": "the request expired (older than %d s)" % int(REQUEST_MAX_AGE_SECONDS)}
            else:
                with open(path, "rb") as handle:
                    raw = handle.read(MAX_REQUEST_BYTES + 1)
                reply = self._process(raw)
        except Exception as error:
            self.log_message("TonetraceBridge: request %s failed: %r" % (request_id, error))
            reply = {"ok": False, "error": "%s" % (error,)}
        try:
            write_json_atomic(os.path.join(self._outbox, request_id + ".json"), reply)
        except (IOError, OSError) as error:
            self.log_message("TonetraceBridge: cannot write the answer %s: %r" % (request_id, error))
        try:
            os.remove(path)
        except OSError as error:
            self.log_message("TonetraceBridge: cannot remove the request %s: %r" % (request_id, error))

    def _process(self, raw):
        try:
            request = parse_request(raw)
        except RequestError as error:
            self.log_message("TonetraceBridge: rejected request: %s" % (error,))
            return {"ok": False, "error": "%s" % (error,)}
        return self._create_sampler_track(request)

    def _create_sampler_track(self, request):
        song = self.song()
        warnings = []
        try:
            song.create_midi_track(-1)
            track = song.tracks[len(song.tracks) - 1]
            track.name = request["name"]
            self.log_message("TonetraceBridge: created track %r" % (request["name"],))
        except Exception as error:
            self.log_message("TonetraceBridge: cannot create the track: %r" % (error,))
            return {"ok": False, "error": "cannot create the MIDI track: %s" % (error,)}
        try:
            song.view.selected_track = track
            browser = Live.Application.get_application().browser
            device = find_device(browser.instruments, DEVICE_NAME)
            if device is None:
                raise RuntimeError("%s was not found in the browser" % DEVICE_NAME)
            browser.load_item(device)
            self.log_message("TonetraceBridge: loaded %s" % DEVICE_NAME)
        except Exception as error:
            self.log_message("TonetraceBridge: cannot load %s: %r" % (DEVICE_NAME, error))
            warnings.append("%s was not loaded: %s" % (DEVICE_NAME, error))
        try:
            clip = self._make_clip(track, request)
            self.log_message("TonetraceBridge: clip with %d notes" % len(request["notes"]))
        except Exception as error:
            self.log_message("TonetraceBridge: cannot create the clip: %r" % (error,))
            return {"ok": False, "error": "the track was created, but not the clip: %s" % (error,)}
        reply = {"ok": True, "notes": len(request["notes"]), "clip": clip.name}
        if warnings:
            reply["warning"] = "; ".join(warnings)
        return reply

    def _make_clip(self, track, request):
        slot = track.clip_slots[0]
        slot.create_clip(request["length"])
        clip = slot.clip
        clip.name = request["name"]
        notes = request["notes"]
        if notes:
            try:
                specs = tuple(Live.Clip.MidiNoteSpecification(pitch=p, start_time=s, duration=d, velocity=v, mute=False) for (p, s, d, v) in notes)
                clip.add_new_notes(specs)
            except (AttributeError, TypeError) as error:  # Live 10 and older: the old note API
                self.log_message("TonetraceBridge: add_new_notes unavailable (%r), using set_notes" % (error,))
                clip.set_notes(tuple((p, s, d, v, False) for (p, s, d, v) in notes))
        return clip

    def _clean_outbox(self):
        cutoff = time.time() - OUTBOX_KEEP_SECONDS  # file times are real time, whatever the injected clock says
        try:
            for name in os.listdir(self._outbox):
                path = os.path.join(self._outbox, name)
                if os.path.getmtime(path) < cutoff:
                    os.remove(path)
        except OSError:
            pass
