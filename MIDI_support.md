# MIDI support

This document records the agreed design for Astralay's first MIDI support implementation. It supersedes the original draft where the design interview changed a decision. It describes intended behavior, not functionality already implemented or verified.

## Scope and terminology

Astralay receives MIDI CC and pitch-bend messages to control eligible sound parameters on every tap, global controls, and macro values.

- A **source** is a MIDI channel plus message type, and a controller number for CC messages.
- A **target** is an eligible sound control, identified independently of its display name. Tap targets identify a fixed tap; macro targets identify a fixed macro slot.
- A **binding** connects one source to one target.
- The **current mapping** is the complete set of bindings held by one plugin instance.
- A **mapping file** is a reusable, nonempty mapping saved as JSON. It contains bindings, not sound settings or macro configuration.
- The **default mapping** is the startup preference for new instances: a reference to a mapping file or an explicit preference to start empty.
- An **empty mapping** exists internally and in project state, but never as a valid mapping file.

## Audio, MIDI, and host integration

Support simultaneous audio and MIDI input wherever the host and plugin format permit the required routing. Preserve stereo input/output and the existing mono-input support.

The two required routing arrangements are:

1. A track receives MIDI directly, while Astralay also receives audio on its first stereo pair.
2. A track receives audio, while another track sends MIDI to Astralay on that track.

Apply this intent to the existing VST3, CLAP, AU, and Standalone builds. Hosted plugins use MIDI delivered by the DAW; the standalone application uses its own MIDI input selection. Do not depend on Reaper-specific routing.

Pass incoming MIDI through unchanged where supported by the host and format, including mapped messages and the event used to learn a binding. Astralay generates no additional MIDI. Notes and unsupported messages have no effect on Astralay itself.

Document host-specific setup or limitations rather than promise identical routing in every DAW. Reaper is currently the only host available for manual verification; other-host compatibility remains unverified until it can be tested.

## Supported sources and binding rules

- Support all CC numbers 0–127 as independent absolute 7-bit values, including numbers conventionally assigned special MIDI meanings.
- Support 14-bit pitch bend.
- Bind the exact MIDI channel, 1–16. There is no all-channels binding in this version.
- Do not interpret relative encoders, combined 14-bit CC pairs, RPN, or NRPN. Their constituent CC messages are treated as ordinary independent CCs.
- One source may control multiple targets.
- Each target has at most one source. Learning a different source onto an already-bound target replaces its previous binding.
- A tap binding always controls the tap selected when the target was captured, regardless of subsequent tap navigation.
- A macro binding always controls its captured macro slot, even if the macro is renamed or reconfigured.

## Eligible targets

Every sound parameter exposed as an eligible control in the main interface can be learned. This includes tap sliders and enable switches, global sliders and switches, placement, and macro values.

The following are ineligible:

- Parameters available only as items inside context menus. In particular, Pitch Mode is not eligible, even though it is a sound parameter.
- Output-clipping settings.
- Interface actions and navigation, including tap selection, macro arming, preset selection, and menu buttons.
- Macro modulation amounts and routing settings.
- Composite interface actions such as the performance pad; learn its eligible sound parameters through their individual controls instead.

The restriction on context-menu-only parameters supersedes the earlier proposal to support Pitch Mode through a special learn entry. Do not add such an entry.

### Time controls and host sync

Bind the logical time control, not only the parameter currently displayed by it. This applies to tap delay time, stutter minimum/maximum, and global buffer size.

When unsynced, MIDI controls the milliseconds parameter. When synced, it controls the corresponding note-value parameter. Use the full range of the active parameter. Changing sync mode does not remove the binding or change its captured tap.

## MIDI learn

Learning has three states: inactive, waiting for a target, and waiting for MIDI. It never times out while the editor remains open.

### Starting from the keyboard

Press Alt+L on Windows or Cmd+L on macOS:

- If learning is already active, cancel it regardless of focus.
- Otherwise, if the focused control is eligible, capture that target, announce "MIDI learn", and wait for MIDI.
- If the focused control is ineligible, announce "This control cannot be MIDI learned" and abort. Do not enter the waiting-for-target state.

### Starting from the MIDI learn button

Add a "MIDI learn" button to the main interface grouping.

