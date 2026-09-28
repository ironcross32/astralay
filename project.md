# Astralay

## Premise

Astralay is a delay plugin combined with a glitch generator. It will support the following formats:
- VST3
- CLAP
- AU
- Standalone application (for testing and use without a DAW)

Screen-reader accessibility is a first class citizen, remember that when implementing the interface. The framework to be used is JUCE.

This plugin is meant to be cross-platform. Version 1 targets Windows and macOS; code must be written so Linux (VST3 and CLAP) can be added later without restructuring.

## Build, dependencies and licensing

- Build system: CMake (not Projucer). macOS builds are universal binaries (Apple Silicon and Intel), with a minimum of macOS 10.13.
- JUCE is included as a git submodule of this repository at `libs/JUCE`, pinned to the release tag 9.0.3. It is never installed or referenced from outside the repository.
- CLAP: JUCE 9.0.3 has no native CLAP support, so CLAP builds use `clap-juce-extensions`, a second git submodule at `libs/clap-juce-extensions`. It has nested submodules of its own, so clone with `git clone --recursive` or run `git submodule update --init --recursive`. Revisit native CLAP if a later JUCE release adds it.
- License: Astralay's own source is MIT. Release builds link JUCE under the AGPLv3, so distributed binaries are AGPLv3; the README must explain this.
- The Surge XT code base at D:\programming\surge is an excellent example of good JUCE accessibility. Learn from it, but do not copy code from it (Surge is GPL-3).
- Every build must pass pluginval at a strict level.
- Automated tests cover the DSP pieces: taps, the glitch scheduler, and seeded determinism.

## Signal flow

- Input is summed to mono going into each tap. Supported layouts are mono in / stereo out and stereo in / stereo out.
- Each tap has its own delay line and its own feedback loop.
- Each tap's feedback path contains, in order: the active glitches, a low cut filter, a high cut filter, and an always-on gentle soft-clipper with no user controls.
- Each tap's output is scaled by its volume and panned into stereo.
- The summed wet output of all taps passes through smear, then is mixed with the dry signal, then output gain is applied.

## Taps

- There are 16 taps. Taps are fixed slots that can either be on or off. This is deliberate, to prevent having to renumber a deleted tap and to keep host automation stable.
- A new instance has only tap 1 enabled. All taps off is allowed (dry signal only).
- When selecting a tap, the tap-specific controls update dynamically to show only the values for the selected tap. The goal is to avoid cluttering the interface.

Per-tap controls, with ranges and defaults:
- Enabled: on/off. Default on for tap 1, off for all others.
- Time: 1 ms to 5 s, default 500 ms. When host sync is on: 1/64 to 4 bars (straight, dotted and triplet), default 1/4. The millisecond value and the synced value are stored as separate parameters, so toggling sync never destroys either.
- Volume: dB, down to -inf. Default 0 dB.
- Pan: 100L to 100R, constant-power pan law. Default centre.
- Feedback: 0% to 100%, where 100% is unity gain. No values above 100%. Default 40%.
- Low cut (in the feedback path): 20 Hz to 2 kHz, 12 dB per octave. Default 20 Hz (effectively off).
- High cut (in the feedback path): 1 kHz to 20 kHz, 12 dB per octave. Default 20 kHz.

When a tap's time changes while audio is playing (automation, tempo change, user edit), the delay glides to the new time, tape style. The glide rate is set by the global glide time control.

## Glitches

### How glitches fire

- Glitches apply within each tap's feedback path, so they compound over repeats.
- Audio is divided into chunks of the global buffer size. When host sync is on, the chunk grid is aligned to the host's bar and beat position; otherwise it is free-running from when the plugin started processing.
- At each chunk boundary, each glitch type on each enabled tap rolls to fire. Its chance is the tap's probability for that glitch multiplied by the global glitch threshold. A global threshold of 0% disables all glitching.
- A glitch that fires runs for a random number of chunks between the global minimum and maximum glitch length.
- Multiple glitches can trigger and run simultaneously, up to the global maximum simultaneous glitches per tap.
- When several glitches run at once on a tap, they are processed in series in this fixed order: reverse, stutter, granularize, pitch, LPC formant, cepstral formant, ring modulation, frequency modulation, bit crusher.
- Effects that need a window of audio (granularize, LPC formant, cepstral formant) read the delay line's history when a tap is shorter than their window. No latency is reported to the host.

