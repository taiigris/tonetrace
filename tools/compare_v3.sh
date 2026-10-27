#!/bin/bash
# Builds tools/compare_v3.cpp against the v3 sources (v3-mapdraw from the local backup bundle) and against this tree,
# runs both and prints a markdown table (F1 per mix and Quick Grab amount, pitch accuracy, short notes).
#   tools/compare_v3.sh [work-dir] [real-wav]     the work dir defaults to a temp directory; nothing is downloaded.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
WORK="${1:-$(mktemp -d)}"
REAL="${2:-}"
BUNDLE="${TONETRACE_BUNDLE:-$HOME/Developer/tonetrace-history-backup-2026-10-26.bundle}"
mkdir -p "$WORK"
if [[ ! -d "$WORK/v3/.git" ]]; then
  git clone -q "$BUNDLE" "$WORK/v3"
  git -C "$WORK/v3" checkout -q v3-mapdraw
fi
build() {  # build <name> <src-dir> <extra flags...>
  local name="$1" src="$2"; shift 2
  local -a files
  files=("$src"/analysis/{SpectralFrames,NoteAnalyzer,FftPortable,FftAccelerate,Hpss,Spectrogram,DrumFilter,QuickGrab,KeyEstimator}.cpp)
  [[ -f "$src/analysis/PitchLines.cpp" ]] && files+=("$src"/analysis/PitchLines.cpp)
  clang++ -std=c++17 -O2 -I"$src" -I"$ROOT/tests" -I"$WORK" "$@" "${files[@]}" "$ROOT/tools/compare_v3.cpp" -framework Accelerate -o "$WORK/compare_$name"
}
build v3 "$WORK/v3/src" -DTT_V3
build now "$ROOT/src" -I"$ROOT/build/generated"
"$WORK/compare_v3" > "$WORK/v3.txt"
"$WORK/compare_now" > "$WORK/now.txt"
STATUS=0
python3 -I - "$WORK/v3.txt" "$WORK/now.txt" <<'EOF' || STATUS=1
import sys
def load(p):
    rows = {}
    for line in open(p):
        n, a, f1, pr, rc, pa, notes, short = line.strip().split("|")
        rows[(n, a)] = (float(f1), float(pr), float(rc), float(pa), int(notes), int(short))
    return rows
v3, now = load(sys.argv[1]), load(sys.argv[2])
print("| Mix | Quick Grab | F1 v3 | F1 4.3 | Pitch v3 | Pitch 4.3 | Short v3 | Short 4.3 | OK |")
print("|---|---|---|---|---|---|---|---|---|")
bad = 0
for k in v3:
    a, b = v3[k], now[k]
    ok = b[0] + 1e-9 >= a[0]
    bad += 0 if ok else 1
    print(f"| {k[0]} | {float(k[1])*100:.0f} % | {a[0]:.3f} | {b[0]:.3f} | {a[3]:.2f} | {b[3]:.2f} | {a[5]} | {b[5]} | {'yes' if ok else 'NO'} |")
sys.exit(1 if bad else 0)
EOF
if [[ -n "$REAL" ]]; then
  echo; echo "Real file (first 30 s, read only):"
  echo "v3:  $("$WORK/compare_v3" file "$REAL" 30 | tr '\n' ' ')"
  echo "4.3: $("$WORK/compare_now" file "$REAL" 30 | tr '\n' ' ')"
fi
exit $STATUS
