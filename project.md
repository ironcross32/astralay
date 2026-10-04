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

- Build system: CMake (not Projucer). macOS builds are universal binaries (Apple Silicon and Intel), with a minimum of macOS 12.0 (the oldest version Xcode 27 supports). Every macOS bundle is ad-hoc signed as the last post-build step, which Apple Silicon requires for local builds; distributed builds need Developer ID signing and notarisation.
- JUCE is included as a git submodule of this repository at `libs/JUCE`, pinned to the release tag 9.0.3. It is never installed or referenced from outside the repository.
- CLAP: JUCE 9.0.3 has no native CLAP support, so CLAP builds use `clap-juce-extensions`, a second git submodule at `libs/clap-juce-extensions`. It has nested submodules of its own, so clone with `git clone --recursive` or run `git submodule update --init --recursive`. Revisit native CLAP if a later JUCE release adds it.
- FFTs use PFFFT (git submodule at `libs/pffft`, built as a static library), since JUCE's fallback FFT on Windows is roughly 19 times slower. PFFFT's BSD-style licence requires its copyright notice in the documentation of binary releases.
- License: Astralay's own source is MIT. Release builds link JUCE under the AGPLv3, so distributed binaries are AGPLv3; the README must explain this.
- The Surge XT code base at D:\programming\surge is an excellent example of good JUCE accessibility. Learn from it, but do not copy code from it (Surge is GPL-3).
- Every build must pass pluginval at strictness level 10. (pluginval checks VST3 and AU; it doesn't support CLAP. Steinberg's separate VST3 validator is optional and not currently run.)
- Release documentation: the README explains building and licensing, and `THIRD_PARTY_NOTICES.md` carries the notices the third-party licences require. Binary releases must include both, plus the AGPLv3 text.
- Automated tests cover the DSP pieces: taps, the glitch scheduler, and seeded determinism.
- Diagnostic logging, for problems found by ear. Compiled into Debug builds, and into every configuration with the CMake option `ASTRALAY_DIAGNOSTICS`; in other builds the probes are empty and cost nothing. It writes only when the `ASTRALAY_LOG` environment variable is set as the plugin loads, one text file per instance in `Documents/Astralay/Logs`. It records every parameter's value and full range at the start and each change after; each glitch as it fires, with the tap, the values picked, the ranges set for them, its length and the freeze amount; and every 50 ms, for each sounding tap, the freeze amount, delay, feedback, the peak levels read from the delay line, after the glitches and written back to it, the peak after each glitch that ran, the estimate of the loop's pitch, and a count of samples that were not finite; and likewise for the output, its peak before the output clip and how many samples the clip caught. The audio thread passes events through a lock-free queue to a background thread that writes the file.

## Signal flow

- Input is summed to mono going into each tap. Supported layouts are mono in / stereo out and stereo in / stereo out.
- Each tap has its own delay line and its own feedback loop.
- Each tap's feedback path contains, in order: the active glitches (when glitch placement is "Feedback path"), a low cut filter, a high cut filter, and an always-on gentle soft-clipper with no user controls.
- The maximum delay is 10 seconds. Synced times longer than that at slow tempos are clamped to 10 seconds.
- A delay of a whole number of samples is read back untouched. Any other is interpolated with a 16-point windowed sinc (delays under 8 samples, which no tap time reaches, fall back to 4 points). A short feedback loop reads its own output back a hundred or more times a second, so the earlier 4-point interpolation lost treble on every pass and a frozen loop went dull within seconds.
- A loop that keeps everything (frozen, or 100% feedback) settles on the nearest whole number of samples once its time stops gliding, so that it is read back untouched. This moves the delay by at most half a sample.
- Each tap's output is scaled by its volume and panned into stereo.
- The summed wet output of all taps passes through smear, then is mixed with the dry signal, then output gain is applied, then the output clip.

## Taps

- There are 16 taps. Taps are fixed slots that can either be on or off. This is deliberate, to prevent having to renumber a deleted tap and to keep host automation stable.
- A new instance has only tap 1 enabled. All taps off is allowed (dry signal only).
- Disabling a tap fades it out and then clears its delay line, so re-enabling it starts silent rather than replaying old audio.
- When selecting a tap, the tap-specific controls update dynamically to show only the values for the selected tap. The goal is to avoid cluttering the interface.

