#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

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
    /** Parameter values by ID, in normalised form, plus the preset name and modified flag. */
    struct Snapshot
    {
        std::map<juce::String, float> values;
        juce::String presetName;
        bool modified = false;
    };

    explicit History (juce::AudioProcessor& processor);
    ~History() override;

    /** Called on the message thread after each recorded user edit (not undo, redo or snapshots). */
    std::function<void()> onUserEdit;

    /** Called when a snapshot is applied by undo or redo, so the owner can restore the preset name
        and modified flag.
    */
    std::function<void (const Snapshot&)> onSnapshotApplied;

    Snapshot capture (const juce::String& presetName, bool modified) const;

    /** Applies a snapshot and records it as one undoable step named description. */
    void applyAndRecord (const Snapshot& before, const Snapshot& after, const juce::String& description);

    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    /** Undoes or redoes one step. Returns a description to announce, or an empty string. */
    juce::String undo();
    juce::String redo();

    void clear() { undoManager.clearUndoHistory(); }

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

    JUCE_DECLARE_WEAK_REFERENCEABLE (History)
    JUCE_DECLARE_NON_COPYABLE (History)
};

} // namespace astralay::state
