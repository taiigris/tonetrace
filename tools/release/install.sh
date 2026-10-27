#!/bin/bash
# Installs Tonetrace.vst3 (next to this script) for the current user, signs it ad hoc and clears the quarantine flag,
# and copies the Live bridge (TonetraceBridge/, a MIDI Remote Script) into Live's User Library.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VST3_DIR="${TONETRACE_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
[[ -d "$HERE/Tonetrace.vst3" ]] || { echo "Tonetrace.vst3 not found next to install.sh"; exit 1; }
mkdir -p "$VST3_DIR"
rm -rf "$VST3_DIR/Tonetrace.vst3"
ditto "$HERE/Tonetrace.vst3" "$VST3_DIR/Tonetrace.vst3"
xattr -dr com.apple.quarantine "$VST3_DIR/Tonetrace.vst3" 2>/dev/null || true
codesign --force --deep -s - "$VST3_DIR/Tonetrace.vst3"
echo "Installed to $VST3_DIR/Tonetrace.vst3 - restart your DAW or rescan plug-ins."
SCRIPTS_DIR="${TONETRACE_REMOTE_SCRIPTS_DIR:-$HOME/Music/Ableton/User Library/Remote Scripts}"
if [[ -d "$HERE/TonetraceBridge" ]]; then
  mkdir -p "$SCRIPTS_DIR"
  rm -rf "$SCRIPTS_DIR/TonetraceBridge"
  ditto --norsrc "$HERE/TonetraceBridge" "$SCRIPTS_DIR/TonetraceBridge"
  find "$SCRIPTS_DIR/TonetraceBridge" -name '__pycache__' -prune -exec rm -rf {} +
  echo "Live bridge installed to $SCRIPTS_DIR/TonetraceBridge"
  echo "In Live: Settings > Link, Tempo & MIDI > Control Surface = TonetraceBridge (Input/Output: None), then restart Live."
fi