Per-tap controls, with ranges and defaults:
- Enabled: on/off. Default on for tap 1, off for all others.
- Time: 1 ms to 5 s, default 500 ms. When host sync is on: 1/64 triplet to 4 bars (straight, dotted and triplet), default 1/4. Bar lengths follow the host's time signature. The millisecond value and the synced value are stored as separate parameters, so toggling sync never destroys either.
- Volume: -inf to +6 dB (the bottom of the range, -60 dB, means silence). Default 0 dB.
- Pan: 100L to 100R, constant-power pan law. Default centre.
- Feedback: 0% to 100%, where 100% is unity gain. No values above 100%. Default 40%.
- Low cut (in the feedback path): 20 Hz to 2 kHz, 12 dB per octave. Default 20 Hz (effectively off).
- High cut (in the feedback path): 1 kHz to 20 kHz, 12 dB per octave. Default 20 kHz.

When a tap's time changes while audio is playing (automation, tempo change, user edit), the delay glides to the new time, tape style. The glide rate is set by the global glide time control.

## Glitches

### How glitches fire

- Glitches compound over repeats. Where they apply is set by the global glitch placement control:
    - Feedback path (default): glitches apply only to the signal fed back into the delay line, so the first repeat is clean and glitches are not heard at 0% feedback.
    - Output and feedback: glitches apply to the delayed signal before it splits to the tap's output and its feedback, so they are heard from the first repeat even at 0% feedback.
- Audio is divided into chunks of the global buffer size. When host sync is on, the chunk grid is aligned to the host's bar and beat position; otherwise it is free-running from when the plugin started processing.
- At each chunk boundary, each glitch type on each enabled tap rolls to fire. Its chance is the tap's probability for that glitch multiplied by the global glitch threshold. A global threshold of 0% disables all glitching.
- A glitch that fires runs for a random number of chunks between the global minimum and maximum glitch length.
- Multiple glitches can trigger and run simultaneously, up to the global maximum simultaneous glitches per tap. A glitch type that is already running on a tap doesn't retrigger.
- Glitches fade in and out over 5 ms to avoid clicks.
- When several glitches run at once on a tap, they are processed in series in this fixed order: reverse, stutter, granularize, pitch, LPC formant, cepstral formant, ring modulation, frequency modulation, bit crusher.
- Effects that need a window of audio (granularize, LPC formant, cepstral formant) read the delay line's history when a tap is shorter than their window. No latency is reported to the host.

### Minimum and maximum ranges

Where a glitch type has a range, the plugin randomly selects a value from it each time the glitch fires. If a minimum is set above its maximum, the two are swapped when the glitch fires; the controls never push each other.

### Glitch types

Every glitch type has a probability control, 0% to 100%, default 0%. Additional controls per type, as full range (default minimum to default maximum):