- When learning is inactive, activating the button announces "Waiting for target" and enters that state.
- Clicking an eligible control or pressing Enter on it captures the target, announces "MIDI learn", and begins waiting for MIDI.
- Consume the target-selection action. It must not also change the control, toggle it, or open its value editor.
- Ineligible controls retain ordinary navigation behavior and cannot become targets. Actions that cancel learning, listed below, still do so.
- Activating the MIDI learn button while either learning stage is active cancels learning.

### Context menus

Give controls context menus, preserving pre-existing options. Append "MIDI learn" below existing options for eligible controls; do not allow it to initiate learning for ineligible controls.

When learning is active, the learn entry becomes "Cancel MIDI learn" throughout the control context menus, whether or not a target has been captured. Cancellation must remain available from ineligible controls as well.

A context menu's learn action targets its owning control, not parameters listed inside that menu. For example, the Output Gain menu can learn gain, but not clipping. Macro arm buttons must not inherit a learn action for the macro value.

### Capturing a source

When waiting for MIDI, the first CC or pitch-bend event establishes the binding. Stop learning and announce:

`"<control name> set to <input name>"`

Identify the captured control unambiguously, including the tap where applicable. The source identity includes its channel and CC number or pitch-bend type.

The event that completes learning does not change the new target's value or any other target already bound to that source. Subsequent events drive all bound targets. This suppression applies to Astralay's parameter handling only; MIDI passthrough remains unchanged.

While waiting for a target, existing mappings continue to operate normally. Unsupported messages neither complete nor cancel learning.

Once captured, a target remains fixed through ordinary focus changes and tap navigation. A sync change follows the logical-control rules above.

### Macro arming

Entering either active learning state disarms any armed macro and announces that change. Learn always targets the sound parameter or macro value, never the modulation amount shown by an armed macro's editing mode.

### Cancellation

Cancel either active learning state through Alt+L / Cmd+L, the MIDI learn button, a "Cancel MIDI learn" entry, or Escape. Announce "MIDI learn canceled".

Also cancel learning when:

- The editor closes.
- A mapping load, mapping save dialog, sound-preset load, or macro-arming action begins.
- A binding is removed, the current mapping is cleared, or project state is restored.

Cancel first when beginning an incompatible user action, announcing cancellation while the editor is present. Ordinary focus changes, tap navigation, sync changes, and closing a context menu alone do not cancel learning.

Established mappings continue to work with the editor closed. Pending learning is not restored by reopening the editor or restoring project state.

## Parameter movement and timing

MIDI writes the target's stored parameter value. It is not a temporary modulation that disappears when MIDI stops. The current value appears in the interface and is saved with sound/project state. Existing macro modulation applies afterward as it does today.

- Use the parameter's existing normalized slider curve, including skew, rather than a new linear scale in physical units.
- CC 0 reaches the minimum and CC 127 reaches the maximum.
- Pitch bend reaches both endpoints; its center value, 8192, maps exactly to the normalized midpoint.
- Snap stepped parameters to valid values using their existing parameter stepping.
- For toggles, the lower half of the source range means Off and the upper half means On. There is no press-to-toggle behavior.
- Use immediate movement, with no pickup requirement. The first event after learning completes can jump the parameter to the controller's position.
- Honor MIDI event positions within each audio block.
- Add no new MIDI smoothing in this version. Existing parameter/DSP smoothing still applies, so rapid or large controller movements can produce abrupt audible changes.

Expose MIDI-driven parameter changes to the host for automation recording where supported. If host automation and MIDI both control a parameter, the latest applied update wins; neither has permanent priority.

## Modulation by macro

For parameters supported by the existing macro system, users can limit MIDI-driven travel by configuring a macro modulation and learning the macro value.

Existing macro behavior is unchanged:

- Modulation adds the macro value multiplied by its configured amount to the target's base value.
- The base value, amount, and unipolar/bipolar macro mode determine travel; this is not an independent pair of minimum/maximum endpoints.
- Other active macros may further change the result, which is clamped/snapped as usual.
- Macro routing currently supports tap sliders and the glide, smear amount, smear size, and mix globals. It excludes output gain, glitch-engine controls, toggles, choices, and other macros.

Consequently, the macro workaround is not available for every MIDI target. A mapping file does not carry macro names, bipolar settings, routing, modulation amounts, or parameter values into another instance. A macro binding uses whatever configuration its fixed slot currently has.

