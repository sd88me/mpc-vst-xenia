#!/usr/bin/env bash
# Build Xenia 2 (the native front end src/XeniaPlugin.cpp around the engine) into build/sdk/"sd88me - VST - Xenia"/ (arm64 only: the
# emulator's JIT has no 32-bit ARM backend). The engine archive comes from ./build.sh engine aarch64 (Docker, gcc); the front end is
# compiled in the same image against the plugin framework headers. The 1.x wrapper build is still ./build.sh.
#   ./build-sdk.sh            gen params, engine archive, plugin .so, folder (plugin.json, version.xml, Plugin Skins, xenia/roms)
#   ./build-sdk.sh test       tests/run.sh (ASan/UBSan, your ROM) + the builder's host_sim on a native build
#   ./build-sdk.sh beta       the same as "Xenia 2 Beta" (uid XenB): installs next to the release; carries your ROM and banks
# MPC_SDK: the plugin builder checkout. MPC_VST: mpc-vst-plugins (wrapper/engine.h). XENIA_ROMS: your ROM folder (default ./roms).
set -euo pipefail
cd "$(dirname "$0")"
SDK="${MPC_SDK:-$HOME/mpc-plugin-builder-sdk}"
[ -f "$SDK/framework/mpc_plugin.h" ] || [ -n "${MPC_SDK:-}" ] || SDK="$HOME/mpc-310-private/mpc-plugin-builder-sdk"   # its older place
MV="${MPC_VST:-$HOME/mpc-vst-plugins}"
[ -f "$SDK/framework/mpc_plugin.h" ] || { echo "need the plugin builder (MPC_SDK)" >&2; exit 1; }
IMAGE=xenia-a64-builder
NAME="Xenia"; UID_="'X','e','n','A'"; UIDS=XenA; SLUG=xenia
if [ "${1:-}" = beta ]; then NAME="Xenia 2 Beta"; UID_="'X','e','n','B'"; UIDS=XenB; SLUG=xenia2beta; fi
FOLDER="build/sdk/sd88me - VST - $NAME"
python3 tools/gen_sdk_params.py

libs() { local B="$1" l; for l in libxenia_dsp.a xtLib/libxtLib.a wLib/libwLib.a hardwareLib/libhardwareLib.a mc68k/lib68kEmu.a synthLib/libsynthLib.a \
  baseLib/libbaseLib.a libresample/libresample.a dsp56kEmu/libdsp56kEmu.a dsp56kBase/libdsp56kBase.a asmjit/libasmjit.a vtuneSdk/libvtuneSdk.a; do printf '%s ' "$B/$l"; done; }

if [ "${1:-}" = test ]; then
  tests/run.sh
  # host_sim (needs glibc >= 2.38): a plain native build, a scratch plugin folder with your ROM
  mkdir -p build/sdk-native
  g++ -std=c++17 -O2 -g -fPIC -shared -fvisibility=hidden -I"$SDK/framework" -I"$MV/wrapper" -Isrc -o build/sdk-native/xenia.so src/XeniaPlugin.cpp \
    -Wl,--start-group $(libs build/x86) -Wl,--end-group -lpthread -ldl -lm
  scratch="$(mktemp -d)"; trap 'rm -rf "$scratch"' EXIT
  mkdir -p "$scratch/xenia"; cp -r "${XENIA_ROMS:-roms}" "$scratch/xenia/roms"; cp plugin.json "$scratch/"
  HS="$SDK/.sdk-tools/host_sim"
  if [ ! -x "$HS" ]; then   # built on first use, as the SDK's build.py does
    mkdir -p "$SDK/.sdk-tools"
    g++ -std=c++17 -O2 -I"$SDK/framework" -I"$SDK/tools/host-sim" -o "$HS" "$SDK/tools/host-sim/host_sim.cpp" -ldl -lpthread -lm
  fi
  # dump: the JSON only (the emulator logs to stdout as it boots)
  MPC_PLUGIN_FOLDER="$scratch" "$HS" build/sdk-native/xenia.so dump 2>/dev/null | python3 -c '
