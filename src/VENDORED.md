# Vendored source

- `gearmulator/` (framework/{baseLib,synthLib,hardwareLib}, 3rdparty/libresample, cpu/mc68k, cpu/dsp56300/{dsp56kBase,dsp56kEmu,asmjit,vtuneSdk},
  waldi/common/wLib, waldi/xt/xtLib): from https://github.com/charlesvestal/gearmulator commit `69981792efa44114aaafda91bed69faa4c96fc64`
  (a snapshot of dsp56300/gearmulator, "bump version to .20"), with its submodules `source/cpu/dsp56300` at `848923e5f6faa481405d034072715a589e6b3326`
  (asmjit at `3577608cab0bc509f856ebf6e41b2f9d9f71acc4`) and `source/cpu/mc68k` at `c0766a6b88f9bf6d3feae44fb0ec3da5b637065c`. GPL-3.0, unmodified.
  Only the Waldorf Microwave XT subset is vendored ("xtLib" is gearmulator's Xenia): no JUCE, no editor, no other synth. The directory layout is
  gearmulator's own, so a re-vendor is a plain copy (`CMakeLists.txt` adds each library the way gearmulator's top-level one does).
- `skin/parameterDescriptions_xt.json`: gearmulator's `source/waldi/xt/xtJucePlugin/parameterDescriptions_xt.json` (the XT parameter table the Xenia
  editor is built from), unmodified. `tools/gen_params.py` turns it into `vst/params.json` and `src/dsp/xenia_params.h`.
- `skin/xtDefault/`: the art of the "xtDefault" skin of gearmulator's Xenia plugin (theusualsuspects), only the background and the knob filmstrip, unmodified (see CREDITS.md).
- `src/dsp/xenia_engine.cpp`: ours. There is no upstream glue for the XT to adapt (Osirus adapted schwung-virus; schwung-vavra is the microQ's).
  It drives xtLib's `xt::Device` through gearmulator's `synthLib::Plugin` on a thread of MPC's own process.
