# Astralay

Astralay is a multi-tap delay and glitch generator, built to be fully usable with a screen reader. It comes as a VST3, CLAP and AU plugin, and as a standalone application, for Windows and macOS.

## Features

- Up to 16 delay taps, each with their own time, volume, pan, feedback, and low and high cut filters in the feedback path.
- Nine glitch types per tap, each firing at random with their own probability and each setting is randomized from ranges you set:
    - reverse
    - stutter
    - granularize
    - pitch
    - LPC formant shift
    - cepstral formant shift
    - ring modulation
    - frequency modulation
    - bit crusher
- A shared glitch grid, which can follow the host's tempo, with a global threshold, glitch lengths, and a limit on how many glitches run at once.
- Reproducible randomness: with a seed, every play-through glitches identically.
- Host sync, tape-style glide when times change, freeze, smear (diffusion), dry/wet mix and output gain.
- Presets, factory presets, and undo and redo.

## Usage

Controls are arranged in three groups: Main, Tap *number*, and global. Main houses things like undo and redo, and preset management. The Tap *number* group contains a tap selector, as well as every control for the selected tap. The group is renamed so that *number* represents what tap you're working with. For example, "Tap 3". Everything that affects the sound as a whole lives in global.

### Keyboard shortcuts

| Action | Windows | macOS |
| --- | --- | --- |
| Next control | Tab | Tab |
| Previous control | Shift+Tab | Shift+Tab |
| Next group | Alt+Period | Cmd+Period |
| Previous group | Alt+Comma | Cmd+Comma |
| Increase or decrease a slider | Arrow keys | Arrow keys |
| Fine adjustment | Shift+Arrow keys | Shift+Arrow keys |
| Coarse adjustment | Ctrl+Arrow keys | Cmd+Arrow keys |
| Slider to maximum | Home | Home |
| Slider to minimum | End | End |
| Reset slider to default | Delete | Delete (Backspace) |
| Type a value | Enter | Return |
| Accept a typed value | Enter | Return |
| Cancel typing a value | Escape | Escape |
| Undo | Ctrl+Z | Cmd+Z |
| Redo | Ctrl+Shift+Z or Ctrl+Y | Cmd+Shift+Z or Cmd+Y |
| Save a preset | Ctrl+S | Cmd+S |
| Load a preset | Ctrl+O | Cmd+O |

## Presets

User presets are saved in `Documents/Astralay/Presets` on both Windows and macOS. Access factory presets by pressing "Load", then expanding the "Factory presets" entry.

## Building

### Requirements

- CMake 3.22 or later.
- Windows: Visual Studio 2022 or its Build Tools, with the C++ workload.
- macOS: Xcode.

### Getting the source

The dependencies (JUCE, clap-juce-extensions and PFFFT) are git submodules, and clap-juce-extensions has submodules of its own, so clone recursively:

```
git clone --recursive https://github.com/ironcross32/astralay.git
```

If you've already cloned without `--recursive`:

```
git submodule update --init --recursive
```

### Windows

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### macOS

```
cmake -S . -B build -G Xcode
cmake --build build --config Release --parallel
```

macOS builds are universal binaries (Apple Silicon and Intel) and need macOS 10.13 or later. The AU is built only on macOS.

### Output

The built plugins are in `build/Astralay_artefacts/Release/`, in the `VST3`, `CLAP`, `AU` and `Standalone` folders. To use them, copy them to the usual plugin folders:

- Windows: VST3 to `C:\Program Files\Common Files\VST3`, CLAP to `C:\Program Files\Common Files\CLAP`.
- macOS: VST3 to `~/Library/Audio/Plug-Ins/VST3`, CLAP to `~/Library/Audio/Plug-Ins/CLAP`, AU to `~/Library/Audio/Plug-Ins/Components`.

## Testing

The build also produces a test program, `AstralayTests`, which checks the audio processing, glitches, formant shifting, smear, parameters, presets, undo, and the accessibility of the interface (tab order, names, groups, help tags and keyboard handling):

```
build/AstralayTests_artefacts/Release/AstralayTests
```

It exits with a non-zero status if any check fails. Run it with `--benchmark` to measure processing cost and tap-switching speed.

Every release must also pass [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10:

```
pluginval --strictness-level 10 --validate "build/Astralay_artefacts/Release/VST3/Astralay.vst3"
```

## Licence

Astralay's own source code is released under the MIT licence; see [LICENSE](LICENSE).

Astralay is built with [JUCE](https://juce.com), which it uses under the GNU Affero General Public License version 3 (AGPLv3). As a result, **distributed builds of Astralay (the plugins and the standalone application) are covered by the AGPLv3**, while the source code in this repository that isn't part of JUCE remains available under MIT. Anyone distributing builds must meet the AGPLv3's terms, including making the corresponding source available and including the AGPLv3 text, which is in [LICENSE-AGPL-3.0.txt](LICENSE-AGPL-3.0.txt).

Astralay also includes third-party code under permissive licences (PFFFT, the VST3 SDK, CLAP, clap-helpers and clap-juce-extensions). Their notices, which must accompany any release, are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
