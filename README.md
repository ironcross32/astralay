# Astralay

Astralay is a screen-reader-first multi-tap delay and glitch generator. It comes as a VST3, CLAP and AU plugin, and as a standalone application for Windows and macOS.

## Features

- Up to 16 delay taps, each with their own time, volume, pan, feedback, and filters in the feedback path.
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
- A shared glitch grid, which can follow the host's tempo, with shared glitch lengths and a limit on how many glitches run at once.
- Reproducible randomness: Glitches follow a pattern set by the seed you enter.
- Eight macros, each moving any number of controls at once from a single value you can automate or control via MIDI.
- MIDI CC and pitch-bend learn, with reusable mappings and project-state recall.

## Usage

The most basic setup is to place Astralay on an armed track that accepts audio input or a track with audio already on it. Once done, you can adjust the taps to taste, bring in glitches, freeze the audio, crank up the wet, bring in a bit more smear or whatever suits your fancy.

There are two ways to set things up if you want MIDI as well. You can either send MIDI from another track to Astralay, or you can put Astralay on a MIDI track and send audio to the first stereo pair, it's your choice. Press ALT+L (CMD+L on Mac) while focused on a control to begin the MIDI learn process, then move a fader or encoder on your controller/ All MIDI CCs are included, as is the pitch wheel. This also gives you mod wheel since it's CC1. Once done, you now have the entire range of the control at your fingertips.

If that's not what you want, this is where Macros come in. Instead of MIDI learning a control, arm a Macro with ALT+M (CMD+M on Mac) then press a number from 1 to 8. Now the adjustments you make with the arrow keys represents how much the macro can modulate that control. To disarm, repeat that same command again, but press either the same number as before or press 0. Now you can MIDI learn the macro value slider. By doing this, you constrain the amounts the controls are allowed to go when the macro modulates them. This is probably what you'll want to do more often than not. You can also bind more than one control to a single macro. An example of this would include raising the glide time in the global section, then making the same macro increase one tap's time while decreasing another's.

Both MIDI learn and macro arming can be done via dedicated buttons. The MIDI learn button is part of the main grouping, while each macro is its own grouping, and you'll find an arm button in there. You'll know which, if any, macro is armed, because its button will read, "Disarm" instead. Only one macro can be armed at a time. Arming a second macro while a first is armed will disarm it.

### Keyboard shortcuts

| Action | Windows | macOS |
| --- | --- | --- |
| Next control | Tab | Tab |
| Previous control | Shift+Tab | Shift+Tab |
| Next group | Alt+Period | Option+Period |
| Previous group | Alt+Comma | Option+Comma |
| Increase or decrease a slider | Arrow keys | Arrow keys |
| Fine adjustment | Shift+Arrow keys | Shift+Arrow keys |
| Coarse adjustment | Ctrl+Arrow keys | Cmd+Arrow keys |
| Slider to maximum | Home | Home |
| Slider to minimum | End | End |
| Reset slider to default | Delete | Forward delete (Fn+Delete) |
| Type a value | Enter | Return |
| Accept a typed value | Enter | Return |
| Cancel typing a value | Escape | Escape |
| Put back the preset name, in its field | Escape | Escape |
| Undo | Ctrl+Z | Cmd+Z |
| Redo | Ctrl+Shift+Z | Cmd+Shift+Z |
| Save a preset | Ctrl+S | Cmd+S |
| Load a preset | Ctrl+O | Cmd+O |
| Toggle host sync | Ctrl+Y | Cmd+Y |
| Toggle freeze | Alt+F | Cmd+F |
| Learn focused control, or cancel MIDI learn | Alt+L | Cmd+L |
| Arm or disarm a macro | Alt+M, then 1 to 8 | Cmd+M, then 1 to 8 |
| Disarm the armed macro | Alt+M, then 0 | Cmd+M, then 0 |
| Switch to tap 1 to 10 | 1 to 9, then 0 | 1 to 9, then 0 |
| Switch to tap 11 to 16 | Shift+1 to Shift+6 | Shift+1 to Shift+6 |
| Previous or next tap | Minus or Equals | Minus or Equals |
| Turn the selected tap on or off | Backspace | Delete (Backspace) |
| Copy a tap or one of its settings | Ctrl+C | Cmd+C |
| Paste | Ctrl+V | Cmd+V |
| Paste to all taps | Ctrl+Shift+V | Cmd+Shift+V |
| Open a control's context menu | Right bracket | Right bracket, or VO+Shift+M |

The tap keys work from any control and leave focus where it is, so you can stay on one control, such as Time, and step through the taps to set each one.

Copy and paste act on the whole tap when focus is on the tap selector or the tap's on/off toggle, and on a single setting when focus is on that setting's slider. A tap can never be activated or deactivated as a result of a copy / paste operation.

### Pitch glitch modes

