# Credits and licences

Xenia stands on the work of others. Each licence applies to its own part; the plugin as a whole is **GPL-3.0** (`LICENSE`).

- **[gearmulator](https://github.com/dsp56300/gearmulator)** by Dieter Mueller (dsp56300) and contributors, **GPL-3.0**: the Waldorf Microwave XT emulation
  (`xtLib`, `wLib`), its 68k and DSP56300 emulators and JIT, and the support libraries (`synthLib`, `baseLib`, `hardwareLib`). "Xenia" is gearmulator's name
  for its Microwave XT plugin. Vendored through [charlesvestal/gearmulator](https://github.com/charlesvestal/gearmulator).
- **[dsp56300](https://github.com/dsp56300/dsp56300)** and **[mc68k](https://github.com/dsp56300/mc68k)** (submodules of gearmulator), **GPL-3.0**; **Musashi** (in mc68k), MIT-style licence of its author Karl Stenerud.
- **[AsmJit](https://github.com/asmjit/asmjit)**, **zlib**; **libresample**, BSD / LGPL (`src/gearmulator/3rdparty/libresample/`).
- **The "xtDefault" skin of the Xenia plugin by theusualsuspects**, **GPL-3.0** (shipped with gearmulator): the amber chassis, the "Xenia" wordmark, the knob
  filmstrip and the look every page is drawn in (`skin/xtDefault/`).
- **Liberation Sans** (Red Hat / Google, **SIL OFL 1.1**), the labels baked into the pages (`vst/fonts/`).
- **[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)**: the plugin wrapper (`wrapper/vst2_wrap.c`) and the skin tools.

Xenia is an independent project, not affiliated with or endorsed by Waldorf Music. "Microwave" and "XT" are the names of the instrument it emulates.
No ROM is included, and none may be shared.
