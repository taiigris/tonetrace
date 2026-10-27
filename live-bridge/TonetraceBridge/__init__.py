# Tonetrace Bridge: Ableton Live MIDI Remote Script (works in every Live edition, also Live Intro).
# Select "TonetraceBridge" as a Control Surface in Live's settings (Link, Tempo & MIDI), Input and Output: None.
from .bridge import TonetraceBridge


def create_instance(c_instance):
    return TonetraceBridge(c_instance)
