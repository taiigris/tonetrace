"""Tests of the Tonetrace Live bridge against a mock of the Live API (standard library only).
Run with:  python3 -I tests/bridge/test_bridge.py   or   python3 -I -m unittest discover -s tests/bridge"""
import json
import os
import shutil
import sys
import tempfile
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "live-bridge"))


# ---- mock of the parts of the Live API the script uses ----
class Note(object):
    def __init__(self, pitch, start_time, duration, velocity, mute):
        self.pitch, self.start_time, self.duration, self.velocity, self.mute = pitch, start_time, duration, velocity, mute


class Clip(object):
    def __init__(self, length):
        self.length, self.name, self.added, self.legacy = length, "", [], []

    def add_new_notes(self, specs):
        self.added.extend(specs)

    def set_notes(self, notes):
        self.legacy.extend(notes)


class ClipSlot(object):
    def __init__(self):
        self.clip = None

    def create_clip(self, length):
        self.clip = Clip(length)


class Track(object):
    def __init__(self):
        self.name, self.clip_slots, self.devices = "", [ClipSlot() for _ in range(4)], []


class Item(object):
    def __init__(self, name, loadable=False, children=()):
        self.name, self.is_loadable, self.children = name, loadable, list(children)


class Browser(object):
    def __init__(self, instruments):
        self.instruments, self.loaded, self.target = instruments, [], None

    def load_item(self, item):
        self.loaded.append(item.name)
        self.target.devices.append(item.name)


class View(object):
    def __init__(self, song):
        self._song, self._selected = song, None

    @property
    def selected_track(self):
        return self._selected

    @selected_track.setter
    def selected_track(self, track):
        self._selected = track
        self._song.browser.target = track


class Song(object):
    def __init__(self, browser):
        self.tracks, self.browser, self.view = [], browser, View(self)

    def create_midi_track(self, index):
        self.tracks.append(Track())


class FakeApp(object):
    def __init__(self, browser):
        self.browser = browser

    def get_major_version(self): return 11
    def get_minor_version(self): return 3
    def get_bugfix_version(self): return 1


class FakeInstance(object):
    def __init__(self, song):
        self._song = song

    def song(self):
        return self._song


class FakeControlSurface(object):
    def __init__(self, c_instance):
        self._c = c_instance
        self.log, self.messages = [], []

    def song(self): return self._c.song()
    def log_message(self, text): self.log.append(text)
    def show_message(self, text): self.messages.append(text)
    def update_display(self): pass
    def disconnect(self): pass


def install_mock(with_simpler=True, new_note_api=True):
    simpler = [Item("Instrument Rack", False, [Item("Simpler", True)])] if with_simpler else [Item("Operator", True)]
    browser = Browser(Item("instruments", False, simpler))
    song = Song(browser)
    app = FakeApp(browser)
    live = types.ModuleType("Live")
    live.Application = types.ModuleType("Live.Application")
    live.Application.get_application = lambda: app
    live.Clip = types.ModuleType("Live.Clip")
    if new_note_api:
        live.Clip.MidiNoteSpecification = Note
    framework = types.ModuleType("_Framework")
    surface = types.ModuleType("_Framework.ControlSurface")
    surface.ControlSurface = FakeControlSurface
    sys.modules.update({"Live": live, "Live.Application": live.Application, "Live.Clip": live.Clip,
                        "_Framework": framework, "_Framework.ControlSurface": surface})
    for name in [m for m in sys.modules if m.startswith("TonetraceBridge")]:
        del sys.modules[name]
    import TonetraceBridge
    return TonetraceBridge, song


class Clock(object):
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


