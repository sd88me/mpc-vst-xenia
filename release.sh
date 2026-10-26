#!/usr/bin/env bash
# Build the release zip (no ROMs in it) and check it against the plugin catalog's rules.
#   ./release.sh <version>            e.g. ./release.sh 0.2.3   -> dist/Xenia-<version>-mpc-aarch64.zip
# Then publish it: gh release create v<version> dist/Xenia-<version>-mpc-aarch64.zip --title "Xenia <version>" --notes-file <notes>
# MPC_VST: checkout of mpc-vst-plugins (default ~/mpc-vst-plugins), (main).
set -euo pipefail
cd "$(dirname "$0")"
VER="${1:?usage: ./release.sh <version>}"
MPC_VST="${MPC_VST:-$HOME/mpc-vst-plugins}"
./build.sh all
rm -rf "dist/Xenia-$VER-mpc-aarch64.zip"
EXTRA="$(mktemp -d)"; trap 'rm -rf "$EXTRA"' EXIT
mkdir -p "$EXTRA/roms"; cp release/xenia/roms/PUT_XT_ROM_HERE.txt "$EXTRA/roms/"; cp LICENSE CREDITS.md "$EXTRA/"        # the installed plugin folder carries its licence and credits
python3 "$MPC_VST/tools/release.py" --so vst/build/aarch64/xenia.so --skin "vst/build/skin/sd88me - VST - Xenia" \
  --entry vst/build/pluginlist-entry.xml --version "$VER" --extra "$EXTRA:xenia" \
  --repo sd88me/mpc-vst-xenia --license GPL-3.0-only --id xenia --requires "your own Microwave XT ROM" -o dist
python3 "$MPC_VST/tools/catalog_check.py" "dist/Xenia-$VER-mpc-aarch64.zip" --catalog --expect-id xenia --expect-repo sd88me/mpc-vst-xenia
# the package must carry no ROM: refuse anything that looks like one
if python3 -I -m zipfile -l "dist/Xenia-$VER-mpc-aarch64.zip" | grep -i -E '\.(bin|mid|syx|rom)\b' | grep -v "PUT_"; then
  echo "refusing: a ROM-like file is in the zip" >&2; exit 1
fi
echo "ok: dist/Xenia-$VER-mpc-aarch64.zip"
