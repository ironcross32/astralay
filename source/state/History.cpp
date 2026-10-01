#include "History.h"

namespace astralay::state
{

namespace
{
    // Transaction names say what to announce: a parameter ID (announced with its new value) or
    // free text.
    const juce::String parameterPrefix = "parameter:";
    const juce::String textPrefix = "text:";
}

//==============================================================================
/** One parameter's change. Already applied when recorded, so the first perform() does nothing. */
class History::ParameterChange final : public juce::UndoableAction
{
public:
    ParameterChange (History& historyToUse, int index, float beforeValue, float afterValue, bool alreadyApplied)
        : history (historyToUse), parameterIndex (index), before (beforeValue), after (afterValue), skipPerform (alreadyApplied)
    {
    }

    bool perform() override
    {
        if (std::exchange (skipPerform, false))
            return true;

        history.setValue (parameterIndex, after);
        return true;
    }

    bool undo() override
    {
        history.setValue (parameterIndex, before);
        return true;
    }

    /** Merges a following change to the same parameter into one step. */
    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<ParameterChange*> (next); change != nullptr && change->parameterIndex == parameterIndex)
            return new ParameterChange (history, parameterIndex, before, change->after, false);

        return nullptr;
    }

private:
    History& history;
    int parameterIndex;
    float before, after;
    bool skipPerform;
};

//==============================================================================
class History::SnapshotChange final : public juce::UndoableAction
{
public:
    SnapshotChange (History& historyToUse, Snapshot beforeSnapshot, Snapshot afterSnapshot)
        : history (historyToUse), before (std::move (beforeSnapshot)), after (std::move (afterSnapshot))
    {
    }

    bool perform() override { history.applySnapshot (after); return true; }
    bool undo() override    { history.applySnapshot (before); return true; }

    int getSizeInUnits() override { return (int) (before.values.size() + after.values.size()); }

    /** Merges a following snapshot in the same step into one change. */
    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* change = dynamic_cast<SnapshotChange*> (next))
            return new SnapshotChange (history, before, change->after);

        return nullptr;
    }

private:
    History& history;
    Snapshot before, after;
};

//==============================================================================
History::History (juce::AudioProcessor& processorToUse)
    : processor (processorToUse)
{
    for (auto* parameter : processor.getParameters())
        parameter->addListener (this);
}

History::~History()
{
    for (auto* parameter : processor.getParameters())
        parameter->removeListener (this);
}

void History::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    if (applying)
        return;

    auto* parameter = processor.getParameters()[parameterIndex];

    if (parameter == nullptr)
        return;

    const auto value = parameter->getValue();
    float before;

    {
        const juce::ScopedLock lock (gestureLock);

        if (gestureIsStarting)
        {
            gestureStartValues[parameterIndex] = value;
            return;
        }

        const auto started = gestureStartValues.find (parameterIndex);

        if (started == gestureStartValues.end())
            return;

        before = started->second;
        gestureStartValues.erase (started);
    }

    // Gestures normally end on the message thread, but a host may send them from elsewhere.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        gestureEnded (parameterIndex, before, value);
    }
    else
    {
        juce::MessageManager::callAsync ([weak = juce::WeakReference<History> (this), parameterIndex, before, value]
        {
            if (weak != nullptr)
                weak->gestureEnded (parameterIndex, before, value);
        });
    }
}

void History::gestureEnded (int parameterIndex, float before, float after)
{
    if (juce::approximatelyEqual (before, after))
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const auto merge = parameterIndex == lastParameter && now - lastEditTime <= mergeWindowMs;

    if (! merge)
    {
        const auto* parameter = processor.getParameters()[parameterIndex];
        const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
        undoManager.beginNewTransaction (parameterPrefix + (withId != nullptr ? withId->paramID : juce::String()));
    }

    undoManager.perform (new ParameterChange (*this, parameterIndex, before, after, true));

    lastParameter = parameterIndex;
    lastEditTime = now;
    snapshotStepOpen = false;

    if (onUserEdit != nullptr)
        onUserEdit();
}

History::Snapshot History::capture (const juce::String& presetName, bool modified) const
{
    Snapshot snapshot;
    snapshot.presetName = presetName;
    snapshot.modified = modified;

    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            snapshot.values[withId->paramID] = parameter->getValue();

    return snapshot;
}

void History::applyAndRecord (const Snapshot& before, const Snapshot& after, const juce::String& description,
                              bool mergeWithPrevious)
{
    if (! (mergeWithPrevious && snapshotStepOpen))
        undoManager.beginNewTransaction (textPrefix + description);

    undoManager.perform (new SnapshotChange (*this, before, after));
    lastParameter = -1;
    snapshotStepOpen = true;
}

void History::clear()
{
    undoManager.clearUndoHistory();
    lastParameter = -1;
    snapshotStepOpen = false;
}

void History::setValue (int parameterIndex, float normalisedValue)
{
    if (auto* parameter = processor.getParameters()[parameterIndex])
    {
        const juce::ScopedValueSetter<bool> applyingChange (applying, true);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (normalisedValue);
        parameter->endChangeGesture();
    }
}

void History::applySnapshot (const Snapshot& snapshot)
{
    {
        const juce::ScopedValueSetter<bool> applyingChange (applying, true);

        for (auto* parameter : processor.getParameters())
        {
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            {
                const auto found = snapshot.values.find (withId->paramID);
                const auto value = found != snapshot.values.end() ? found->second : parameter->getDefaultValue();

                if (! juce::approximatelyEqual (parameter->getValue(), value))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost (value);
                    parameter->endChangeGesture();
                }
            }
        }
    }

    if (onSnapshotApplied != nullptr)
        onSnapshotApplied (snapshot);
}

juce::String History::announcementFor (const juce::String& verb, const juce::String& transactionName) const
{
    // A parameter step announces the parameter and the value it now has.
    if (transactionName.startsWith (parameterPrefix))
    {
        const auto id = transactionName.fromFirstOccurrenceOf (parameterPrefix, false, false);

        for (auto* parameter : processor.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter); withId != nullptr && withId->paramID == id)
                return verb + " " + parameter->getName (128) + ", " + parameter->getCurrentValueAsText();
    }

    return verb + " " + transactionName.fromFirstOccurrenceOf (textPrefix, false, false);
}

juce::String History::undo()
{
    const auto name = undoManager.getUndoDescription();

    if (! undoManager.undo())
        return "Nothing to undo";

    lastParameter = -1;
    snapshotStepOpen = false;
    return announcementFor ("Undo", name);
}

juce::String History::redo()
{
    const auto name = undoManager.getRedoDescription();

    if (! undoManager.redo())
        return "Nothing to redo";

    lastParameter = -1;
    snapshotStepOpen = false;
    return announcementFor ("Redo", name);
}

} // namespace astralay::state