- Reverse: plays in reverse for the length of the glitch. Each chunk-long segment (up to 2 seconds) plays the segment before it backwards. No additional controls.
- Stutter: captures a slice (up to 2 seconds) when the glitch starts and repeats it for the length of the glitch. Slice length 5 ms to 250 ms (default 20 ms to 120 ms), or when synced 1/64 triplet to 1/4 (default 1/32 to 1/8).
- Granularize: grain size 5 ms to 200 ms (default 20 ms to 80 ms); density 1 to 100 grains per second (default 10 to 40). Grains are Hann-windowed copies of audio from up to half a second back, spaced with random jitter.
- Pitch: a range of -24 to +24 semitones (default -12 to +12), a speed of 0 to 24 semitones per pass (default 0 to 12), and a mode, "Sweep" (default) or "Varispeed". The mode is a parameter of each tap ("Tap 3 Pitch Mode") with no control of its own; it is set from the pitch probability control's context menu.
    - Sweep shifts the audio by a step on every pass through the tap. In a feedback loop the shifter meets the same audio again on every trip round, so the pitch keeps moving for as long as the glitch runs; that movement is the sweep. Without feedback, or on a tap longer than the glitch, the audio passes once and is simply shifted by the step.
    - The step is picked from the speed range each time the glitch fires. The speed is nominal: the shifter's two read heads trail up to a window (about 40 ms) behind, so on a loop shorter than that the audio moves by about half the step per trip.
    - The range is the pair of walls the pitch stays between. The plugin keeps an estimate of how far each tap's looped audio sits from its original pitch. The estimate travels with the audio: each sample's offset is stored beside it, through the shifter and round the delay line, and feedback pulls it back towards zero as new input replaces the loop. It does not see pitch changes from other sources, such as a tap's time being moved while frozen.
    - A step that would carry the audio through a wall is cut short so that it lands on the wall, and when that audio comes round again the sweep turns back and carries on the other way. So no sweep goes outside the range, at any speed.
    - Direction when a glitch fires: back into the range if the pitch is outside it; otherwise up or down with equal chance, unless homing is on. Reaching a wall turns homing on. While it is on, the chance of heading for the middle of the range is 90% at a wall, falling evenly to 50% at the edge of a dead zone, the middle fifth of the range; entering the dead zone turns homing off.
    - Each pass through a loop of fixed length discards or repeats some of its audio, so a frozen loop is worn down by sweeps and does not come back. This is accepted; a lower probability, a slower speed or a narrower range slows it.
    - The shifter's crossfades, between its two read heads and at the start and end of the glitch, keep their level: the mix is scaled up by as much as a plain crossfade would lose, judged by how alike the two signals are (nothing for identical audio, 3 dB at most), around the signal's slow-moving mean. An equal-power crossfade instead adds level when the two are alike, as they are in a repeating loop, and a frozen loop then climbs until it clips. Steering a stage's output level by its input level was tried and is unstable in a feedback loop.
    - In a loop of 40 ms or less the shifter's window is fitted to twice a whole number of loop lengths.
    - Varispeed leaves the audio alone and changes the tap's delay time for the length of the glitch, as changing a tape's speed would: a value picked from the range is the speed change, so +12 semitones halves the delay time. The tap glides there at the global glide time and glides back when the glitch ends, and the pitch bends only while it is gliding. It does not use the speed range. It discards or repeats audio as the loop shortens and lengthens, and while the time glides the loop is read between samples, which costs a little of the very top on each pass; speeding up also pushes the highest frequencies past what the sample rate can hold. Otherwise the audio is untouched, and the tap returns to its own time when the glitch ends.
- LPC formant shifting: -12 to +12 semitones (default -5 to +5).
- Cepstral formant shifting: -12 to +12 semitones (default -5 to +5).
- Both formant shifters keep each frame at the level it came in with, and their fades in and out keep their level as the pitch glitch's do. Most sound falls away towards the top, so moving its formants up raises every band a little and moving them down lowers every band; in a feedback loop that happens on every pass, and a frozen tap climbed by tens of decibels a second or faded to nothing. Formant shifts still compound in a loop (the tone keeps moving while a glitch runs) and a frozen loop still loses a little level under constant shifting.
- Both formant shifters analyse about 21 ms frames at 4x overlap, estimate each frame's spectral envelope, and replace it with a copy stretched by the shift, keeping the pitch. LPC estimates the envelope by linear prediction (broader, grittier); cepstral by smoothing the log spectrum (sharper, smoother). Their output lags by one frame, and they analyse recent history when they start, so there is no silent gap.
- Ring modulation: frequency 1 Hz to 5 kHz (default 30 Hz to 800 Hz).
- Frequency modulation: ratio 0.25 to 16 (default 0.5 to 3); index 0 to 10 (default 0.5 to 4). The audio is phase modulated by a sine at the ratio times the audio's estimated fundamental (from its zero crossings when the glitch starts), with the index as the phase deviation.
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
- Glitch placement: "Feedback path" or "Output and feedback", default "Feedback path".
- Buffer size: 10 ms to 2 s, default 125 ms. When synced: 1/64 to 4 bars, default 1/16.
- Maximum simultaneous glitches: 1 to 4 per tap, default 2.
- Minimum glitch length: 1 to 16 chunks, default 1.
- Maximum glitch length: 1 to 16 chunks, default 4.
- Reproducible randomness: on/off, default off.
- Seed: 0 to 9999.

