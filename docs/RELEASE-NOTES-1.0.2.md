# Xenia 1.0.2

## Fixed
- **The bank and sound in the display now take the data wheel and Q-Links.** Touch the bank or sound name and turn the data wheel. On PLAY, Q-Link 2 steps the bank and Q-Link 3 the sound (the Detune row). On BANKS, the Q-Links step both too.
- **One bank or sound a click.** A turn no longer skips every other sound, or needs two clicks per bank, and turning back steps back.

## Install
- `Xenia-1.0.2-mpc-sdk.zip`: copy the folder `sd88me - VST - Xenia` into a `VST2 Plugins` folder on a drive of the MPC. To update, copy it over the
  old folder (merge it, don't delete the old folder first) so your ROM in `xenia/roms/`, your banks and favourites stay. Needs MPC OS 3.10.1 or later, with
  Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support switched on.
- `Xenia-1.0.2-mpc-aarch64.zip`: run `install.sh` on the device as root. The installed Xenia is upgraded in place, and your ROM is kept.

Use one of the two, not both. 64-bit MPCs only (Live III and others); the Force and the 32-bit MPCs are not supported.

Projects, automation and Q-Link assignments from 1.0.x carry over unchanged.

**Checked:** on an MPC Live III (data wheel and Q-Links on the bank and sound).
