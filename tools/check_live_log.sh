#!/bin/bash
# Shows every Tonetrace line of Ableton Live's Log.txt (newest Live 11/12 version), the lines of the Live bridge
# ("TonetraceBridge", the Remote Script) and the warnings and errors around them.
#   tools/check_live_log.sh              Tonetrace lines and the Live warnings/errors
#   tools/check_live_log.sh --all        also the lines of other plug-ins that mention VST3
# LIVE_PREFS overrides ~/Library/Preferences/Ableton (used by the tests).
set -uo pipefail

PREFS="${LIVE_PREFS:-$HOME/Library/Preferences/Ableton}"
LOG=""
# newest "Live 12.x.y" folder (version sort) that has a Log.txt
while IFS= read -r dir; do
  [[ -f "$dir/Log.txt" ]] && LOG="$dir/Log.txt"
done < <(find "$PREFS" -maxdepth 1 -type d -name 'Live 1[0-9].*' 2>/dev/null | sort -V)
if [[ -z "$LOG" || ! -f "$LOG" ]]; then
  echo "No Live Log.txt found in $PREFS"
  echo "Start Live once, then run this script again."
  exit 1
fi
echo "Log: $LOG"
echo
echo "== Tonetrace =="
if ! grep -i "tonetrace" "$LOG"; then echo "(no Tonetrace lines: the plug-in was not scanned or loaded yet)"; fi
echo
echo "== TonetraceBridge (Remote Script) =="
if ! grep "TonetraceBridge" "$LOG"; then echo "(no TonetraceBridge lines: the control surface is not selected in Live's settings, or Live was not restarted yet)"; fi
echo
echo "== Warnings and errors that mention plug-ins or VST3 =="
grep -iE "(warn|error|fail|couldn't|could not|crash).*(vst|plug-?in|tonetrace)|(vst|plug-?in|tonetrace).*(warn|error|fail|couldn't|could not|crash)" "$LOG" | tail -n 40 || true
if [[ "${1:-}" == "--all" ]]; then
  echo
  echo "== All VST3 lines =="
  grep -i "VST3" "$LOG" | tail -n 80
fi
echo
echo "Tonetrace's own log: ~/Library/Logs/Tonetrace/Tonetrace.log (open it from the diagnostics panel with the (i) button)"
