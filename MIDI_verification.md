# MIDI implementation verification

The agreed behavior is in [MIDI_support.md](MIDI_support.md). The implementation uses JSON format `Astralay MIDI mapping`, version 1. Each binding contains a stable `target` parameter ID, `type` (`cc` or `pitchBend`), `channel` (1–16), and, for CC, `controller` (0–127). Synced time IDs and Pitch Mode are rejected; logical times use their unsynced IDs and resolve the active parameter at runtime. Empty bindings are allowed only in embedded project state.

## Automated verification

Environment: Windows (OS version 10.0.26200.0), Visual Studio 2022 Build Tools (MSBuild 17.13.15), Release configuration, October 6, 2026. The processor/editor test runner exercises MIDI independently of a host. Windows VST3, CLAP and Standalone targets built successfully. AU requires a macOS build.

`tests/MidiTests.cpp` covers all 128 CCs on all 16 channels, pitch-bend endpoints/centre, source replacement, one-to-many capture suppression, unsupported messages, sync-following logical times, fixed taps, toggles, persistent parameter changes, passthrough, undo isolation, project snapshots, file validation, portable filenames, safe saves, external mismatches, defaults, editor selection consumption and event-offset rendering. Existing editor tests include the new tab order and context-menu accessibility actions.

Run `cmake --build build --config Release --parallel`, then `ctest --test-dir build -C Release --output-on-failure`.

Final automated run: **253,838 checks passed, 0 failed**; CTest passed in 47.66 seconds. One earlier full-suite run failed the existing direct-DSP test `Freeze sustain lifecycle / Sustain survives a glide, fades off, recaptures on refreeze and clears on tap disable`, with `Turning sustain off didn't release recovery`. That test does not use MIDI or the processor, and passed on the final rerun. Its intermittent failure has not been investigated or changed as part of MIDI support.

The build environment initially supplied duplicate `Path`/`PATH` entries to MSBuild. Builds succeeded after normalising the process environment and disabling MSBuild node reuse (`MSBUILDDISABLENODEREUSE=1`, `-j 1 -- /nr:false`). No repository-wide build-environment settings were changed.

## Manual acceptance still required

No DAW or screen-reader session was controlled during implementation. Reaper is the available manual test host; its version and tested plugin formats must be recorded when these checks are performed. Other-host compatibility remains unverified. No macOS/AU build or host test has been performed here.

- Reaper: record OS and host version; test VST3 and CLAP with direct MIDI plus audio, and an audio track receiving a MIDI send. Verify stereo and mono input and MIDI output to a downstream monitor.
- Standalone: select MIDI inputs in audio/MIDI settings and verify simultaneous audio. Confirm routing of MIDI output where configured.
- With a screen reader: keyboard, mouse and context-menu learn; consumed click/Enter; announcements including tap/slot/channel; macro disarming; no timeout; cancellation and editor reopening.
- During playback: rapid controller changes, pitch bend, source replacement and fan-out, tap navigation, sync changes, macro modulation, editor-closed operation and host automation recording/conflicts.
- Project save/reopen and instance duplication: saved, unsaved and explicitly empty mappings; legacy projects; changed/missing external files; presets and defaults remain independent.
- File UI: invalid names stay editable; overwrite/external-change confirmation; Save/Discard/Cancel on load; failed/canceled saves block dependent operations; invalid files are omitted without announcements; empty, modified, missing and invalid defaults; undo/redo restores in-memory metadata without changing files.

Plugin-format MIDI representation and host routing can limit passthrough and automation recording. The processor does not remove or generate events; wrapper/host behavior needs the format-specific manual checks above.

## MIDI smoothing implementation verification

The confirmed design in [MIDI_smoothing.md](MIDI_smoothing.md) is implemented. Windows Release builds of the test runner, VST3, CLAP, and Standalone pass. Final CTest passed in 32.30 seconds: 253,916 checks passed, 0 failed.

Automated coverage includes fast/slow linear and exponential timing; multiple sample rates; normalized movement of skewed controls and macro values; pitch-bend fan-out; duplicate-event handling; retargeting; live shared preferences; preference reload, invalid fallback, unrelated setting preservation and write failures; immediate stepped controls; mouse/automation cancellation; sync, binding, learn, preset and project interruptions; continued movement in MIDI-free audio blocks; event offsets, passthrough and bounded host notifications. MIDI tests use isolated temporary preference folders rather than the user's saved smoothing setting.

The Debug test build remains blocked by an existing unrelated diagnostics compile error: source/state/DiagnosticLog.cpp calls dsp::diagnostics::isNonFinite, which is not declared. Release validation does not compile that diagnostics path.

Manual acceptance remains required for the accessible smoothing submenu and change announcements, controller feel in Reaper, host automation recording of the smoothed trajectory, and live preference updates across multiple hosted instances. No macOS/AU build or manual host/screen-reader test was performed.
