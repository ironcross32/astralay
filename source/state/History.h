#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "Macros.h"

namespace astralay::state
{

/** Undo and redo for the user's edits.

    Edits are recognised by their change gestures (a slider drag, a key press, a toggle, a menu
    choice), so host automation, which doesn't send gestures, never enters the history. A run of
    edits to the same parameter in quick succession, such as repeated arrow presses, merges into
    one step. Whole-state changes such as preset loads are recorded with recordStateChange().

    Lives in the processor so the history survives the editor being closed. Message thread only,
    apart from the gesture callbacks, which are forwarded to it.
*/
class History final : private juce::AudioProcessorParameter::Listener
{
public:
    /** Parameter values by ID, in normalised form, plus the macro settings, the preset name and
        the modified flag. A macro's value is normalised over the range that macro has in the
        same snapshot.
    */
    struct Snapshot
    {
        std::map<juce::String, float> values;
        MacroSettings macros;
        juce::String presetName;
        bool modified = false;
    };

    explicit History (juce::AudioProcessor& processor);
    ~History() override;

    /** Called on the message thread after each recorded user edit (not undo, redo or snapshots). */
    std::function<void()> onUserEdit;

    /** Called as a snapshot is applied, before its parameter values are set, so the owner can
        restore the macro settings, the preset name and the modified flag.
    */
    std::function<void (const Snapshot&)> onSnapshotApplied;

    /** The current parameter values. The owner fills in the macro settings. */
    Snapshot capture (const juce::String& presetName, bool modified) const;

    /** Applies a snapshot and records it as one undoable step named description. With
        mergeWithPrevious, it joins the previous step instead if that was also recorded here and
        nothing has happened since, as for a held key.
    */
    void applyAndRecord (const Snapshot& before, const Snapshot& after, const juce::String& description,
                         bool mergeWithPrevious = false);

    /** While one of these exists, gestures aren't recorded, for changes that shouldn't be undoable. */
    class ScopedSuspend final
    {
    public:
        explicit ScopedSuspend (History& history) : setter (history.applying, true) {}

    private:
        juce::ScopedValueSetter<bool> setter;
    };

    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    /** Undoes or redoes one step. Returns a description to announce, or an empty string. */
    juce::String undo();
    juce::String redo();

    void clear();

    /** The maximum gap between edits of one parameter that still merge into a single step. */
    static constexpr juce::uint32 mergeWindowMs = 600;

private:
    class ParameterChange;
    class SnapshotChange;

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    void gestureEnded (int parameterIndex, float before, float after);
    void setValue (int parameterIndex, float normalisedValue);
    void applySnapshot (const Snapshot& snapshot);
    juce::String announcementFor (const juce::String& verb, const juce::String& transactionName) const;

    juce::AudioProcessor& processor;
    juce::UndoManager undoManager { 0, 100 };
    std::map<int, float> gestureStartValues;
    juce::CriticalSection gestureLock;

    int lastParameter = -1;
    juce::uint32 lastEditTime = 0;
    bool applying = false;
    bool snapshotStepOpen = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (History)
    JUCE_DECLARE_NON_COPYABLE (History)
};

} // namespace astralay::state
