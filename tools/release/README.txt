Tonetrace 5 (macOS, VST3, Apple Silicon fuer die Transkription)

Installation
  1. In diesem Ordner im Terminal ausfuehren:  ./install.sh
     (kopiert Tonetrace.vst3 nach ~/Library/Audio/Plug-Ins/VST3, signiert ad hoc, entfernt die Quarantaene)
  2. Ableton Live: Einstellungen > Plug-Ins > "VST3-Plug-In-Systemordner verwenden", neu scannen.
     FL Studio: Options > Manage plugins > Find plugins.
  3. Tonetrace auf eine Spur legen, einen Song ins Fenster ziehen (wav, aiff, mp3, m4a, flac, caf; bis 10 min)
     oder Record druecken und die Spur abspielen. Die Noten erscheinen Schritt fuer Schritt, Play geht sofort.
  4. "Drag MIDI out" in die Arrangement-Ansicht ziehen: eine MIDI-Spur pro Instrument, Drums auf Kanal 10.

Beim ersten Song laedt das Plug-in das Transkriptionsmodell (620 MB, einmalig, SHA256-geprueft) nach
~/Library/Tonetrace/models/. Die Modellgewichte (MuScriptor, Kyutai/Mirelo) stehen unter CC BY-NC 4.0: nur
nicht-kommerzielle Nutzung.

Deinstallation:  ./uninstall.sh   (mit --logs auch den Log-Ordner loeschen; ~/Music/Tonetrace bleibt unberuehrt)

Fehlerbehebung
  - Plug-in erscheint nicht: in Live den Plug-in-Scan wiederholen (Alt/Option beim Klick auf "Neu scannen" scannt alles neu).
  - Log: ~/Library/Logs/Tonetrace/Tonetrace.log, Diagnose in der Oberflaeche ueber den Knopf rechts oben.
  - macOS blockiert das Plug-in: xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3

Hinweis zur Weitergabe: Das Bundle ist nur ad hoc signiert und nicht notarisiert. Auf anderen Macs blockiert Gatekeeper es,
bis install.sh (xattr) ausgefuehrt wurde.

Vollstaendige Anleitung: README.md im Quellcode-Repository.