### Minimum and maximum ranges

Where a glitch type has a range, the plugin randomly selects a value from it each time the glitch fires. If a minimum is set above its maximum, the two are swapped when the glitch fires; the controls never push each other.

### Glitch types

Every glitch type has a probability control, 0% to 100%, default 0%. Additional controls per type, as full range (default minimum to default maximum):

- Reverse: reverses the buffer for the length of the glitch. No additional controls.
- Stutter: repeats a slice for the length of the glitch. Slice length 5 ms to 250 ms, or a note value when synced (default 20 ms to 120 ms).
- Granularize: grain size 5 ms to 200 ms (default 20 ms to 80 ms); density 1 to 100 grains per second (default 10 to 40).
- Pitch: -24 to +24 semitones (default -12 to +12).
- LPC formant shifting: -12 to +12 semitones (default -5 to +5).
- Cepstral formant shifting: -12 to +12 semitones (default -5 to +5).
- Ring modulation: frequency 1 Hz to 5 kHz (default 30 Hz to 800 Hz).
- Frequency modulation: ratio 0.25 to 16 (default 0.5 to 3); index 0 to 10 (default 0.5 to 4).
- Bit crusher: bit depth 1 to 16 (default 4 to 10); sample-rate reduction 1x to 64x (default 1x to 8x).

### Randomness

- A reproducible randomness toggle (default off) and a seed (integer, 0 to 9999) are global controls.
- When the toggle is on, the random sequence restarts from the seed every time the host transport starts, so every play-through and bounce glitches identically.
- When the toggle is off, the seed has no effect, and its help tag says so.

## Global controls

Timing:
- Host sync: when on, time values are expressed as note values and synced with the host's tempo. If the host provides no tempo, 120 BPM is used.
- Glide time: 0 s to 2 s, default 100 ms.
- Freeze: freezes the delay in place so repetitions do not decrease in volume. While frozen, new input into the delay lines is muted (the dry signal still passes), glitches keep firing, and each tap's filters and soft-clipper are bypassed so the loop does not dull or thin. Freeze engages and releases with a short crossfade.

Glitch engine:
- Glitch threshold: 0% to 100%, default 20%.
- Buffer size: 10 ms to 2 s, default 125 ms. When synced: 1/64 to 4 bars, default 1/16.
- Maximum simultaneous glitches: 1 to 4 per tap, default 2.
- Minimum glitch length: 1 to 16 chunks, default 1.
- Maximum glitch length: 1 to 16 chunks, default 4.
- Reproducible randomness: on/off, default off.
- Seed: 0 to 9999.

Output:
- Smear amount: 0% to 100%, default 10%. Applies smearing and diffusion to the summed wet output of all taps.
- Smear size: 10 ms to 500 ms, default 200 ms.
- Mix: 0% to 100% wet, equal-power crossfade, default 50%.
- Output gain: dB, default -3 dB.

## Presets and state

- Factory presets are built into the plugin binary. User presets are saved to disk. The two are never written to the same place.
- User presets are stored in `%userprofile%\Documents\Astralay\Presets` on Windows and `~/Documents/Astralay/Presets` on macOS.
- Preset files are XML with a version attribute and the extension `.astralay`. When loading a preset from an older version, any missing parameter takes its default value.
- Save opens a native OS save dialog in the user preset folder, suggesting the current preset name.
- Load opens a menu containing a "Factory presets" submenu and a "From file…" item, which opens a native OS open dialog in the user preset folder.
- A new instance's preset name is "Init". When the current preset has been changed, the name label reads "<name>, modified" (a word rather than an asterisk, which screen readers often skip).
- The host session state stores all parameters plus the currently selected tap.

### Undo

