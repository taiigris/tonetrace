#!/bin/bash
# Removes Tonetrace from this Mac.
#   ./uninstall.sh          removes ~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3
#   ./uninstall.sh --logs   additionally removes the log folder ~/Library/Logs/Tonetrace
# The MIDI files in ~/Music/Tonetrace are yours and are never touched.
# TONETRACE_VST3_DIR / TONETRACE_LOG_DIR override the folders (used by the tests).
set -euo pipefail

VST3_DIR="${TONETRACE_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
LOG_DIR="${TONETRACE_LOG_DIR:-$HOME/Library/Logs/Tonetrace}"
BUNDLE="$VST3_DIR/Tonetrace.vst3"

if [[ -d "$BUNDLE" ]]; then
  rm -rf "$BUNDLE"
  echo "Removed $BUNDLE"
else
  echo "Not installed: $BUNDLE"
fi
if [[ "${1:-}" == "--logs" ]]; then
  if [[ -d "$LOG_DIR" ]]; then rm -rf "$LOG_DIR"; echo "Removed $LOG_DIR"; else echo "No log folder: $LOG_DIR"; fi
fi
echo "Restart your DAW or rescan plug-ins. Your MIDI files in ~/Music/Tonetrace were left alone."