## Project state and sound presets

Store the complete current mapping in plugin project state, including bindings never saved to a JSON file. Preserve the mapping through project save/reopen, instance duplication, and host state restoration without requiring the external mapping file to exist.

Preserve the mapping's file association and unsaved-edit status as needed to continue Save/Save As behavior after restoration, but restore bindings from the project snapshot rather than replacing them with the current disk contents.

- Restored project mappings always take precedence over the startup default, including explicitly empty mappings.
- Projects saved before MIDI support restore with no mappings. A newly configured default must not introduce mappings into those projects.
- Loading an Astralay sound preset leaves MIDI bindings unchanged.
- Mapping files contain bindings only; sound presets and macro settings remain separate.

## Undo and modification tracking

- Learning/replacing a binding, removing a binding, clearing the mapping, and loading a mapping each form one undoable action.
- Undoing or redoing a mapping action affects mappings and their in-memory metadata only, not sound values or external files.
- Incoming MIDI movements stay outside Astralay's undo history, including when exposed to host automation recording.
- Mapping edits do not mark the sound preset modified.
- MIDI movements that actually change sound parameter values do mark the sound preset modified.
- Incoming value changes do not modify the mapping's bindings or make its mapping file dirty.
- Saving a file and changing the global default are not undoable.

## Removing bindings and clearing the mapping

Add "Remove MIDI mapping" to a bound eligible control's context menu. Removing one target's binding does not remove other bindings using the same source.

Add "Clear mapping" to the MIDI menu. Invoking it cancels learning first. After one confirmation, it:

1. Discards the complete current in-memory mapping, including unsaved mapping edits, without a save prompt.
2. Switches to an unnamed internal empty mapping.
3. Leaves all existing mapping files and the global default preference unchanged.

Clear remains undoable. Undo restores the previous in-memory mapping, file association, and unsaved edits; it does not undo a separately chosen default change.

Removing the last binding individually reaches the same unnamed internal empty state, without a confirmation dialog. That removal remains undoable.

An empty mapping cannot be saved to a file. Save and Save As are unavailable while empty, but "Set current mapping as default" remains available.

## Mapping files

Use JSON files in `Documents/Astralay/MIDI Mappings`, the sibling of the existing Presets folder. Create the MIDI Mappings folder at plugin startup if it does not exist.

Mappings must be portable between supported platforms. Use stable target identities and logical time-control identities, not translated labels, machine-specific paths, or macro names, inside mapping files.

### Saving and filenames

Offer "Save MIDI mapping" and "Save MIDI mapping as..." for nonempty mappings.

- Save updates the associated file. An unnamed mapping prompts for a name.
- Save As always prompts for a name.
- Replacing another existing file requires confirmation.
- If the associated file has changed externally since loading/saving, ask before overwriting it.
- Use an accessible type-in like the existing control-value editors, never a native file dialog.

Apply portable filename rules:

1. Trim leading and trailing whitespace.
2. Strip the final filename extension, if supplied, and use `.json` as the saved extension.
3. Reject an empty resulting name, path separators, control characters, Windows-invalid filename characters (`< > : " / \ | ? *`), reserved device names, and names ending in a dot.
4. Preserve internal spaces and Unicode.

Names must be valid on both Windows and macOS, not merely on the current platform. Validate the resulting filename before saving. On invalid input, report a specific accessible error and keep the name editor open for correction.

A failed or canceled save leaves the current in-memory mapping intact. It prevents any dependent mapping load or default-setting operation from continuing. Do not replace a valid existing file with a partial or failed write.

### Validation

Validate the entire file before applying any bindings. Require a recognized mapping format and supported version, at least one binding, valid source types and channel/controller ranges, and known eligible target identities.

Reject unknown or ineligible targets, unsupported versions, malformed content, empty mappings, and duplicate target assignments that violate the one-source-per-target rule. Multiple targets sharing one source are valid.

Do not partially load a file. Validation failure leaves the current mapping unchanged.

### Discovery and loading

Scan and validate mapping files:

- At startup.
- Whenever the MIDI menu opens.
- After a successful save.

List only valid mappings, sorted by name. Mark current and default entries where applicable. Revalidate a selected file before replacing the current mapping, since it may have changed since the scan.

