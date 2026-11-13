#!/usr/bin/env bash
# The release: builds the release identity ("Xenia", uid XenA), runs the offline tests (host test under ASan/UBSan, host_sim),
# packs the zips (tools/package_catalog.sh) and writes their SHA256SUMS:
#   dist/Xenia-<ver>-mpc-aarch64.zip   64-bit MPCs: install.sh (MPC.settings), upgrades 0.1.x in place
#   dist/Xenia-<ver>-mpc-sdk.zip       the plugin folder, to copy into "VST2 Plugins"
set -euo pipefail
cd "$(dirname "$0")/.."
./build-sdk.sh test
./build-sdk.sh
tools/package_catalog.sh
VER=$(python3 -c 'import json; print(json.load(open("plugin.json"))["version"])')
(cd dist && sha256sum "Xenia-$VER-mpc-aarch64.zip" "Xenia-$VER-mpc-sdk.zip" > "Xenia-$VER-SHA256SUMS.txt" && cat "Xenia-$VER-SHA256SUMS.txt")
