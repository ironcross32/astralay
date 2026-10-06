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
- Formant glitches briefly keep the original signal while preparing, then fade in over 5 ms. Preparation takes up to about 6 ms at common sample rates, spreading processing work to reduce CPU spikes. Very short formant glitches can sound weaker.
- Reproducible randomness: playback starts restart the glitch sequence from your seed. Seeks and loop wraps also restart it when the host supplies sample positions. A host that skips stopped callbacks must report a position jump or prepare the plugin again for a restart to be detected.
- Host sync, tape-style glide when times change, freeze, smear (diffusion), dry/wet mix, output gain and an output clip.
- Eight macros, each moving any number of controls at once from a single value you can automate.
- MIDI CC and pitch-bend learn, with reusable mappings and project-state recall.
- Presets, factory presets, and undo and redo.

## Usage

Controls are arranged in five groups: Main, Tap *number*, Macros, Global, and Performance. Main houses things like undo and redo, and preset management. The Tap *number* group contains a tap selector, as well as every control for the selected tap. The group is renamed so that *number* represents what tap you're working with. For example, "Tap 3". Macros holds the eight macros. Everything that affects the sound as a whole lives in global.

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

Copy and paste act on the whole tap when focus is on the tap selector or the tap's on/off toggle, and on a single setting when focus is on that setting's slider. Whether a tap is on is never copied.

### Pitch glitch modes

The pitch glitch has two modes, chosen for each tap from the context menu on its Pitch probability control.

- **Sweep** shifts the audio on every pass through the tap, so with feedback, and most of all while frozen, the pitch keeps moving for as long as the glitch lasts. Min speed and Max speed set how far each pass moves it. Minimum and Maximum are the limits: a sweep that reaches one turns back, and sweeps then tend to head for the middle of the range until the pitch is back there. Sweeps wear a frozen loop down over time; lower the probability or the speed, or narrow the range, to slow that.
- **Varispeed** changes the tap's time instead, like changing a tape's speed. The pitch bends while the time glides to its new value, at the Glide time, and bends back when the glitch ends. Minimum and Maximum set the speed change; the speed controls aren't used.

### Freeze sustain

The **Freeze Sustain** toggle is beside Freeze in Global's Timing group and is off by default. Turn it on to keep frozen repeats audible when glitches wear them down. Each tap saves a protected copy of its loop and gradually blends some of it back as the processed audio loses level. Recovery can bring back the earlier sound of the loop.

Capturing takes one trip around each tap. If you turn sustain on during a freeze, it saves what remains then; it cannot recover a loop that has already gone silent. Turning sustain off fades recovery out, and releasing freeze or disabling a tap discards its recording. The setting is saved with presets and projects and can be automated.

### Macros

A macro is one value that moves several controls at once. There are eight, each in its own group inside the Macros group, with an Arm button and a value slider. The value appears to your host as a parameter, so you can automate it or map it to a knob.

To set up what a macro moves:

1. Press its Arm button. The button now reads Disarm.
2. Go to any control you want the macro to move and adjust it. While a macro is armed, a slider sets how far the macro moves that control, instead of changing the control itself. The amount is in the control's own units: 200 ms on a time makes it 200 ms longer, and -6 dB on a volume makes it 6 dB quieter. The slider keys all work as usual: Home and End go to the largest amounts up and down, Enter lets you type one, and Delete sets it to 0, which removes it.
3. Set up as many controls as you like, on any tap, then press Disarm.

A time has one amount, which applies whether or not host sync is on. While host sync is on, the time controls are note values and show that amount as a percentage of the range instead, such as 40%; the note value moves by that share of the list and always lands on a whole note value.

You can also arm a macro without leaving the control you're on, anywhere outside the performance area. Press Alt+M (Cmd+M on macOS), wait for "Arm?", then press the macro's number, 1 to 8. Pressing the number of the macro that is already armed disarms it, and 0 disarms whichever one is armed. You have two seconds to press the number; any other key cancels.

Arming a second macro disarms the first. Now moving the macro's value slider moves everything you set up: at a value of 1, each control has moved by the full amount you gave it, and at 0 not at all. The controls themselves keep the values you set; the macro's movement is added on top.

Macros can move every slider except the ones in the Glitch engine group, Output gain, and the macros' own values.

Each macro's group has a context menu, which opens from either of its controls:

- **Rename** gives the macro a name of your own, which your host shows too.
- **Modulations** lists every control the macro moves, with its amount. Each has **Edit**, to type a new amount, and **Clear**, to remove it. The item is missing when the macro moves nothing.
- **Bipolar** makes the macro's value run from -1 to 1 instead of 0 to 1, so it can move its controls in both directions from where they sit.

Macro names, modes and modulations are saved with your project and in presets, and changes to them can be undone.

### Output clipping

The output is hard clipped at +18 dBFS, so that glitches piling up in a frozen loop can't get loud enough for the host to mute the track. To clip at 0 dBFS instead, or to turn clipping off, open the context menu on Output gain. The setting is saved with your project, but it can't be automated and loading a preset doesn't change it.

### Performance area

The Performance group holds a single control, the performance area. While it has focus, the keys below replace the tap keys, copy and paste, Alt+F and Alt+M. They act on a selection of taps that starts as all of them and is separate from the selected tap.

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

The arrows only move taps that are on, and move them by one note value while host sync is on. If any of them would pass its limit, none move. The arrows and the held freeze are not announced.

## Presets

