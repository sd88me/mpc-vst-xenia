#!/usr/bin/env bash
# Make tests/data/legacy-0.1.2.chunk: what a project saved with the released 0.1.2 build stores (effGetChunk), from that build's own code.
# Builds the v0.1.2 tree (engine + mpc-vst-plugins' wrapper) for x86 as a shared library, loads it like a host with your ROM, and saves
# the chunk. Not committed: it carries a ROM sound's bytes. XENIA_ROMS: a folder with the ROM (default ./roms). MPC_VST: mpc-vst-plugins.
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$HOME/mpc-vst-plugins}"
ROMS="$(cd "${XENIA_ROMS:-roms}" && pwd)"
W=build/legacy; rm -rf "$W"; mkdir -p "$W/src" "$W/plug/xenia"
git archive v0.1.2 | tar -x -C "$W/src"
ln -s "$ROMS" "$W/plug/xenia/roms"
( cd "$W/src" && python3 "$MV/tools/gen_vst.py" vst/vst.json --params-h >/dev/null )
cmake -S "$W/src" -B "$W/eng" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMPC_VST="$MV" >/dev/null
cmake --build "$W/eng" --target xenia_dsp -j"$(nproc)" >/dev/null
B="$W/eng"; libs=""
for l in libxenia_dsp.a xtLib/libxtLib.a wLib/libwLib.a hardwareLib/libhardwareLib.a mc68k/lib68kEmu.a synthLib/libsynthLib.a baseLib/libbaseLib.a \
         libresample/libresample.a dsp56kEmu/libdsp56kEmu.a dsp56kBase/libdsp56kBase.a asmjit/libasmjit.a vtuneSdk/libvtuneSdk.a; do libs="$libs $B/$l"; done
gcc -O1 -g -fPIC -std=gnu11 -I"$W/src/vst/build" -I"$MV/wrapper" -c "$MV/wrapper/vst2_wrap.c" -o "$W/vst2_wrap.o"
g++ -shared -o "$W/plug/xenia.so" "$W/vst2_wrap.o" -Wl,--start-group $libs -Wl,--end-group -lpthread -lm -ldl
g++ -O1 -g -std=c++17 -o "$W/dump" tests/legacy_dump.cpp -ldl -lpthread
"$W/dump" "$W/plug/xenia.so" tests/data/legacy-0.1.2.chunk