REQUEST = {"version": 1, "action": "create_sampler_track", "name": "Tonetrace A minor", "bpm": 120.0, "length_beats": 16.0,
           "notes": [{"pitch": 57, "start": 0.0, "duration": 1.0, "velocity": 100}, {"pitch": 60, "start": 1.5, "duration": 0.5, "velocity": 90}]}


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ttr_bridge_")
        self.clock = Clock()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def start(self, **mock):
        package, self.song = install_mock(**mock)
        self.bridge = package.bridge.TonetraceBridge(FakeInstance(self.song), bridge_dir=self.dir, clock=self.clock)
        return package

    def send(self, request_id, payload):
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
        with open(os.path.join(self.dir, "inbox", request_id + ".json"), "wb") as handle:
            handle.write(data)

    def tick(self, seconds=0.3):
        self.clock.now += seconds
        self.bridge.update_display()

    def answer(self, request_id):
        with open(os.path.join(self.dir, "outbox", request_id + ".json")) as handle:
            return json.load(handle)

    def test_create_instance_returns_the_control_surface(self):
        package, song = install_mock()
        instance = package.create_instance(FakeInstance(song))
        self.assertEqual(type(instance).__name__, "TonetraceBridge")

    def test_request_creates_track_simpler_clip_and_notes(self):
        self.start()
        self.send("a", REQUEST)
        self.tick()
        self.assertEqual(len(self.song.tracks), 1)
        track = self.song.tracks[0]
        self.assertEqual(track.name, "Tonetrace A minor")
        self.assertIs(self.song.view.selected_track, track)
        self.assertEqual(track.devices, ["Simpler"])
        clip = track.clip_slots[0].clip
        self.assertEqual(clip.length, 16.0)
        self.assertEqual(clip.name, "Tonetrace A minor")
        self.assertEqual([(n.pitch, n.start_time, n.duration, n.velocity, n.mute) for n in clip.added], [(57, 0.0, 1.0, 100, False), (60, 1.5, 0.5, 90, False)])
        self.assertTrue(self.answer("a")["ok"])
        self.assertFalse(os.path.exists(os.path.join(self.dir, "inbox", "a.json")))

    def test_a_request_is_processed_exactly_once(self):
        self.start()
        self.send("a", REQUEST)
        self.tick()
        self.send("a", REQUEST)  # same id again, e.g. a leftover file
        self.tick()
        self.tick()
        self.assertEqual(len(self.song.tracks), 1)

    def test_two_requests_make_two_tracks(self):
        self.start()
        self.send("a", REQUEST)
        self.send("b", REQUEST)
        self.tick()
        self.assertEqual(len(self.song.tracks), 2)
        self.assertTrue(self.answer("a")["ok"] and self.answer("b")["ok"])

    def test_broken_json_is_answered_with_an_error_and_does_not_crash(self):
        self.start()
        self.send("bad", b"{not json")
        self.tick()
        reply = self.answer("bad")
        self.assertFalse(reply["ok"])
        self.assertIn("JSON", reply["error"])
        self.assertEqual(self.song.tracks, [])
        self.send("good", REQUEST)  # the script keeps working
        self.tick()
        self.assertTrue(self.answer("good")["ok"])

    def test_invalid_requests_are_rejected(self):
        self.start()
        bad = [dict(REQUEST, version=2), dict(REQUEST, action="delete_everything"), dict(REQUEST, notes="x"),
               dict(REQUEST, notes=[{"pitch": 200, "start": 0, "duration": 1}]), dict(REQUEST, notes=[{"pitch": 60}]),
               dict(REQUEST, length_beats=-1), [1, 2, 3], b"\xff\xfe"]
        for i, payload in enumerate(bad):
            self.send("r%d" % i, payload)
        self.tick()
        for i in range(len(bad)):
            self.assertFalse(self.answer("r%d" % i)["ok"], bad[i])
        self.assertEqual(self.song.tracks, [])

    def test_missing_simpler_still_creates_the_clip_with_a_warning(self):
        self.start(with_simpler=False)
        self.send("a", REQUEST)
        self.tick()
        reply = self.answer("a")
        self.assertTrue(reply["ok"])
        self.assertIn("Simpler", reply["warning"])
        self.assertEqual(len(self.song.tracks[0].clip_slots[0].clip.added), 2)

    def test_old_note_api_is_the_fallback(self):
        self.start(new_note_api=False)
        self.send("a", REQUEST)
        self.tick()
        clip = self.song.tracks[0].clip_slots[0].clip
        self.assertEqual(clip.legacy, [(57, 0.0, 1.0, 100, False), (60, 1.5, 0.5, 90, False)])
        self.assertTrue(self.answer("a")["ok"])

    def test_heartbeat_is_written_every_second(self):
        self.start()
        with open(os.path.join(self.dir, "heartbeat.json")) as handle:
            first = json.load(handle)
        self.assertEqual(first["live_version"], "11.3.1")
        self.tick(0.5)
        with open(os.path.join(self.dir, "heartbeat.json")) as handle:
            self.assertEqual(json.load(handle)["timestamp"], first["timestamp"])  # not yet
        self.tick(0.6)
        with open(os.path.join(self.dir, "heartbeat.json")) as handle:
            self.assertGreater(json.load(handle)["timestamp"], first["timestamp"])

    def test_an_old_request_is_not_executed(self):
        self.start()
        self.send("old", REQUEST)
        path = os.path.join(self.dir, "inbox", "old.json")
        stale = os.path.getmtime(path) - 3600
        os.utime(path, (stale, stale))
        self.tick()
        self.assertEqual(self.song.tracks, [])
        reply = self.answer("old")
        self.assertFalse(reply["ok"])
        self.assertIn("expired", reply["error"])

    def test_dot_files_and_foreign_files_are_ignored(self):
        self.start()
        self.send(".half", REQUEST)
        with open(os.path.join(self.dir, "inbox", "note.txt"), "w") as handle:
            handle.write("hello")
        self.tick()
        self.assertEqual(self.song.tracks, [])

    def test_a_failing_live_call_is_reported_not_raised(self):
        self.start()
        self.song.create_midi_track = lambda index: (_ for _ in ()).throw(RuntimeError("no more tracks"))
        self.send("a", REQUEST)
        self.tick()
        reply = self.answer("a")
        self.assertFalse(reply["ok"])
        self.assertIn("no more tracks", reply["error"])


if __name__ == "__main__":
    unittest.main()
