# Xenia 2: native plugin port (branch rev-sdk, from v0.1.2)

## Frozen identity (from the released 0.1.2 build)
- Name `Xenia`, vendor `sd88me`, uid `XenA` (0x58656e41), folder `sd88me - VST - Xenia`, ROMs in `<folder>/xenia/roms/`.
- Gen2 (arm64) only, as every release so far: the DSP56300 JIT has no 32-bit ARM backend and the interpreter would not keep up on the Force.
- `params.lock` = the released VST table, 332 indices: the 253 keys of `vst/params.json`, then 79 hidden `<key>__open` flags
  (one per popup of `vst/layout.conf`, added by the 1.x wrapper). 253 becomes `ui_open`, 254..331 inert reserved slots, new
  parameters are appended after 331. `tools/gen_sdk_params.py` refuses any other order.
- Appended (2026-11-12): 332 `fav_add`, 333 `fav_del` (favourites = VST programs in the PRESET list, `<folder>/favourites.dat`), 334 `random_patch`.
- Beta identity: `Xenia 2 Beta`, uid `XenB`.

## Saved state
The 1.x chunk (effGetChunk of `vst2_wrap.c`) is the engine's `state` text plus its NUL:
`{"state_version":1,"bank":B,"preset":P,"gain":G,"clock":C,"p1".."p4":<PLAY target index>,"single":"<hex SysEx dump of the edited sound>"}`
(`single` only once a sound was in the edit buffer). The new front end saves the same JSON as extra state and a wrapped
`effSetChunk` hands a plain 1.x chunk to the engine unchanged.

## Status log
- 2026-11-12: phase 1-2: repo checked (v0.1.2 = default branch head, clean), rev-sdk from the tag, identity frozen.