The pitch glitch has two modes, chosen for each tap from the context menu on its Pitch probability control. Sweep will keep going, and when it hits either end of its range, it'll bounce back and head the other way. Varyspeed doesn't actually shift the pitch, it speeds that tap's audio up or down like adjusting the speed on a tape machine. It's better for frozen loops because it doesn't grind them down as fast and it doesn't jump around. It also ignores the speed control.

-

### Freeze sustain

The **Freeze Sustain** toggle is beside Freeze in Global's Timing group and is off by default. Turn it on to keep frozen repeats audible when glitches wear them down. Each tap saves a protected copy of its loop and gradually blends some of it back as the processed audio loses level. Recovery can bring back the earlier sound of the loop.

### Output clipping

The output is hard clipped at +18 dBFS, so that glitches piling up in a frozen loop can't get loud enough for the host to mute the track. To clip at 0 dBFS instead, or to turn clipping off, open the context menu on Output gain. The setting is saved with your project.

### Performance area

The performance area is designed for expressive live performance. While the performance area has focus, a different set of keybinds is in effect and taps can be selected for alteration. By default, all taps are selected, but you can select even numbered, odd numbered, or select no taps at all in which case you can manually toggle the ones you want, then the up and down arrow keys adjust their time. What follows is the keybinds for the performance area.

| Action | Key |
| --- | --- |
| Add or remove tap 1 to 10 | 1 to 9, then 0 |
| Add or remove tap 11 to 16 | Shift+1 to Shift+6 |
| Select all taps, or none | Backspace |
| Select the even taps, or the odd ones | Shift+Backspace |
| Make the selected taps' times 10% longer or shorter | Up or Down arrow |
| Lower or raise the smear size by 10 ms | Left or Right arrow |
| Lower or raise the smear amount by 5% | Shift+Left or Shift+Right arrow |
| Freeze while held | F |
| Switch freeze on or off | Shift+F |
| Stop all glitches while held | G |
| Switch all glitches off or on | Shift+G |
| Stop the tape while held | T |
| Switch the tape stop on or off | Shift+T |

The arrows only move taps that are on, and move them by one note value while host sync is on. If any of them would pass its limit, none move.

Stopping the glitches affects every tap, whatever the selection. New glitches don't start and running ones fade out quickly, but glitched audio already going round a feedback loop stays in it. Pressing G after switching the glitches off with Shift+G switches them back on when you let go. The glitch stop can't be automated and isn't saved with your project.

### Tape stop

The tape stop slows the repeats to a halt, dropping in pitch like a tape machine being switched off, and brings them back up to speed when you turn it off. It works on the repeats only and leaves the dry signal alone, so turn Mix up to 100% for the full effect.

Hold T in the performance area to stop the tape for as long as the key is down, or press Shift+T to switch the stop on and leave it on. If you let go before the tape has stopped, it speeds back up from wherever it had got to. Pressing T after switching the stop on with Shift+T switches it off when you let go.

The **Tape stop** group in Global holds the same switch and two settings:

- **Stop time** is how long the tape takes to slow from full speed to a stop, from 50 ms to 2 s.
- **Start time** is how long it takes to get back up to full speed, from 50 ms to 2 s.

What you play while the tape is slowing is still recorded, and comes back once the tape is at speed. The slower the tape, the less of it is recorded. Audio already in the delay is unaffected. Everything in the delay holds where it stopped, including a frozen loop and any change you make to a tap, and carries on from there.

The switch and both times can be automated and MIDI learned. With a MIDI controller, the upper half of its range stops the tape and the lower half lets it go, which also releases a stop you switched on from the keyboard. The times are saved with presets and projects. The switch itself is never saved, so a project always opens with the tape running, and loading a preset leaves it as it is. Switching it can't be undone.

## Presets

The **Preset name** field in Main holds the name Save uses. A new instance starts with a randomly chosen name, such as "hollow-lantern". Type over it to use your own, or activate **Randomize** for another random one. Escape puts back the name the field had when you moved to it.

**Save** writes the preset under that name, with no dialog. If a preset of that name already exists it is replaced without asking, and Astralay says "Replaced" rather than "Saved". To keep the original and save a variation, change the name first. Characters that file names can't hold are left out of the name, and an empty name is refused.

**Load** opens a menu with a **Factory presets** submenu, a **User presets** submenu listing your presets alphabetically, and **Open presets folder**. The User presets submenu only appears once you have saved a preset. The preset the name field names is ticked. Loading a preset puts its name in the field, except for the factory preset Init, which gets a new random name.

User presets are saved in `Documents/Astralay/Presets` on both Windows and macOS, and that folder is the only place they are loaded from; folders inside it are ignored. A preset's name is its file's name, so use Open presets folder to rename or delete presets, or to add ones you've been given.

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

### macOS installer

After a build, `installer/mac/build-pkg.sh` packages all four formats into `build/installer/Astralay-<version>-macOS.pkg`. It takes the build folder and configuration as optional arguments (`build` and `Release` by default), and needs only the tools that come with macOS.

