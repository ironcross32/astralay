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

Controls are arranged in four groups: Main, Tap *number*, Global, and Performance. Main houses things like undo and redo, and preset management. The Tap *number* group contains a tap selector, as well as every control for the selected tap. The group is renamed so that *number* represents what tap you're working with. For example, "Tap 3". Everything that affects the sound as a whole lives in global.

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
| Reset slider to default | Delete | Forward delete (Fn+Delete) |
| Type a value | Enter | Return |
| Accept a typed value | Enter | Return |
| Cancel typing a value | Escape | Escape |
| Undo | Ctrl+Z | Cmd+Z |
| Redo | Ctrl+Shift+Z | Cmd+Shift+Z |
| Save a preset | Ctrl+S | Cmd+S |
| Load a preset | Ctrl+O | Cmd+O |
| Toggle host sync | Ctrl+Y | Cmd+Y |
| Toggle freeze | Alt+F | Cmd+F |
| Switch to tap 1 to 10 | 1 to 9, then 0 | 1 to 9, then 0 |
| Switch to tap 11 to 16 | Shift+1 to Shift+6 | Shift+1 to Shift+6 |
| Previous or next tap | Minus or Equals | Minus or Equals |
| Turn the selected tap on or off | Backspace | Delete (Backspace) |
| Copy a tap or one of its settings | Ctrl+C | Cmd+C |
| Paste | Ctrl+V | Cmd+V |
| Paste to all taps | Ctrl+Shift+V | Cmd+Shift+V |

The tap keys work from any control and leave focus where it is, so you can stay on one control, such as Time, and step through the taps to set each one.

Copy and paste act on the whole tap when focus is on the tap selector or the tap's on/off toggle, and on a single setting when focus is on that setting's slider. Whether a tap is on is never copied.

### Performance area

The Performance group holds a single control, the performance area. While it has focus, the keys below replace the tap keys, copy and paste, and Alt+F. They act on a selection of taps that starts as all of them and is separate from the selected tap.

| Action | Key |
| --- | --- |
| Add or remove tap 1 to 10 | 1 to 9, then 0 |
| Add or remove tap 11 to 16 | Shift+1 to Shift+6 |
| Select all taps, or none | Backspace |
| Select the even taps, or the odd ones | Shift+Backspace |
| Make the selected taps' times 10% longer or shorter | Up or Down arrow |
| Freeze while held | F |
| Switch freeze on or off | Shift+F |

The arrows only move taps that are on, and move them by one note value while host sync is on. If any of them would pass its limit, none move. The arrows and the held freeze are not announced.

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

Or, with [Ninja](https://ninja-build.org):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Leave out `--target` so that `cmake --build` builds everything: all four formats and the test program. If it prints `ninja: no work to do`, everything is already up to date; add `--clean-first` to rebuild from scratch. If a build stops partway through, the bundle folders can still exist with nothing inside them, and macOS then reports the standalone as damaged. Check that `Contents/MacOS` inside each bundle holds an `Astralay` binary.

macOS builds are universal binaries (Apple Silicon and Intel) and need macOS 12 or later, the oldest version Xcode 27 supports. If you're reusing a build folder configured with an older minimum version, delete it or pass `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`, since CMake keeps the old value in its cache. The AU is built only on macOS.

Every bundle is ad-hoc signed as the last step of the build, which Apple Silicon needs before it will load the plugins or run the standalone. No `xattr` or `chmod` is needed for local builds. Ad-hoc signing isn't enough for builds you distribute: those need signing with a Developer ID and notarising.

### Output

The built plugins are in `build/Astralay_artefacts/Release/`, in the `VST3`, `CLAP`, `AU` and `Standalone` folders. To use them, copy them to the usual plugin folders:

- Windows: VST3 to `C:\Program Files\Common Files\VST3`, CLAP to `C:\Program Files\Common Files\CLAP`.
- macOS: VST3 to `~/Library/Audio/Plug-Ins/VST3`, CLAP to `~/Library/Audio/Plug-Ins/CLAP`, AU to `~/Library/Audio/Plug-Ins/Components`. Create the folders if they don't exist, and copy with `ditto` (for example `ditto build/Astralay_artefacts/Release/VST3/Astralay.vst3 ~/Library/Audio/Plug-Ins/VST3/Astralay.vst3`) so the signature is kept.

To check the AU on macOS, run `auval -v aufx Alay Ilbs`. If a host doesn't show Astralay after you replace a broken copy, clear its plugin cache and re-scan. In Reaper, that's Preferences, Plug-ins, VST, "Clear cache/re-scan".

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