Do not announce automatic scan summaries or summaries of rejected files. Opening the menu must not interrupt the user's workflow; the menu will gain other features in future. Failures of an explicitly requested operation still need accessible feedback.

Loading a file replaces the complete current mapping. If the current nonempty mapping has unsaved mapping edits, offer Save / Discard / Cancel first. Continue only after a successful save or an explicit discard. If the current mapping is empty, there is no empty-file save step.

Loaded mappings are independent in-memory copies. Saving or externally editing a shared file does not silently change other running instances.

## Startup defaults

With no default configured, new instances start empty. Only the current mapping can be made the default.

### Nonempty current mapping

"Set current mapping as default" follows these rules:

- If the mapping has an existing, valid saved file and no unsaved edits or external mismatch, set that file as the default.
- If the associated mapping has unsaved edits, present a Yes/No dialog explaining that it must be saved first. Yes saves and sets the default only after success; No leaves the default unchanged.
- If the mapping has no existing saved file, use the normal save-name type-in, then set the default only after a successful save.
- Honor external-change overwrite confirmation. The default must refer to the successfully saved current mapping, not silently to different contents that appeared on disk.

The default references the saved file. New instances read its latest valid contents; already-running instances retain their own mappings. If the default file is missing or invalid, start empty and make an accessible failure status available without issuing automatic library-scan summaries.

### Empty current mapping

"Set current mapping as default" remains available after clearing or removing the last binding. Store a persistent preference to start with no mappings and remove any previous default-file reference. Do not create an empty JSON file or delete existing mapping files.

Also offer "Clear default MIDI mapping". It produces the same empty-startup behavior, without changing the current instance's bindings.

This empty-default behavior supersedes the original rule that a current mapping needed at least one binding before it could be made the default. Restored project state still takes precedence over every startup-default setting.

## Main menu

Add a "Main menu" button as the first item in the main interface grouping. Clicking it or pressing Enter opens a context menu. Initially, its only top-level option is the MIDI submenu.

The MIDI submenu contains:

1. "Smoothing", disabled as a placeholder for future work.
2. "Save MIDI mapping" and "Save MIDI mapping as...", available only for a nonempty current mapping.
3. "Set current mapping as default", available even when the current mapping is empty.
4. "Clear default MIDI mapping".
5. "Clear mapping", applicable when bindings exist.
6. Valid mapping files by name, sorted, with current/default markers where applicable.

Retain the existing accessible interaction conventions for menus, type-ins, confirmations, and operation results. Additional binding-inspection UI, source information added to control descriptions/menus, and a current-mapping modified-status display are out of scope for this version. This does not remove the original learn completion/cancellation announcements or the agreed current/default markers in the file list.

## Verification requirements

These are implementation acceptance checks, not claims of completed testing:

- In Reaper, verify both direct-MIDI-plus-audio and audio-plus-MIDI-send routing, including stereo and existing mono-input support.
- Verify source/channel matching, all CC numbers, pitch-bend endpoints and exact midpoint, one-to-many bindings, and replacement of a target's source.
- Verify eligibility exclusions, fixed tap/macro identity, sync-following time controls, and macro behavior without changing existing modulation semantics.
- Verify keyboard, mouse, and context-menu learn paths; target-selection action consumption; no timeout; cancellation; and the non-moving capture event.
- Verify event-position timing, MIDI passthrough where supported, operation with the editor closed, and host automation interaction without MIDI flooding undo history.
- Verify project reopening/duplication, unsaved mappings, explicit empty mappings, legacy project state, sound-preset independence, and independence from external files after state restoration.
- Verify mapping-only undo/redo, modification tracking, clearing with unsaved edits, removal of the final binding, and restoring the pre-clear mapping through undo.
- Verify portable filenames and cross-platform file loading, complete validation, rejection of empty files, overwrite/external-change handling, failed saves, and refresh without unsolicited scan announcements.
- Verify named, modified, missing, invalid, and explicitly empty defaults, including that existing instances and restored projects remain unaffected by later default changes.
- Record the tested OS, host version, and plugin formats. Other-host checks are deferred until suitable hosts are available; do not claim broader host verification from Reaper results alone.

## Deferred features

Relative encoders, combined high-resolution CC protocols, all-channels bindings, pickup, new MIDI smoothing, MIDI control of context-menu-only parameters, expanded macro routing, and additional binding-inspection UI are outside this implementation.
