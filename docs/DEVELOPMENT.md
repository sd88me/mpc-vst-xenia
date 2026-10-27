# Xenia (Gen2): development notes (state as of 2026-10-27)

Repo `mpc-vst-xenia`, GPL-3.0, Gen2/aarch64 only, built the way Osirus (`mpc-vst-osirus`) is. See README.md, src/VENDORED.md.

## State
- **Engine:** gearmulator's Microwave XT (`xtLib`: the XT's 68k firmware and its DSP56303 voice DSP) vendored in `src/gearmulator/`. `src/dsp/xenia_engine.cpp` is our own
  glue (no upstream one exists): one emulator thread per instance runs `xt::Device` through `synthLib::Plugin` (40 kHz -> 44.1 kHz), fills an int16 ring that `render()`
  drains (target fill 1024 frames), takes MIDI and parameter writes from a queue. Parameters are XT single parameter change sysex (`F0 3E 0E 7F 20 <part> <idxH> <idxL> <raw> F7`),
  at most six per block, coalesced per index. A sound is chosen by requesting the ROM single (bank A/B, program 0-99) and sending it back as the edit buffer
  (bank byte 0x30 = the Multi-mode edit buffer, location 0 = part 1, checksum redone), which also fills the value cache the UI reads. **The XT boots in Multi mode: a dump to the
  Single-mode edit buffer 0x20 is accepted and ignored** (names changed, the sound did not; found on the device, reproduced and fixed on x86). The ROM names are requested in the
  background (current bank first) and re-scanned for dumps the XT dropped (3 passes on x86 to get all 200). Ready is only reported once the first sound has arrived. Sound
  changes and name passes are logged to `/tmp/xenia.log` (`XENIA_LOG` overrides). Four assignable PLAY knobs (`play1..4` popups pick one of 24 raw 0..127 targets, `play_v1..4`
  write it) are appended at the end of the parameter list and saved in the state.
  MIDI program change and bank select (CC 0) select sounds themselves and are not forwarded. State chunk = JSON with the edit buffer dump as hex (~700 bytes).
- **ROM:** `<plugin folder>/xenia/roms/`, found by `xt::RomLoader::findROM()` (256 kB full ROM, two 128 kB halves, or an OS update `.mid`). Booted with the two 128 kB halves.
- **Done and checked:** `build/x86/xenia_engine_test <dir with roms/>` PASSES (boot 0.5-0.7 s, idle is silent, notes, F1Cutoff changes the sound, presets 0/7/14/21 sound different,
  stepper verbs, play knobs, bank/page tiles, every name of both banks, INIT, state restore); `./build.sh test` (wrapper host test, ASan/UBSan) PASSES; `./build.sh all` builds
  `xenia.so` in the bookworm image (glibc 2.35). On a Live III (MPC OS 3.9.1.2, 2026-10-27): loads, sound, bank/preset steppers, BANKS tiles, data wheel, Q-Link sub-pages,
  popups, DROPOUTS 0, the DSP thread at 35-36% of one core (x86: 21%).
- **Not yet verified on a device:** save/reload of a project, INIT PATCH, long sessions (the stuck note seen at the start of the first builds was not seen again; PANIC clears it),
  the level against a factory instrument. `bench` is meaningless for this plugin (silent PASS): measure the "DSP" thread instead (`/proc/<mpc pid>/task/<tid>/stat`, BusyBox: no `getconf`, CLK_TCK is 100).

## Parameters
`tools/gen_params.py` reads `skin/parameterDescriptions_xt.json` (page 0 = the single parameters) and writes `vst/params.json` and `src/dsp/xenia_params.h` (key, index,
raw range, mapping kind, default). 195 single parameters are exposed under the table's own names; value lists with a label for every value become option lists
(`kind 1`), sparse ones (wavetable names, effect types) are option lists over a raw-value map (`kind 3`), `signed` values lose their 64 offset, octaves are -4..4.
Not exposed: Multi parameters (pages 10-18), Global (20), the play-parameter soft knobs (30), the control W/X/Y/Z (40), the arp user pattern bits, the patch name.
Index 70 (F1Extra/F1BPOffset), 81 (EffectParamA/DelayTime) and 159/166 (LFO rates, free/synced) share one parameter each. VST indices only ever append (`vst/params.lock`).

## Skin
`tools/make_assets.py` cuts the parts from the Xenia skin art (amber chassis gradient, the wordmark with its alpha, the black knob filmstrip, the browser tile);
`tools/make_skin.py` draws the 10 pages at 1280 x 628 (Liberation Sans for the labels, Xenia-blue italic headings on thin lines, black knobs, a 30% brighter orange chassis;
selectors, boxes and the BANKS browser take Clementine-XT's scheme: olive LCD boxes with near-black text, red bezel on the top bank/preset selector, framed BANKS/SOUNDS panels) and
writes `vst/layout.conf`. `Page(name, qkeys, (title, keys)...)` gives a tab one Q-Link sub-page per extra tuple. The live controls sit over the drawn labels; popups (option lists, 32-97 entries open as
multi-column lists, `cw=` narrows a column) replace Xenia's selector bars; rates and delays with 128 labelled values are knobs. Q-Links: groups of four per column.
Preview offline: `tools/preview.sh <name>` (Docker, or a local Playwright Chromium: see the script header).
Known gaps: no Multi page, no wave editor, no patch manager (the BANKS page lists the ROM banks only), no LCD mirror (the preset display shows the sound name, not the XT's
two-line display), selectors (popups) have no Q-Link (Clementine-XT puts them on Q-Links), the play knobs show raw 0..127 values.

## Next
1. Device: save/reload a project, INIT PATCH, a long session; compare the output level with a factory instrument and set the GAIN default from it (now 250 from an x86 peak of 0.31).
2. Docs in mpc-vst-plugins for the in-process-emulator pattern (PORTING/GEN2; Osirus has the same gap).
3. Selector Q-Links, a Multi page, patch name editing, if wanted.
