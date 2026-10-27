Tonetrace 4.3.0 (macOS, VST3)

Installation
  1. In diesem Ordner im Terminal ausfuehren:  ./install.sh
     (kopiert Tonetrace.vst3 nach ~/Library/Audio/Plug-Ins/VST3, signiert ad hoc, entfernt die Quarantaene
     und installiert die Live-Bridge nach ~/Music/Ableton/User Library/Remote Scripts/TonetraceBridge)
  2. Ableton Live: Einstellungen > Plug-Ins > "VST3-Plug-In-Systemordner verwenden", neu scannen.
     FL Studio: Options > Manage plugins > Find plugins.
  3. Tonetrace auf die Audiospur mit dem Sample ziehen, Record druecken, Sample abspielen, Noten zeichnen,
     "Drag MIDI" in eine MIDI-Spur ziehen.

Live-Bridge ("New Sampler Track"), einmalig:
  Live > Einstellungen > Link, Tempo & MIDI > Control Surface = "TonetraceBridge" (Input/Output: None), Live neu starten.
  Im Plug-in zeigt "Live bridge: connected", dass sie laeuft. Ausfuehrlich: README.md im Quellcode-Repository.

Deinstallation:  ./uninstall.sh   (mit --logs auch den Log-Ordner loeschen; ~/Music/Tonetrace bleibt unberuehrt)

Fehlerbehebung
  - Plug-in erscheint nicht: in Live den Plug-in-Scan wiederholen (Alt/Option beim Klick auf "Neu scannen" scannt alles neu).
  - Log: ~/Library/Logs/Tonetrace/Tonetrace.log, Diagnose in der Oberflaeche ueber den Knopf (i).
  - Live-Log auswerten: tools/check_live_log.sh (im Quellcode-Repository).
  - macOS blockiert das Plug-in: xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3

Hinweis zur Weitergabe: Das Bundle ist nur ad hoc signiert und nicht notarisiert. Auf anderen Macs blockiert Gatekeeper es,
bis install.sh (xattr) ausgefuehrt wurde. Fuer eine Weitergabe ohne diesen Schritt braucht man eine Apple Developer ID
(Signatur + Notarisierung). Lokal reicht ad hoc.

Vollstaendige Anleitung: README.md im Quellcode-Repository.
