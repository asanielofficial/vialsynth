# VialSynth

VialSynth is a wavetable synthesizer plugin. It is built from GPLv3 synthesizer source code by way of
[VialFX](https://github.com/asanielofficial/vialfx), the effect plugin made from the same code (see
[NOTICE.md](NOTICE.md) for the origin of the code).

> **Status: early scaffold.** The project has been put together from the VialFX tree but has not been built or
> run yet. Nothing here has been compiled, tested or validated with pluginval.

## What this is

VialSynth starts from VialFX's tree and turns it back into a synth:

- **Oscillators and the ADVANCED tab come from the original synthesizer source.** The three wavetable oscillators,
  the sample oscillator, their wavetable editor and 3D display, and the ADVANCED tab (including the advanced
  settings for each oscillator) are the original files again, not the effect plugin's variants. The only
  differences are the skin loader below and the name written into exported wavetable files.
- **The new skin loader is kept.** Choosing *Custom* in the ADVANCED tab's skin selector opens the file chooser
  every time, including when a custom skin is already loaded, and it accepts both `.vialskin` and the older
  `.vitalskin` files.
- **Sign-in is compiled out** (`NO_AUTH=1` in every plugin and standalone build file), and the web-service
  addresses in the source are empty, so builds do not contact any server.
- The effect plugin target (`effects/`) is not part of this repository. It lives on in VialFX.

## Skin editor bins

The skin editor groups the interface into bins. They now describe the synth rather than the effect plugin:

| Bin | Covers |
| --- | --- |
| Global | the base skin |
| Header | logo, header |
| Oscillators | the three wavetable oscillators and the sample oscillator together |
| Oscillators 1-3 | sub-bin: the three wavetable oscillators (they share one skin section) |
| Sample | sub-bin: the sample oscillator |
| Filters | filters 1 and 2, effects filter |
| Envelopes | envelopes |
| LFO / Random | LFOs, random LFOs |
| Voice / MIDI | voice, macros, keyboard |
| Effects | all effect modules together |
| Chorus, Compressor, Delay, Distortion, Equalizer, Filter, Flanger, Phaser, Reverb | sub-bins, one per effect |
| Matrix / Mod | modulation drag and drop, modulation matrix |
| Browser | preset and popup browsers |
| Overlays / Advanced | overlays, the ADVANCED tab, the wavetable editor |

The three wavetable oscillators share a single skin section, so they are skinned together. Giving each its own bin
would need new section overrides, which changes the skin file format.

The skin file format is unchanged, so VialFX skins still load. The overrides for VialFX's extra effect modules
(Morph, Pitch Shifter, Utility, Freq Shifter, Vibrato, Tremolo) stay in the format but have no bin here, because
VialSynth has no such sections.

## Not done yet

- **Nothing has been built.** Do a first Linux build and fix whatever the compiler finds.
- **CI has never run.** The workflows under `.github/workflows/` now build the synth plugin (`plugin/`) as a VST3
  on Linux, Windows and macOS, but nobody has run them yet, so expect the first run to need fixes. They are manual
  (Actions tab) and publish VST3 zips only: the CLAP wrapper and the Windows and macOS installers belonged to the
  effect plugin and are not part of this project yet.
- **Names.** The plugin is still built as `Vial` (`Vial.vst3`), not `VialSynth`. The top-level `Makefile` install
  targets still look for `Vital.*` files, and the Debian packaging, installer folder, standalone, headless and
  test projects still carry the older names (and the original author's name in their project files). The CI
  branding check covers `src/`, `plugin/`, the `Makefile` and this README, and the built VST3 binaries.
- **FX-only code remains as dead code.** Branches guarded by `VITAL_FX` (the effect rack, table modes, spectral
  morph, vocoder, sidechain) are still in `src/` but are not compiled into the synth.
- **Presets.** No presets are included (see the conditions in [NOTICE.md](NOTICE.md)).

## Building

### Linux (VST3)

```
sudo apt-get install build-essential pkg-config libasound2-dev libfreetype6-dev libgl1-mesa-dev \
  libcurl4-openssl-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev
make vst3 CONFIG=Release
```

`make vst3` runs the build in `plugin/builds/linux_vst`; the result is under `plugin/builds/linux_vst/build/`.

## Acknowledgements

**Matt Tytel.** VialSynth would not exist without him. It is built on the open-source synthesizer code he wrote and released under the GPLv3: the audio engine, the oscillators, the modulation system and the interface. Thank you for sharing that work openly so that others can learn from it and build on it. VialSynth is an independent project and is not affiliated with or endorsed by him.

**VialFX.** The project, tooling and skin editor it grew from.

## License

GPLv3, see [LICENSE](LICENSE). If you distribute builds you must comply with that license, including making
the corresponding source available.

Additional conditions inherited with the source code are listed in [NOTICE.md](NOTICE.md).
