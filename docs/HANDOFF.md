# Handoff: Xenia (Microwave XT) for MPC, from the cloud session to a local one (2026-10-26)

Branch `ccr-aef1400f-qpr3a5` in `sd88me/mpc-vst-xenia` (4 commits, pushed) and in `sd88me/mpc-vst-plugins` (one NOTES.md commit on top of main, pushed).
Nothing was merged, no PR opened. Read `docs/DEVELOPMENT.md` first, then `src/VENDORED.md`.

## Done (offline only)
- gearmulator XT emulation vendored (`src/gearmulator/`, commit pins in VENDORED.md).
- `src/dsp/xenia_engine.cpp`: own glue, emulator thread + int16 ring, `xt::Device` via `synthLib::Plugin`, sysex parameters, ROM single -> edit buffer, state chunk, name harvest.
- `tools/gen_params.py` -> `vst/params.json` + `src/dsp/xenia_params.h` (195 params). `tools/make_assets.py`, `tools/make_skin.py` -> 12 pages in the Xenia look.
- `./build.sh test` passes (wrapper host test, no ROM). aarch64 `xenia.so` linked ad hoc with an Ubuntu cross toolchain (glibc 2.38, NOT releasable).

## Never run
- The engine has never booted a ROM. `test/engine_test.cpp` has never run. Boot time, `customData`/home path needs, ROM single dumps, level, CPU: all unknown.
- No Docker build (`./build.sh all`), no device install, no bench, nothing on the Live III.

## First steps locally
1. Check out both branches; `export MPC_VST=<mpc-vst-plugins checkout>` (needs the branch or main).
2. Put your XT ROM at `<dir>/roms/` (never commit it; `.gitignore` covers `/roms/`, `*.bin`, `*.mid`). `./build.sh engine x86 && build/x86/xenia_engine_test <dir> 2`. Fix what breaks.
   Expect to touch: boot waiting (statuses "Booting the XT..."), whether names arrive (tiles), `F1Cutoff` write changes the sound, state restore.
3. Measure level (GAIN default 100 passes float through; the microQ was quiet) and CPU (`DSP CPU` in STATS; "DSP" thread ticks). Lower `dsp_clock` default if needed.
4. `./build.sh all` (Docker, bookworm glibc <= 2.36), `tools/preview.sh x` to recheck the skin.
5. Device (ask before installing; it restarts MPC): `./release.sh 0.1.0` with the ROM copied into a local-only zip, or tar to `/nvme/Synths`, `sh install.sh -y -t /nvme/Synths`.
   Osirus recipe is in `mpc-vst-osirus/docs/DEVELOPMENT.md` (release.py `--extra <dir with roms>:xenia`, dist-local/ gitignored). Back up `MPC.settings`.
6. Device test list: loads, sound, bank/preset steppers, BANKS tiles, Q-Link columns, wavetable popup, data wheel, save/reload project, INIT PATCH, DROPOUTS count.
7. After a real run only: `tested.json`, README status line, NOTES.md device facts (dated), catalog entry, release workflow.

## Known gaps / decisions open
- 12 tabs (three pager screens); merge if wanted. Several pages sparse at the bottom.
- Not exposed: Multi mode, wave editor, Global menu, LCD mirror, patch name edit, arp user pattern.
- Skin is previewed only; the many TOUCH warnings are open-popup overlaps; check on device that popup lists (wavetable 97 entries, 9x11) behave.
- Docs sync in mpc-vst-plugins: only NOTES.md was added; PORTING/GEN2 entries for the in-process-emulator pattern are still unwritten (Osirus has the same gap).
