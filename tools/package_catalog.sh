#!/usr/bin/env bash
# The release zips, from the release build (build/sdk/"sd88me - VST - Xenia", made by ./build-sdk.sh):
#   dist/Xenia-<ver>-mpc-aarch64.zip   install.sh / uninstall.sh (MPC.settings) for 64-bit MPCs, made by mpc-vst-plugins' tools/release.py;
#                                      it upgrades a 0.1.x install in place
#   dist/Xenia-<ver>-mpc-sdk.zip       the plugin folder for "VST2 Plugins" (tools/sdk_zip.py)
# As in 0.1.x the .so is xenia.so at the folder root (plugin.json names it, so the plugin finds its folder and xenia/ beside it).
# User data, kept on upgrade and uninstall: xenia/roms (your ROM), xenia/banks (your sound banks), xenia-favourites.dat,
# xenia-skin.txt (Classic / Shadow Edition). No ROM or bank is ever packed. MPC_VST: mpc-vst-plugins with tools/sdk_zip.py.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
MPC_VST="${MPC_VST:-$HOME/mpc-vst-plugins-postlaunch}"
[ -f "$MPC_VST/tools/release.py" ] && [ -f "$MPC_VST/tools/sdk_zip.py" ] || { echo "need mpc-vst-plugins with tools/sdk_zip.py (MPC_VST)" >&2; exit 1; }
DIR="sd88me - VST - Xenia"; SRC="$here/build/sdk/$DIR"
[ -f "$SRC/VST/xenia-arm64.so" ] || { echo "missing $SRC: run ./build-sdk.sh first" >&2; exit 1; }
VER=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$SRC/plugin.json")
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); sys.exit(0 if m["uid"]=="XenA" and "Beta" not in m["name"] else "not the release identity")' "$SRC/plugin.json"
out="$here/dist"; mkdir -p "$out"
st="$here/build/catalog/aarch64"; rm -rf "$st"; mkdir -p "$st/$DIR" "$st/extra"
cp -r "$SRC/Plugin Skins" "$SRC/version.xml" "$st/$DIR/"
python3 - "$SRC/plugin.json" "$st/$DIR/plugin.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
m.pop("file32", None)
m["file64"] = "xenia.so"
json.dump(m, open(sys.argv[2], "w"), indent=2)
PY
cp "$SRC/VST/xenia-arm64.so" "$st/xenia.so"
cp -r "$SRC/xenia/." "$st/extra/"; cp "$here/LICENSE" "$here/CREDITS.md" "$st/extra/"
# nothing of the user's may ride along: only the placeholders
if find "$st" -type f | grep -v -E 'PUT_(XT_ROM|SYSEX_BANKS)_HERE\.txt$' | grep -i -E '/(roms|banks)/|\.(bin|syx|mid)$|favourites|skin\.txt|names\.cache'; then
  echo "refusing: user files in the package" >&2; exit 1
fi
printf '<PLUGIN name="Xenia" descriptiveName="Xenia" format="VST" category="Synth" manufacturer="sd88me" version="%s" file="xenia.so" uid="58656e41" isInstrument="1" fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>\n' "$VER" > "$st/entry.xml"
python3 "$MPC_VST/tools/release.py" --so "$st/xenia.so" --skin "$st/$DIR" --entry "$st/entry.xml" --version "$VER" --extra "$st/extra:xenia" \
  --user-data xenia/roms --user-data xenia/banks --user-data xenia-favourites.dat --user-data xenia-skin.txt \
  --user-files "$here/release/userfiles.json" \
  --repo sd88me/mpc-vst-xenia --license GPL-3.0-only --id xenia \
  --requires "64-bit MPC (Live III and others) and your own Microwave II/XT ROM" \
  --about "Waldorf Microwave XT emulation with a native MPC panel (Classic or Shadow Edition), your own SysEx sound banks, RANDOM and favourites." \
  -o "$st/dist"
z=$(ls "$st/dist"/*.zip); cp "$z" "$out/Xenia-$VER-mpc-aarch64.zip"
python3 "$MPC_VST/tools/catalog_check.py" "$out/Xenia-$VER-mpc-aarch64.zip" --catalog --expect-id xenia --expect-repo sd88me/mpc-vst-xenia
python3 "$MPC_VST/tools/sdk_zip.py" "$out/Xenia-$VER-mpc-sdk.zip" "$out/Xenia-$VER-mpc-aarch64.zip"
for f in "$out/Xenia-$VER-mpc-aarch64.zip" "$out/Xenia-$VER-mpc-sdk.zip"; do   # last guard: no ROM-like file in either zip
  if python3 -I -m zipfile -l "$f" | grep -i -E '\.(bin|syx|rom)\b|\.mid\b' | grep -v PUT_; then echo "refusing: a ROM-like file in $f" >&2; exit 1; fi
done
ls -la "$out"/Xenia-"$VER"-mpc-*.zip