Output:
- Smear amount: 0% to 100%, default 10%. Applies smearing and diffusion to the summed wet output of all taps. Smear is a cascade of six allpass diffusers per channel (whole-sample delays that add up to the smear size, slightly different on the left and right for stereo spread), so it spreads sound out in time without changing its tone or level. The amount is an equal-power crossfade between the unsmeared and smeared signal; 0% is an exact bypass. Size changes glide over 100 ms.
- Smear size: 10 ms to 500 ms, default 200 ms.
- Mix: 0% to 100% wet, equal-power crossfade, default 50%.
- Output gain: -24 dB to +12 dB, default -3 dB.
- Output clip: "+18 dBFS" (default), "0 dBFS" or "Off". A hard clip on the final output, after output gain, so that glitches piling up in a frozen loop cannot push the level to where a host mutes the track (Reaper mutes above +18 dBFS). The +18 dBFS ceiling sits about 0.001 dB under +18, so rounding cannot carry a clipped peak over that limit. It is not a parameter: it cannot be automated, is not part of a preset (loading one leaves it alone) and is not undoable. It is saved with the host session, per plugin instance. It has no control of its own: it is set from the output gain control's context menu.

## Presets and state

- Factory presets are built into the plugin binary. User presets are saved to disk. The two are never written to the same place.
- Factory presets are defined in code (`source/state/Presets.cpp`) as settings that differ from the defaults, written as the text a user would type ("250 ms", "1/8 dotted"). The starter set: Init, Slapback, Ping pong, Rhythmic scatter, Frozen grains, Robot choir.
- Preset files hold every parameter's value in its own units, keyed by parameter ID. Parameters the file doesn't contain take their defaults; parameters the plugin doesn't know are ignored. A loaded preset takes its name from the file name.
- Saving, loading and errors are announced ("Saved My Preset", "Loaded Slapback", "Could not load ... It isn't an Astralay preset.").
- User presets are stored in `%userprofile%\Documents\Astralay\Presets` on Windows and `~/Documents/Astralay/Presets` on macOS.
- Preset files are XML with a version attribute and the extension `.astralay`. When loading a preset from an older version, any missing parameter takes its default value.
- Save opens a native OS save dialog in the user preset folder, suggesting the current preset name.
- Load opens a menu containing a "Factory presets" submenu and a "From file…" item, which opens a native OS open dialog in the user preset folder.
- A new instance's preset name is "Init". When the current preset has been changed, the name label reads "<name>, modified" (a word rather than an asterisk, which screen readers often skip).
- The host session state stores all parameters plus the currently selected tap and the output clip setting.

### Undo

- Undo covers parameter changes, tap enable/disable, pastes, performance-area time and smear changes, and preset loads. Changing the selected tap, the performance selection and a held freeze are not undoable.
- One slider gesture is one undo step. A run of edits to the same parameter less than 600 ms apart (such as repeated arrow presses) joins into one step.
- Only the user's edits are recorded, recognised by their change gestures; host automation never enters the undo history or marks the preset modified.
- Undoing a preset load restores the previous values, preset name and modified state.
- Undo and redo announce what changed, for example "Undo Tap 3 Feedback, 40%". The history lives in the processor, so it survives closing the editor, and is cleared when the host restores a session.

## Automation

- Every parameter is automatable. The tap selector is UI state and the output clip is a session setting; neither is a parameter.
- Parameter names must be properly labelled and reflect exactly what they change, including the tap number, for example "Tap 3 Feedback" or "Tap 3 Stutter Probability".

## UI

### Visual style

The window is resizable. The visual style is plain, as the plugin will mostly be used by blind individuals, but it uses a high-contrast colour scheme (light text on dark) with large, clear labels, and text scales with the window size for low-vision users.

### Grouping

It is imperative to group controls based on their function. This is vital for proper VoiceOver accessibility in macOS, as users will interact with groupings to access the controls inside. Without this, the interface appears disorganized and cluttered. It is also necessary to use JUCE-specific features to set accessibility info so help tags on VoiceOver work. Think of help tags as the screen-reader equivalent of tooltips.

