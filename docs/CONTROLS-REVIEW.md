# Controls review against JV-880 PR #22 (2026-11-13)

Lessons from sd88me/mpc-vst-jv880 PR #22 (patch/bank controls), checked against this port. Not yet fixed or tested on hardware.

Background, measured on an MPC One+: a Q-Link tick moves the host value by 1/128 of the range. While the host keeps moving a param the worker holds its read-back for 300 ms, so the host value can run ahead of the engine. After a read-back the Q-Link sometimes carries on from its own old value (about 30% of the time).

Xenia's front end (src/XeniaPlugin.cpp) is the same design JV-880 had before the PR, so most of these apply directly.

## Findings

1. **Stuck tiles: FIXED on branch tile-held-fix (reproduced in host_test first; checked on the Live III 2026-11-13 with Xenia 2 Beta: sound tiles 5, 6, 5 and bank tiles B, A, B all fire, one tile lit).** When the worker turns a tile off (`pushValue(i, on)` in the kTile branch of workerMain), `held_[i]` stays true, because the host sends no 0 for a tile the engine switched off. The next tap on that tile is then dropped (`held_.exchange(true)` returns true) while the host lights it. Fix: clear `held_[i]` when a tile is pushed off. Also consider reading every tile back on the pass right after a tap.
2. **PRESET (0..255) skips on a Q-Link turn (to verify).** One tick moves about 2 presets, and `settle()` follows the host value, so a turn lands on every other preset. The data-wheel special case only covers moves within 3 of the current preset. JV-880 uses `nudge_pct` plus the `ahead_` turn logic for one patch a tick.
3. **BANK (0..63):** fine for a tick (about 0.5 a tick). Check its max against the real bank count.
4. Not applicable: no MIDI CC/NRPN control path, and no step-into-browsed-bank rule.