The installer lets you choose which formats to install and puts them where every user of the Mac can reach them, so it asks for an administrator password: the plugins go in the folders under `/Library/Audio/Plug-Ins` and the standalone in `/Applications`. It replaces any copy already there. It doesn't touch copies in your own `~/Library/Audio/Plug-Ins`; remove those so the host doesn't find two.

The installer isn't signed with a Developer ID or notarised, so macOS refuses to open a downloaded copy. Either run `xattr -c Astralay-<version>-macOS.pkg` first, or try to open it and then choose "Open Anyway" under Privacy & Security in System Settings. The plugins it installs need nothing further.

To uninstall, delete `Astralay.vst3`, `Astralay.component` and `Astralay.clap` from the folders under `/Library/Audio/Plug-Ins`, and `Astralay.app` from `/Applications`.

### Windows installer

After a build, `installer\windows\build-installer.ps1` packages the VST3, the CLAP and the standalone into `build\installer\Astralay-<version>-Windows.exe`. It takes the build folder and configuration as optional `-BuildDir` and `-Config` arguments (`build` and `Release` by default), and needs [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or later.

The installer lets you choose which formats to install and puts them where every user of the PC can reach them, so it asks for administrator rights: the VST3 goes in `C:\Program Files\Common Files\VST3`, the CLAP in `C:\Program Files\Common Files\CLAP`, and the standalone in `C:\Program Files\Astralay` with a Start menu shortcut. It replaces any copy already there.

The installer isn't code signed, so Windows SmartScreen warns about a downloaded copy. Choose "More info", then "Run anyway".

To uninstall, use "Installed apps" in Windows Settings.

### Releases

`scripts/release.ps1` cuts a release. It asks for the new version, offering the current one with its last number raised by one, writes it into `CMakeLists.txt`, commits that file alone, tags the commit `v<version>`, and pushes the branch and then the tag. It stops before changing anything if the tag already exists. To run it as `git release`, add the alias once in each clone:

```
git config alias.release '!powershell -NoProfile -ExecutionPolicy Bypass -File "$(git rev-parse --show-toplevel)/scripts/release.ps1"'
```

Pushing a tag that starts with `v` runs the workflow in `.github/workflows/release.yml`. It builds on macOS and Windows, then publishes a GitHub release for the tag with five files: the macOS installer, the Windows installer, and a ZIP of each plugin format for installing by hand, `Astralay-<version>-VST3.zip`, `Astralay-<version>-CLAP.zip` and `Astralay-<version>-AU.zip`. The VST3 and CLAP ZIPs hold the plugin for both systems. The tag has to match the version in `CMakeLists.txt` (`v0.1.0` for version 0.1.0), or the workflow stops before building.

The workflow can also be run by hand from the Actions tab. It then builds the same five files and keeps each as its own workflow artifact, without touching any release.

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

## Diagnostic logging

To track down a problem you can hear, Astralay can write a log of what it is doing: every parameter's value and each change to it, each glitch as it fires with the values it picked and the ranges set for them, and, every 50 ms, the freeze amount and signal levels of each sounding tap and of the output.

Logging is compiled into Debug builds. To add it to the other configurations too, which is useful when a Debug build is too slow to reproduce the problem, configure with `-DASTRALAY_DIAGNOSTICS=ON`:

```
cmake -S . -B build -DASTRALAY_DIAGNOSTICS=ON
cmake --build build --config Release --parallel
```

Even then nothing is written until the `ASTRALAY_LOG` environment variable is set to any value before the host starts. Each instance of the plugin then writes its own file to `Documents/Astralay/Logs`, named with the date and time. The top of each file explains its lines. Logs grow by several megabytes a minute while audio is playing, so unset the variable when you're done.

## Credits

Random preset names are made from the [EFF Large Wordlist for Passphrases](https://www.eff.org/dice) by the Electronic Frontier Foundation, used under a [Creative Commons Attribution licence](https://www.eff.org/copyright). The four entries that contain a hyphen are left out.

## Licence

Astralay's own source code is released under the MIT licence; see [LICENSE](LICENSE).

Astralay is built with [JUCE](https://juce.com), which it uses under the GNU Affero General Public License version 3 (AGPLv3). As a result, **distributed builds of Astralay (the plugins and the standalone application) are covered by the AGPLv3**, while the source code in this repository that isn't part of JUCE remains available under MIT. Anyone distributing builds must meet the AGPLv3's terms, including making the corresponding source available and including the AGPLv3 text, which is in [LICENSE-AGPL-3.0.txt](LICENSE-AGPL-3.0.txt).

Astralay also includes third-party code under permissive licences (PFFFT, the VST3 SDK, CLAP, clap-helpers and clap-juce-extensions). Their notices, which must accompany any release, are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
