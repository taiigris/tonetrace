#!/bin/bash
# Tonetrace: build, test, install, sign and validate the VST3 in one command.
#   ./build.sh                full run (build, unit tests, UI test, benchmark, install, validator, host test, screenshots, crash-report check)
#   ./build.sh --no-install   build, unit tests and UI test only
#   ./build.sh --release-zip  full run, then dist/Tonetrace-<version>-macOS.zip (bundle with icon, README, logo PNG, install.sh, uninstall.sh)
# Needs the Steinberg VST3 SDK in external/vst3sdk (see README).
set -euo pipefail
cd "$(dirname "$0")"

VERSION="$(head -n 1 VERSION)"
VST3_DIR="${TONETRACE_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
VST3_BUNDLE="Tonetrace.vst3"
REPORT_DIR="$HOME/Library/Logs/DiagnosticReports"
DO_INSTALL=1
DO_ZIP=0
for arg in "$@"; do
  case "$arg" in
    --no-install) DO_INSTALL=0 ;;
    --release-zip) DO_ZIP=1 ;;
    *) echo "unknown option: $arg"; exit 2 ;;
  esac
done
[[ -f external/vst3sdk/CMakeLists.txt ]] || { echo "external/vst3sdk is missing: see README (VST3 SDK holen)"; exit 1; }
FILTER='linkd\|\[Connection\]\|\[Application\]'

step() { printf '\n==> %s\n' "$1"; }
JOBS="$(sysctl -n hw.ncpu)"
crash_reports() { ls "$REPORT_DIR" 2>/dev/null | grep -c '^tonetrace_.*\.ips$' || true; }

CRASHES_BEFORE="$(crash_reports)"
echo "Crash reports of tonetrace_* before the run: $CRASHES_BEFORE"

step "Configure + build ($VERSION, $(git rev-parse --short HEAD 2>/dev/null || echo nogit))"
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j "$JOBS" 2>&1 | grep -E "(src|tools|tests)/.*(error|warning)" || true
cmake --build build -j "$JOBS" >/dev/null
cmake --build build --target validator -j "$JOBS" >/dev/null   # only the validator from the SDK's hosting examples

step "Unit tests (CTest)"
(cd build && ctest --output-on-failure) | tee build/unittests.log
(cd build && ./tonetrace_tests | tail -1)

step "UI interaction test (synthetic mouse and key events on the real view)"
build/tonetrace_uitest 2>&1 | grep -v "$FILTER" | tee build/uitest.log | tail -3
grep -q "UI INTERACTION TEST PASSED" build/uitest.log

if [[ $DO_INSTALL -eq 0 ]]; then
  echo "Done (install skipped)."; exit 0
fi

step "Benchmark (docs/PERFORMANCE.md)"
mkdir -p docs
build/tonetrace_bench --markdown docs/PERFORMANCE.md 2>&1 | grep -v "$FILTER" | tee build/bench.log | tail -4
grep -q "All targets reached." build/bench.log

step "Install VST3 to $VST3_DIR/$VST3_BUNDLE"
mkdir -p "$VST3_DIR"
rsync -a --delete "build/VST3/Release/$VST3_BUNDLE/" "$VST3_DIR/$VST3_BUNDLE/"
codesign --force --deep -s - "$VST3_DIR/$VST3_BUNDLE"
codesign --verify --deep --strict "$VST3_DIR/$VST3_BUNDLE"
lipo -archs "$VST3_DIR/$VST3_BUNDLE/Contents/MacOS/Tonetrace"
/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$VST3_DIR/$VST3_BUNDLE/Contents/Info.plist"

step "Steinberg validator on the installed VST3"
build/bin/Release/validator "$VST3_DIR/$VST3_BUNDLE" > build/validator.log 2>&1 || true
tail -4 build/validator.log
grep -q "0 tests failed" build/validator.log
if grep -qiE "warn|\[Failed\]" build/validator.log; then echo "the validator reported warnings"; exit 1; fi

step "VST3 host test"
build/tonetrace_hosttest_vst3 "$VST3_DIR/$VST3_BUNDLE" 2>&1 | grep -v "$FILTER" | tee build/hosttest.log | tail -4
grep -q "HOSTTEST VST3 PASSED" build/hosttest.log

step "UI screenshots (docs/)"
build/tonetrace_uishot docs 2>&1 | grep -v "$FILTER"

step "Crash reports"
CRASHES_AFTER="$(crash_reports)"
echo "tonetrace_* crash reports: before $CRASHES_BEFORE, after $CRASHES_AFTER"
[[ "$CRASHES_AFTER" -le "$CRASHES_BEFORE" ]] || { echo "this run produced new crash reports in $REPORT_DIR"; exit 1; }

if [[ $DO_ZIP -eq 1 ]]; then
  step "Release archive"
  PKG="Tonetrace-$VERSION"
  STAGE="build/release/$PKG"
  rm -rf build/release && mkdir -p "$STAGE" dist
  ditto --norsrc --noextattr "$VST3_DIR/$VST3_BUNDLE" "$STAGE/$VST3_BUNDLE"
  cp tools/release/README.txt "$STAGE/README.txt"
  cp docs/logo.png "$STAGE/Tonetrace-Logo.png"
  cp tools/release/install.sh uninstall.sh "$STAGE/"
  chmod +x "$STAGE/install.sh" "$STAGE/uninstall.sh"
  ZIP="dist/Tonetrace-$VERSION-macOS.zip"
  find dist -mindepth 1 -maxdepth 1 ! -name "$(basename "$ZIP")" -exec rm -rf {} +
  rm -f "$ZIP"
  (cd build/release && ditto -c -k --norsrc --noextattr --keepParent "$PKG" "../../$ZIP")
  unzip -l "$ZIP" | tail -14
  echo "Created $ZIP ($(du -h "$ZIP" | cut -f1))"
fi

printf '\nAll steps passed. Restart your DAW (or rescan plug-ins) to pick up the new build.\n'