User presets are saved in `Documents/Astralay/Presets` on both Windows and macOS. Access factory presets by pressing "Load", then expanding the "Factory presets" entry.

## MIDI

Focus a sound control and press Alt+L on Windows or Cmd+L on macOS, then move a MIDI controller. Alternatively, activate **MIDI learn** in Main, click a sound control or press Enter on it, then move the controller. The selection click or Enter is consumed. A control's context menu also offers MIDI learn and, when bound, Remove MIDI mapping. Escape, the shortcut, or the learn button cancels learning. Learning disarms an armed macro and has no timeout while the editor stays open.

Each binding remembers the exact channel and CC number, or channel and pitch bend. All CC numbers 0–127 are independent absolute values; relative encoders, CC pairs, RPN and NRPN aren't decoded. One source can drive several controls, but each control has one source. The event that completes learning changes no sound values. Later events move immediately through the control's full slider range, including its curve and stepping. Pitch bend's centre is the exact midpoint. Switches use the lower half for Off and the upper half for On. Existing DSP smoothing still applies; MIDI adds none.

Tap bindings stay on the captured tap, and macro bindings stay on their slot. Time, stutter slice limits and buffer size follow host sync between milliseconds and note values. Pitch Mode, clipping, navigation, macro arms, modulation amounts and the performance pad cannot be learned. MIDI writes the stored sound value, and existing macro modulation applies afterward. Learning a macro value lets its existing routes and amounts shape travel for parameters supported by macros.

**Main menu → MIDI** holds Save, Save As, default and clear operations, followed by valid mapping files sorted by name. Files live in `Documents/Astralay/MIDI Mappings`. Names are typed into an accessible field and must be portable between Windows and macOS. Saving uses `.json` and confirms replacement of another file or an externally changed file. Loading a mapping with unsaved edits offers Save, Discard or Cancel. Clear mapping asks once and discards the current bindings; it leaves files and the default unchanged.

Mapping edits are undoable independently of sound edits. Incoming MIDI movement does not enter Astralay's undo history. Projects embed their complete mapping, file association and unsaved status; reopening or duplicating an instance doesn't depend on the mapping file. Loading a sound preset leaves bindings alone. Mapping files contain no sound values or macro configuration. Empty mappings cannot be saved as files.

Set current mapping as default saves a nonempty mapping first when necessary, then references its file for new instances. Making an empty mapping the default, or clearing the default, makes new instances start empty. Existing instances keep their mappings. Restored projects, including projects predating MIDI support, take precedence over defaults. A missing or invalid default starts empty and exposes a status in the MIDI menu.

### Audio and MIDI routing

Astralay remains an audio effect with stereo output and stereo or mono input. Hosted builds receive MIDI from the DAW; the standalone's audio/MIDI settings select MIDI input devices. Astralay leaves incoming MIDI messages and their sample offsets unchanged in its processing buffer, including the learn event. Actual MIDI input/output routing and automation recording depend on the host and format; VST3 represents controller input through its MIDI-controller parameter interface, and hosts may filter or consume messages before delivery. AU is configured as a MIDI-capable music effect. Astralay generates no MIDI.

In Reaper, use either of these arrangements:

1. Put Astralay on a track receiving MIDI, enable the track's MIDI input/monitoring, and also feed audio to that track's first channel pair (for example from another track).
2. Put Astralay on an audio track and send MIDI from another track, enabling MIDI in the send while disabling that send's audio if it isn't needed.

Keep the effect's audio pins on the first stereo pair; mono input is also supported. MIDI output must be routed onward by the host if needed. Enable the host's appropriate automation recording mode to record parameter notifications. MIDI and host automation write the same stored values; the last applied update wins.

These routing instructions require manual verification. Automated Windows processor/editor tests and build results are recorded in [MIDI verification](MIDI_verification.md); no Reaper, other-host, screen-reader or macOS/AU verification is implied by those tests.

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

## Diagnostic logging

To track down a problem you can hear, Astralay can write a log of what it is doing: every parameter's value and each change to it, each glitch as it fires with the values it picked and the ranges set for them, and, every 50 ms, the freeze amount and signal levels of each sounding tap and of the output.

Logging is compiled into Debug builds. To add it to the other configurations too, which is useful when a Debug build is too slow to reproduce the problem, configure with `-DASTRALAY_DIAGNOSTICS=ON`:

```
cmake -S . -B build -DASTRALAY_DIAGNOSTICS=ON
cmake --build build --config Release --parallel
```

Even then nothing is written until the `ASTRALAY_LOG` environment variable is set to any value before the host starts. Each instance of the plugin then writes its own file to `Documents/Astralay/Logs`, named with the date and time. The top of each file explains its lines. Logs grow by several megabytes a minute while audio is playing, so unset the variable when you're done.

## Licence

Astralay's own source code is released under the MIT licence; see [LICENSE](LICENSE).

Astralay is built with [JUCE](https://juce.com), which it uses under the GNU Affero General Public License version 3 (AGPLv3). As a result, **distributed builds of Astralay (the plugins and the standalone application) are covered by the AGPLv3**, while the source code in this repository that isn't part of JUCE remains available under MIT. Anyone distributing builds must meet the AGPLv3's terms, including making the corresponding source available and including the AGPLv3 text, which is in [LICENSE-AGPL-3.0.txt](LICENSE-AGPL-3.0.txt).

Astralay also includes third-party code under permissive licences (PFFFT, the VST3 SDK, CLAP, clap-helpers and clap-juce-extensions). Their notices, which must accompany any release, are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
