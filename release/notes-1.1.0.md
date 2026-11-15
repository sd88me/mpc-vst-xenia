# Xenia 1.1.0

A new PLAY page in the look of the Microwave XT's front panel, named favourites, and cleaner output.

## What's new
- **PLAY like the XT's front**: the LCD in the teal ellipse shows the engine status and DSP load, the sound and its bank (the data wheel steps them), and
  the sources of Play 1-4, each right over its knob: tap a source to pick another. The red data dial at the end of the ellipse steps the sound. Big Play 1-4
  knobs hang from the LCD on leader lines; the keys sit 2 x 3 beside the dial; Voice, Effect and Output follow below.
- **New knobs on every page**: the cone shape with a value ring instead of a pointer, red on Classic and teal on the Shadow Edition.
- **Named favourites**: **+ FAV** opens an on-screen keyboard with the sound's name; edit it and SAVE. CANCEL stores nothing.
- **SKIN** key (was SHADOW EDITION) on PLAY switches between Classic and the Shadow Edition.
- **Cleaner output**: audio reaches the MPC in floating point, so loud sounds no longer clip in the plugin.
- Projects saved with 0.1.x and 1.0.x still load; the parameter order is unchanged.

## Install
- `Xenia-1.1.0-mpc-sdk.zip`: copy the folder `sd88me - VST - Xenia` into a `VST2 Plugins` folder on a drive of the MPC and put your ROM in its
  `xenia/roms/` (sound banks in `xenia/banks/`). Needs MPC OS 3.10.1 or later, with Preferences → Enhancements (Beta) → Enable Enhancements and
  External Plugin Support switched on.
- `Xenia-1.1.0-mpc-aarch64.zip`: run `install.sh` on the device as root. An older install is upgraded in place; your ROM, banks and favourites are kept.

Use one of the two, not both. 64-bit MPCs only (Live III and others). Your own Microwave II or XT ROM is required and not included.

## Notes
- Tested on an MPC Live III as the identical beta build (Xenia 2 Beta): PLAY, the dial, the LCD source lists, the keyboard and both skins.
  Save/reload and the install.sh upgrade were not tried on the device.
