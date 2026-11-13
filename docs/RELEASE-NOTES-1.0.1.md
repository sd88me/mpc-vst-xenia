# Xenia 1.0.1

A bug-fix release.

## Fixed
- **BANKS page: a tile could ignore a tap.** After you picked one sound or bank tile and then another, tapping the first tile again sometimes did nothing, although the MPC lit it. Every tap on a sound or bank tile now loads it.

## Install
- `Xenia-1.0.1-mpc-sdk.zip`: copy the folder `sd88me - VST - Xenia` into a `VST2 Plugins` folder on a drive of the MPC. To update 1.0.0, copy it over the
  old folder (merge, don't delete it first) so your ROM in `xenia/roms/`, your banks and favourites stay. Needs MPC OS 3.10.1 or later, with
  Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support switched on.
- `Xenia-1.0.1-mpc-aarch64.zip`: run `install.sh` on the device as root. The installed Xenia is upgraded in place, and your ROM is kept.

Use one of the two, not both. 64-bit MPCs only (Live III and others); the Force and the 32-bit MPCs are not supported.

Projects, automation and Q-Link assignments from 1.0.0 carry over unchanged.