There are four top-level groups, in this order:

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
    - Nine nested glitch sub-groups, named after each glitch type, in processing order: Reverse, Stutter, Granularize, Pitch, LPC formant, Cepstral formant, Ring modulation, Frequency modulation, Bit crusher. Inside each, the probability comes first, followed by the range controls, each minimum before its maximum. Pitch has two pairs: minimum and maximum, then minimum speed and maximum speed.
3. Global, with three nested sub-groups:
    - Timing: host sync, glide time, freeze
    - Glitch engine: glitch threshold, glitch placement, buffer size, maximum simultaneous glitches, minimum glitch length, maximum glitch length, reproducible randomness, seed
    - Output: smear amount, smear size, mix, output gain (whose context menu sets the output clip)
4. Performance: a single control, the performance area (see below).

Every control in the tap group includes the tap number in its accessible name (for example "Tap 3 Feedback"), so each control identifies itself without relying on announcements.

### Help tags

Every control has a help tag of one or two plain sentences describing what it does. Ranges are left out, since screen readers already announce each value and the extra words add verbosity. Claude drafts them, kept together in one table in the code so they are easy to edit. They are reviewed by the author before release.

### Keyboard

Keyboard shortcuts, Windows / macOS:
- Next group: ALT+. / OPTION+. (hosts keep CMD+, and CMD+. for themselves)
- Previous group: ALT+, / OPTION+,
- Undo: CTRL+Z / CMD+Z
- Redo: CTRL+SHIFT+Z / CMD+SHIFT+Z
- Save: CTRL+S / CMD+S
- Load: CTRL+O / CMD+O
- Toggle host sync: CTRL+Y / CMD+Y, announcing "Host sync on" or "Host sync off"
- Toggle freeze: ALT+F / CMD+F, announcing "Freeze on" or "Freeze off". Not available in the performance area, which has its own freeze keys.

- Context menu: ] on both platforms. Opens the context menu of the focused control, if it has one, and does nothing otherwise. It stands in for the applications key, which JUCE doesn't reliably receive on Windows.

Context menus:
- A control with a context menu opens it three ways: the ] key, a right-click, and the screen reader's show-menu action (VO+SHIFT+M in VoiceOver).
- Moving focus to a control with a context menu announces "has context menu" after the screen reader has read the control. Help tags don't mention menus, and nothing says what a menu contains; users explore it themselves. In code this is `ContextMenuHint`, which finds the menu through `ContextMenuTarget`, so a new menu is announced without further work.
- Choosing an item announces it. The menu's current setting is ticked.
- Output gain: "Clip at +18 dBFS", "Clip at 0 dBFS", "No clipping", setting the output clip.
- Pitch probability: "Sweep", "Varispeed", setting the selected tap's pitch mode. This is a parameter, so choosing one is an undoable edit.
- In code, a control gains a menu by implementing `ContextMenuTarget` (`source/ui/ContextMenu.h`); sliders take one through `setContextMenu`.

Previous and next group controls don't actually place focus on the group itself, but the first control inside said group. They move between the four top-level groups only, and wrap around, as should using the TAB key. TAB is not constrained by grouping, and navigates the interface in a flat manner.

Taps. These work from any control except a type-in field and the performance area, and never move focus:
- 1 to 9 and 0 switch to taps 1 to 10; SHIFT+1 to SHIFT+6 switch to taps 11 to 16. Keys are matched by position on the number row, so layouts that need SHIFT to type digits are not supported.
- MINUS and EQUALS switch to the previous and next tap, wrapping in both directions.
- Switching announces the tap ("Tap 5") without interrupting, so the focused control's value for the new tap is read after it. Nothing is announced when focus is on the tap selector, which reads its own new value. Switching to the tap that is already selected does nothing.
- BACKSPACE turns the selected tap on or off, announcing "Tap 5 on" or "Tap 5 off".

