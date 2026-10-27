#!/bin/bash
# Builds tools/accuracy.cpp against a source tree and runs it.
#   tools/accuracy.sh <src-dir> <out-binary>     e.g. tools/accuracy.sh src /tmp/acc_now ; then /tmp/acc_now mixes
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="${1:-src}"
OUT="${2:-build/accuracy}"
GEN="${TONETRACE_GENERATED:-build/generated}"
FILES=$(ls "$SRC"/analysis/*.cpp)
# shellcheck disable=SC2086
clang++ -std=c++17 -O2 -I"$SRC" -Itests -I"$GEN" $FILES tools/accuracy.cpp -framework Accelerate -o "$OUT"