- Undo covers parameter changes, tap enable/disable, and preset loads. Changing the selected tap is not undoable.
- One slider gesture is one undo step. A run of arrow-key presses joins into one step after a short pause.

## Automation

- Every parameter is automatable except the tap selector, which is UI state.
- Parameter names must be properly labelled and reflect exactly what they change, including the tap number, for example "Tap 3 Feedback" or "Tap 3 Stutter Probability".

## UI

### Visual style

The window is resizable. The visual style is plain, as the plugin will mostly be used by blind individuals, but it uses a high-contrast colour scheme (light text on dark) with large, clear labels, and text scales with the window size for low-vision users.

### Grouping

It is imperative to group controls based on their function. This is vital for proper VoiceOver accessibility in macOS, as users will interact with groupings to access the controls inside. Without this, the interface appears disorganized and cluttered. It is also necessary to use JUCE-specific features to set accessibility info so help tags on VoiceOver work. Think of help tags as the screen-reader equivalent of tooltips.

There are three top-level groups, in this order:

1. Main: Undo, Redo, Save, Load, then a read-only label showing the current preset name.
2. Tap group, named after the selected tap (for example "Tap 3"):
    - Tap selector, a dropdown whose items show each tap's state (for example "Tap 3, on")
    - Enabled
    - Time
    - Volume
    - Pan
    - Feedback
    - Low cut
    - High cut
    - Nine nested glitch sub-groups, named after each glitch type, in processing order: Reverse, Stutter, Granularize, Pitch, LPC formant, Cepstral formant, Ring modulation, Frequency modulation, Bit crusher. Inside each, the probability comes first, followed by the range controls, each minimum before its maximum.
3. Global, with three nested sub-groups:
    - Timing: host sync, glide time, freeze
    - Glitch engine: glitch threshold, buffer size, maximum simultaneous glitches, minimum glitch length, maximum glitch length, reproducible randomness, seed
    - Output: smear amount, smear size, mix, output gain

Every control in the tap group includes the tap number in its accessible name (for example "Tap 3 Feedback"), so each control identifies itself without relying on announcements.

### Help tags

Every control has a help tag of one or two plain sentences describing what it does and its range. Claude drafts them, kept together in one table in the code so they are easy to edit. They are reviewed by the author before release.

### Keyboard

Keyboard shortcuts, Windows / macOS:
- Next group: ALT+. / CMD+. (fall back to OPTION only if hosts turn out to swallow CMD)
- Previous group: ALT+, / CMD+,
- Undo: CTRL+Z / CMD+Z
- Redo: CTRL+SHIFT+Z / CMD+SHIFT+Z
- Save: CTRL+S / CMD+S
- Load: CTRL+O / CMD+O

Previous and next group controls don't actually place focus on the group itself, but the first control inside said group. They move between the three top-level groups only, and wrap around, as should using the TAB key. TAB is not constrained by grouping, and navigates the interface in a flat manner.

Sliders:
- All controls that increase and decrease a value should be sliders.
- HOME and END jump to the minimum and maximum respectively.
- Arrow keys increase and decrease by a musical step suited to the value's unit. Adding SHIFT gives a fine adjustment; adding CTRL (Windows) or CMD (macOS) gives a coarse adjustment.
    - dB: 0.5 dB, fine 0.1 dB, coarse 3 dB
    - Milliseconds: 10 ms, fine 1 ms, coarse 100 ms
    - Percent: 1%, fine 0.1%, coarse 10%
    - Semitones: 1, fine 0.1, coarse 12
    - Note values: one step through the list; no fine step; coarse jumps between straight values only
- DELETE (BACKSPACE on macOS) sets the slider to its default value.
- ENTER presents a type-in field. It accepts plain numbers in the control's own unit, optional unit suffixes (ms, s, Hz, kHz, dB, %, st), "-inf" for volume, and note values such as "1/8", "1/8d", "1/8t", "1/64" and "4 bars". ENTER accepts the value if it is valid and in range. An out-of-range value is rejected: the screen reader announces the valid range (for example "Out of range, 1 to 5000 milliseconds") and the field stays open. ESC cancels.
