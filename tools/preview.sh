#!/usr/bin/env bash
# Skin only, no engine build: regenerate params + layout.conf + the skin folder, then draw every page into vst/preview/<name>/page_N.png.
#   tools/preview.sh [name]      (default "latest"; builds nothing for the device)
# Renders with Docker's mpc-vst-html-art image, or, with no Docker daemon, with a local Chromium: set PYTHONPATH to a Playwright
# install (pip install --target) and, when its browser build differs from the installed Chromium, PW_CHROME to that chrome binary.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$HOME/mpc-vst-plugins}"
OUT="vst/preview/${1:-latest}"
python3 tools/gen_params.py
python3 tools/make_skin.py
mkdir -p "$OUT"
if docker info >/dev/null 2>&1; then
  docker image inspect mpc-vst-html-art >/dev/null 2>&1 || docker build -q -t mpc-vst-html-art "$MPC_VST/tools/html_art" >/dev/null
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w mpc-vst-html-art python3 /mv/tools/gen_vst.py vst/vst.json 2>&1 | grep -v "^warning: skin:" || true
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w python:3.11-slim sh -c \
    "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 /mv/tools/studio.py preview 'vst/build/skin/sd88me - VST - Xenia/Plugin Skins' -o $OUT/page_%d.png" | grep -v open
else
  python3 "$MPC_VST/tools/gen_vst.py" vst/vst.json 2>&1 | grep -v "^warning: skin:" || true
  python3 "$MPC_VST/tools/studio.py" preview "vst/build/skin/sd88me - VST - Xenia/Plugin Skins" -o "$OUT/page_%d.png" | grep -v open
fi