Copy and paste (CTRL on Windows, CMD on macOS):
- With focus on the tap selector or the tap's on/off toggle, CTRL+C copies the whole tap except whether it is on, including both the millisecond and synced values. CTRL+V pastes it onto the selected tap and CTRL+SHIFT+V onto all taps; neither changes whether a tap is on. Announced as "Tap copied", "Tap pasted" and "Tap pasted to all".
- With focus on a per-tap slider, the same keys copy and paste that one setting, announced with its name: "Time copied", "Time pasted", "Time pasted to all". It pastes only onto the same control. While host sync is on, the time and stutter slice controls are the synced ones, so a copied note value does not paste onto the millisecond control.
- "Can't paste" is announced when nothing has been copied or the copy doesn't match the focused control.
- On any other control these keys do nothing.
- A paste is one undo step. The copy belongs to one plugin instance and survives closing the window; it is not saved with the session or in presets, and is not placed on the system clipboard.

Sliders:
- All controls that increase and decrease a value should be sliders.
- HOME and END jump to the maximum and minimum respectively.
- Arrow keys increase and decrease by a musical step suited to the value's unit. Adding SHIFT gives a fine adjustment; adding CTRL (Windows) or CMD (macOS) gives a coarse adjustment.
    - dB: 0.5 dB, fine 0.1 dB, coarse 3 dB
    - Milliseconds: 10 ms, fine 1 ms, coarse 100 ms
    - Percent: 1%, fine 0.1%, coarse 10%
    - Semitones: 1, fine 0.1, coarse 12
    - Semitones per pass (sweep speed): 0.5, fine 0.1, coarse 3
    - Note values: one step through the list; no fine step; coarse jumps between straight values only
- DELETE sets the slider to its default value. This is the forward delete key (FN+DELETE on Mac laptops), since BACKSPACE turns the selected tap on or off.
- ENTER presents a type-in field. It accepts plain numbers in the control's own unit, optional unit suffixes (ms, s, Hz, kHz, dB, %, st), "-inf" for volume, and note values such as "1/8", "1/8d", "1/8t", "1/64" and "4 bars". ENTER accepts the value if it is valid and in range. An out-of-range or unrecognised value is rejected: the screen reader announces the valid range (for example "Out of range, 1 ms to 5 s") and the field stays open with the rejected text selected, so the user can either type over it or move into it to fix it. ESC cancels.
- Announcements (such as type-in errors) go through the user's screen reader. On Windows this uses UI Automation notification events rather than JUCE's default, which speaks through the system voice.

### Performance area

The Performance group holds one canvas-like control named "Performance area", with the help tag "Provides additional functionality for live performance." It is exposed to screen readers as an image, the nearest role to a canvas that both platforms name. While it has focus its keys play the plugin rather than edit it. TAB, SHIFT+TAB, the group keys, undo, redo, save, load and the host sync shortcut still work; tap switching, tap on/off, copy and paste, and ALT+F do not.

It acts on a selection of taps, which is separate from the selected tap and has no effect anywhere else. The selection starts as all taps, survives closing the window, and is not saved with the session or in presets.

- 1 to 0 and SHIFT+1 to SHIFT+6 add a tap to the selection or remove it, announcing "Tap 3 selected" or "Tap 3 unselected".
- BACKSPACE selects all taps, or none if all are already selected, announcing "All taps" or "No taps".
- SHIFT+BACKSPACE selects the even-numbered taps, or the odd-numbered ones if exactly the even ones are selected, announcing "Even taps" or "Odd taps".
- UP and DOWN make the time of every selected tap that is on 10% longer or shorter (multiplying or dividing by 1.1, so the taps keep their ratios). While host sync is on they move each tap by one note value instead. If any of those taps would pass its limit, none of them move. There are no modifiers and nothing is announced. One press is one undo step for all the taps, and the repeats of a held key join into that step.
- LEFT and RIGHT lower and raise the smear size by 10 ms; with SHIFT they lower and raise the smear amount by 5%. Both stop at their limits. Nothing is announced. One press is one undo step, and the repeats of a held key join into that step.
- F freezes while it is held and releases when it is let go or focus leaves. If freeze was already on, releasing F turns it off. It is not announced and adds no undo step, but is sent to the host as one gesture so it can be recorded as automation.
- SHIFT+F switches freeze on or off, announcing "Freeze on" or "Freeze off".

Visually it is a row of 16 numbered cells, filled when selected and dimmed when the tap is off, with a freeze indicator beside them. Clicking a cell adds or removes that tap.