import json, sys
t = sys.stdin.read(); d = json.JSONDecoder()
for i, c in enumerate(t):
    if c in "{[":
        try: obj, _ = d.raw_decode(t[i:]); break
        except ValueError: pass
else: sys.exit("host_sim dump: no JSON in its output")
json.dump(obj, sys.stdout, indent=1)' > build/sdk-native/params.json
  MPC_PLUGIN_FOLDER="$scratch" "$HS" build/sdk-native/xenia.so test --notes 60,64,67 --wav build/sdk-native/test.wav 2>&1 \
    | grep -v 'MIPS:\|LCD:\|^ \|GMT 2000\|HOTX\|_log@\|operator()\|Latency\|Resampler\|thread priority'
  exit "${PIPESTATUS[0]}"
fi

./build.sh engine aarch64      # vst/build/engine/libxenia_all.a, from build/a64 (always rebuilt here: the 1.x x86 test writes the same path)
rm -rf "$FOLDER"; mkdir -p "$FOLDER/VST" "$FOLDER/Plugin Skins" "$FOLDER/xenia/roms"
docker image inspect $IMAGE >/dev/null 2>&1 || docker build -q -t $IMAGE -f scripts/Dockerfile scripts
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/b -v "$SDK":/sdk:ro -v "$MV":/mv:ro -w /b $IMAGE aarch64-linux-gnu-g++ \
  -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden -fno-gnu-unique -ffunction-sections -fdata-sections \
  -Wall -Wextra -mcpu=cortex-a76 -I/sdk/framework -I/mv/wrapper -Isrc \
  -DXENIA_NAME="\"$NAME\"" -DXENIA_UID="$UID_" \
  -o "$FOLDER/VST/$SLUG-arm64.so" src/XeniaPlugin.cpp vst/build/engine/libxenia_all.a \
  -Wl,--gc-sections -Wl,--version-script=/sdk/tools/exports.map -Wl,--exclude-libs,ALL -static-libstdc++ -static-libgcc -ldl -lpthread -lm
python3 - "$NAME" "$SLUG" "$UIDS" "$FOLDER" <<'PY'
import json, sys
name, slug, uid, folder = sys.argv[1:]
m = json.load(open("plugin.json"))
m.update(name=name, uid=uid, file64="VST/%s-arm64.so" % slug)
json.dump(m, open(folder + "/plugin.json", "w"), indent=2)
open(folder + "/version.xml", "w").write('<?xml version="1.0" encoding="utf-8"?>\n<plugincontent version="1.0">\n    <version>%s.0</version>\n    <uiversion>0.1.0.0</uiversion>\n</plugincontent>\n' % m["version"])
PY
cp release/xenia/roms/PUT_XT_ROM_HERE.txt "$FOLDER/xenia/roms/"; mkdir -p "$FOLDER/xenia/banks"; cp release/xenia/banks/PUT_SYSEX_BANKS_HERE.txt "$FOLDER/xenia/banks/"; cp LICENSE CREDITS.md "$FOLDER/"
# a beta for the device gets your ROM and user banks inside its folder (mpc_remote.py update replaces the whole folder); never a release
if [ "${1:-}" = beta ]; then
  [ -d "${XENIA_ROMS:-roms}" ] && cp "${XENIA_ROMS:-roms}"/* "$FOLDER/xenia/roms/"
  [ -d "${XENIA_BANKS:-tests/data/banks}" ] && mkdir -p "$FOLDER/xenia/banks" && cp -r "${XENIA_BANKS:-tests/data/banks}"/. "$FOLDER/xenia/banks/"
fi
# the native skin from tools/make_tui.py (skins/); until it exists, the builder's generated default pages
if [ -f tools/make_tui.py ]; then python3 tools/make_tui.py; fi
if [ -d skins ]; then cp -r skins/. "$FOLDER/Plugin Skins/"
else
  [ -f build/sdk-native/params.json ] || { echo "run ./build-sdk.sh test once first (params.json for the default skin)" >&2; exit 1; }
  python3 "$SDK/tools/gen_ui.py" --params build/sdk-native/params.json --out "$FOLDER/Plugin Skins" >/dev/null
fi
python3 "$SDK/tools/validate.py" "$FOLDER" || true
echo "built: $FOLDER"
