# Xenia (Gen2): development notes (state as of 2026-10-26)

Repo `mpc-vst-xenia`, GPL-3.0, Gen2/aarch64 only, built the way Osirus (`mpc-vst-osirus`) is. See README.md, src/VENDORED.md.

## State
- **Engine:** gearmulator's Microwave XT (`xtLib`: the XT's 68k firmware and its DSP56303 voice DSP) vendored in `src/gearmulator/`. `src/dsp/xenia_engine.cpp` is our own
  glue (no upstream one exists): one emulator thread per instance runs `xt::Device` through `synthLib::Plugin` (40 kHz -> 44.1 kHz), fills an int16 ring that `render()`
  drains (target fill 1024 frames), takes MIDI and parameter writes from a queue. Parameters are XT single parameter change sysex (`F0 3E 0E 7F 20 <part> <idxH> <idxL> <raw> F7`),
  at most six per block, coalesced per index. A sound is chosen by requesting the ROM single (bank A/B, program 0-99) and sending it back as the edit buffer
  (bank byte 0x20, location 0, checksum redone), which also fills the value cache the UI reads. The ROM names are requested in the background (current bank first).
  MIDI program change and bank select (CC 0) select sounds themselves and are not forwarded. State chunk = JSON with the edit buffer dump as hex (~700 bytes).
- **ROM:** `<plugin folder>/xenia/roms/`, found by `xt::RomLoader::findROM()` (256 kB full ROM, two 128 kB halves, or an OS update `.mid`). No ROM was available when this was
  written: **the engine has never booted a ROM**. Everything about booting, timing, level, CPU and the preset dumps is untested.
- **Offline, done:** x86 build; the wrapper host test (`./build.sh test`: PASSED, ASan/UBSan, 245 parameters + the popups' hidden ones, no ROM, so the engine reports
  `No Microwave XT ROM in xenia/roms/`); the aarch64 engine and `xenia.so` link (cross-compiled with Debian/Ubuntu `g++-aarch64-linux-gnu`, `--no-undefined`, exports only
  `VSTPluginMain`); the skin builds and previews (`tools/preview.sh`).
- **Not done:** `test/engine_test.cpp` (needs a ROM) has not run; `./build.sh all` through Docker (the session had no Docker daemon; the release build must use the
  bookworm image so the glibc stays <= 2.36: the ad-hoc Ubuntu 24.04 link needs 2.38); `bench` (meaningless for this plugin: measure the "DSP" thread instead); the device.

## Parameters
`tools/gen_params.py` reads `skin/parameterDescriptions_xt.json` (page 0 = the single parameters) and writes `vst/params.json` and `src/dsp/xenia_params.h` (key, index,
raw range, mapping kind, default). 195 single parameters are exposed under the table's own names; value lists with a label for every value become option lists
(`kind 1`), sparse ones (wavetable names, effect types) are option lists over a raw-value map (`kind 3`), `signed` values lose their 64 offset, octaves are -4..4.
Not exposed: Multi parameters (pages 10-18), Global (20), the play-parameter soft knobs (30), the control W/X/Y/Z (40), the arp user pattern bits, the patch name.
Index 70 (F1Extra/F1BPOffset), 81 (EffectParamA/DelayTime) and 159/166 (LFO rates, free/synced) share one parameter each. VST indices only ever append (`vst/params.lock`).

## Skin
`tools/make_assets.py` cuts the parts from the Xenia skin art (amber chassis gradient, the wordmark with its alpha, the black knob filmstrip, the browser tile);
`tools/make_skin.py` draws the 12 pages at 1280 x 628 (Liberation Sans for the labels, blue italic headings on thin lines like the original, black knobs, blue selector bars,
the preset in a green dot-matrix LCD in a brown bezel) and writes `vst/layout.conf`. The live controls sit over the drawn labels; popups (option lists, 32-97 entries open as
multi-column lists, `cw=` narrows a column) replace Xenia's selector bars; rates and delays with 128 labelled values are knobs. Q-Links: groups of four per column.
Preview offline: `tools/preview.sh <name>` (Docker, or a local Playwright Chromium: see the script header).
Known gaps: no Multi page, no wave editor, no patch manager (the BANKS page lists the ROM banks only), no LCD mirror (the preset display shows the sound name, not the XT's
two-line display), pages are not tuned for density (MIXER, LFO and FILTER leave room at the bottom).

## Next (needs the ROM and a device)
1. `build/x86/xenia_engine_test <dir with roms/>`; fix what a first boot shows (boot time, whether `xt::Device` needs `customData` or a home path, the ROM single dumps, level).
2. Level: the engine passes the float output through unchanged at GAIN 100 (the microQ was quiet in Schwung: -27..-35 dBFS); set the default from a measurement.
3. CPU on a Live III at DSP CLOCK 100 and lower; the `STATS` readout (`DROPOUTS n DSP CPU x%`) is wired like Osirus's.
4. Device test list: loads, sound, bank/preset steppers, BANKS tiles, Q-Link columns, popups (wavetable list), save/reload a project, `INIT PATCH`.
5. Catalog entry (`catalog/plugins/xenia.json`, `"asset_pattern": "*-mpc-aarch64.zip"`), release workflow, `tested.json` only after a real run.
