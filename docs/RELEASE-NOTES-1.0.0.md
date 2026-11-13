# Xenia 1.0.0

Xenia is now built on Akai's MPC Plugin SDK, with a new screen in the look of the Microwave XT's panel, your own sound banks, and a second skin.

## What's new
- **New screen**:
  - One display for bank and sound, with step arrows, and the engine status and DSP load below it.
  - Round keys with LEDs: RANDOM, + FAV, − FAV, PANIC, INIT PATCH and SHADOW EDITION.
  - The XT's red Cutoff and Wavetable knobs (Classic skin). The Wavetable knob on OSC turns through the tables.
  - Every block of four controls is a Q-Link group on the page.
- **Two skins**: **Classic**, the orange panel with red rings, and the **Shadow Edition**, the black panel with teal print. The SHADOW EDITION key on
  PLAY switches every Xenia on the MPC, and the choice stays until you switch again.
- **Your own sound banks**: put Microwave XT SysEx dumps (`.syx`, or a `.mid` that carries them) in the plugin folder's `xenia/banks/`. Each file, or a
  folder with one file, becomes a bank after ROM banks A and B. The BANKS page lists them ten at a time. BANK, PRESET, RANDOM and MIDI program change
  and bank select reach them too.
- **RANDOM**: a random sound from every loaded bank.
- **Favourites in MPC's PRESET list**:
  - **+ FAV** saves the current sound with your edits (up to 64).
  - Pick a favourite from the PRESET list on the track or from a pad.
  - **− FAV** removes the favourite that is picked.
- **Faster start**: the ROM's sound names are read once and kept, so a new instance no longer spends seconds reading them.
- **Projects saved with 0.1.x still load** with their sound and edits. The parameter order is unchanged, so automation and Q-Link assignments carry over.
  BANK and PRESET now run up to 63 and 255, for the user banks.

## Install
- `Xenia-1.0.0-mpc-sdk.zip`: copy the folder `sd88me - VST - Xenia` into a `VST2 Plugins` folder on a drive of the MPC and put your ROM in its
  `xenia/roms/` (sound banks in `xenia/banks/`). Needs MPC OS 3.10.1 or later, with Preferences → Enhancements (Beta) → Enable Enhancements and External
  Plugin Support switched on.
- `Xenia-1.0.0-mpc-aarch64.zip`: run `install.sh` on the device as root. A 0.1.x install is upgraded in place, and your ROM is kept.

Use one of the two, not both. 64-bit MPCs only (Live III and others); the Force and the 32-bit MPCs are not supported.

Your own Microwave II or XT ROM is required and not included.

## Notes
- Tested on an MPC Live III with a real XT ROM and 20 user banks. A project saved by 0.1.2 was tested offline with a real 0.1.2 project chunk; loading
  one on the device and the install.sh upgrade of a 0.1.x install were not tried on a device.
