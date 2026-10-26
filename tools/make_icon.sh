#!/bin/bash
# Regenerates the brand PNGs, docs/logo.png and assets/logo/Tonetrace.icns (needs a finished build: build/tonetrace_mklogo).
# The files are committed, so a normal build does not need this.
set -euo pipefail
cd "$(dirname "$0")/.."
build/tonetrace_mklogo assets/logo/png docs/logo.png
iconutil -c icns assets/logo/png/Tonetrace.iconset -o assets/logo/Tonetrace.icns
ls -l assets/logo/Tonetrace.icns
