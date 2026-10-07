#include "PerformancePad.h"
#include "Announcer.h"
#include "Theme.h"
#include "params/Parameters.h"

namespace astralay::ui
{

namespace
{
    constexpr juce::uint32 allTaps = (1u << params::numTaps) - 1;
    constexpr juce::uint32 oddTaps = 0x55555555u & allTaps;   // Taps 1, 3, 5 and so on.
    constexpr juce::uint32 evenTaps = allTaps & ~oddTaps;

    constexpr int statusWidth = 150;
    constexpr int cellGap = 4;
}

int tapIndexForKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return -1;

    const auto code = key.getKeyCode();

    if (code >= '0' && code <= '9')
    {
        if (! mods.isShiftDown())
            return code == '0' ? 9 : code - '1';

        return code >= '1' && code <= '6' ? 10 + code - '1' : -1;
    }

    // macOS reports the shifted character as the key code, so recognise the US layout's.
    const auto shifted = juce::String ("!@#$%^").indexOfChar ((juce::juce_wchar) code);
    return shifted >= 0 && mods.isShiftDown() ? 10 + shifted : -1;
}

//==============================================================================
PerformancePad::PerformancePad()
{
    setWantsKeyboardFocus (true);
}

PerformancePad::~PerformancePad()
{
    releaseHeldKeys();
}

void PerformancePad::setSelection (juce::uint32 newSelection)
{
    selection = newSelection & allTaps;
    repaint();
}

void PerformancePad::setTapEnabled (int tapIndex, bool enabled)
{
    const auto bit = 1u << tapIndex;
    enabledTaps = enabled ? (enabledTaps | bit) : (enabledTaps & ~bit);
    repaint();
}

void PerformancePad::setFrozen (bool shouldBeFrozen)
{
    frozen = shouldBeFrozen;
    repaint();
}

void PerformancePad::setGlitchesStopped (bool shouldBeStopped)
{
    glitchesStopped = shouldBeStopped;
    repaint();
}

void PerformancePad::setTapeStopped (bool shouldBeStopped)
{
    tapeStopped = shouldBeStopped;
    repaint();
}

void PerformancePad::changeSelection (juce::uint32 newSelection, const juce::String& announcement)
{
    setSelection (newSelection);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (selection);

    announceFrom (*this, announcement);
}

//==============================================================================
bool PerformancePad::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return false;

    const auto code = key.getKeyCode();

    if (const auto tap = tapIndexForKey (key); tap >= 0)
    {
        const auto bit = 1u << tap;
        const auto nowSelected = (selection & bit) == 0;

        changeSelection (selection ^ bit, "Tap " + juce::String (tap + 1) + (nowSelected ? " selected" : " unselected"));
        return true;
    }

    if (code == juce::KeyPress::backspaceKey)
    {
        if (mods.isShiftDown())
        {
            const auto odd = selection == evenTaps;
            changeSelection (odd ? oddTaps : evenTaps, odd ? "Odd taps" : "Even taps");
        }
        else
        {
            const auto none = selection == allTaps;
            changeSelection (none ? 0 : allTaps, none ? "No taps" : "All taps");
        }

        return true;
    }

    if ((code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) && ! mods.isShiftDown())
    {
        // Still held from the last press: this one is a repeat.
        const auto continuing = heldArrow == code;
        heldArrow = code;
        heldArrowShifted = false;
        startTimerHz (30);

        if (onStepTimes != nullptr)
            onStepTimes (code == juce::KeyPress::upKey ? 1 : -1, continuing);

        return true;
    }

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        // Shift changes which setting moves, so a press with it changed isn't a repeat.
        const auto amount = mods.isShiftDown();
        const auto continuing = heldArrow == code && heldArrowShifted == amount;
        heldArrow = code;
        heldArrowShifted = amount;
        startTimerHz (30);

        if (onStepSmear != nullptr)
            onStepSmear (amount, code == juce::KeyPress::rightKey ? 1 : -1, continuing);

        return true;
    }

    if (code == 'F' || code == 'f')
    {
        // Ignore the repeats of a held key.
        if (heldFreezeKey != 0)
            return true;

        heldFreezeKey = code;
        startTimerHz (30);

        if (mods.isShiftDown())
        {
            if (onToggleFreeze != nullptr)
                onToggleFreeze();
        }
        else
        {
            holdingFreeze = true;

            if (onHoldFreeze != nullptr)
                onHoldFreeze (true);
        }

        return true;
    }

    if (code == 'G' || code == 'g')
    {
        if (heldGlitchKey != 0)
            return true;

        heldGlitchKey = code;
        startTimerHz (30);

        if (mods.isShiftDown())
        {
            if (onToggleGlitchStop != nullptr)
                onToggleGlitchStop();
        }
        else
        {
            holdingGlitchStop = true;

            if (onHoldGlitchStop != nullptr)
                onHoldGlitchStop (true);
        }

        return true;
    }

    if (code == 'T' || code == 't')
    {
        if (heldTapeKey != 0)
            return true;

        heldTapeKey = code;
        startTimerHz (30);

        if (mods.isShiftDown())
        {
            if (onToggleTapeStop != nullptr)
                onToggleTapeStop();
        }
        else
        {
            holdingTapeStop = true;

            if (onHoldTapeStop != nullptr)
                onHoldTapeStop (true);
        }

        return true;
    }

    return false;
}

bool PerformancePad::keyStateChanged (bool)
{
    updateHeldKeys();
    return false;
}

void PerformancePad::timerCallback()
{
    // Hosts don't always pass key releases on, so the held keys are also polled.
    updateHeldKeys();
}

void PerformancePad::updateHeldKeys()
{
    if (heldArrow != 0 && ! juce::KeyPress::isKeyCurrentlyDown (heldArrow))
        heldArrow = 0;

    if (heldFreezeKey != 0 && ! juce::KeyPress::isKeyCurrentlyDown (heldFreezeKey))
        releaseFreezeKey();

    if (heldGlitchKey != 0 && ! juce::KeyPress::isKeyCurrentlyDown (heldGlitchKey))
        releaseGlitchKey();

    if (heldTapeKey != 0 && ! juce::KeyPress::isKeyCurrentlyDown (heldTapeKey))
        releaseTapeKey();

    if (heldArrow == 0 && heldFreezeKey == 0 && heldGlitchKey == 0 && heldTapeKey == 0)
        stopTimer();
}

void PerformancePad::releaseFreezeKey()
{
    heldFreezeKey = 0;

    if (std::exchange (holdingFreeze, false) && onHoldFreeze != nullptr)
        onHoldFreeze (false);
}

void PerformancePad::releaseGlitchKey()
{
    heldGlitchKey = 0;

    if (std::exchange (holdingGlitchStop, false) && onHoldGlitchStop != nullptr)
        onHoldGlitchStop (false);
}

void PerformancePad::releaseTapeKey()
{
    heldTapeKey = 0;

    if (std::exchange (holdingTapeStop, false) && onHoldTapeStop != nullptr)
        onHoldTapeStop (false);
}

void PerformancePad::releaseHeldKeys()
{
    heldArrow = 0;
    stopTimer();
    releaseFreezeKey();
    releaseGlitchKey();
    releaseTapeKey();
}

void PerformancePad::focusLost (FocusChangeType)
{
    releaseHeldKeys();
}

//==============================================================================
juce::Rectangle<int> PerformancePad::cellArea() const
{
    return getLocalBounds().withTrimmedRight (statusWidth);
}

void PerformancePad::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu()) { showContextMenu(); return; }
    const auto area = cellArea();

    if (! area.contains (event.getPosition()) || area.getWidth() <= 0)
        return;

    const auto tap = juce::jlimit (0, params::numTaps - 1, (event.x - area.getX()) * params::numTaps / area.getWidth());
    const auto bit = 1u << tap;
    const auto nowSelected = (selection & bit) == 0;

    changeSelection (selection ^ bit, "Tap " + juce::String (tap + 1) + (nowSelected ? " selected" : " unselected"));
}

void PerformancePad::paint (juce::Graphics& g)
{
    const auto area = cellArea();
    const auto cellWidth = (float) area.getWidth() / (float) params::numTaps;

    g.setFont (juce::Font (juce::FontOptions (sizes::textHeight)));

    for (int t = 0; t < params::numTaps; ++t)
    {
        const auto bit = 1u << t;
        const auto selected = (selection & bit) != 0;
        const auto enabled = (enabledTaps & bit) != 0;

        const auto cell = juce::Rectangle<float> ((float) area.getX() + cellWidth * (float) t, (float) area.getY(),
                                                  cellWidth - (float) cellGap, (float) area.getHeight());

        // Filled when selected; the number is dimmed for a tap that is off.
        g.setColour (selected ? colours::sliderFill : colours::sliderTrack);
        g.fillRect (cell);
        g.setColour (selected ? colours::text : colours::outline);
        g.drawRect (cell, selected ? 2.0f : 1.0f);
        g.setColour (enabled ? colours::text : colours::outline);
        g.drawText (juce::String (t + 1), cell, juce::Justification::centred);
    }

    // Three lines, in type small enough for each to have one.
    auto status = getLocalBounds().removeFromRight (statusWidth);
    const auto lineHeight = status.getHeight() / 3;
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (sizes::textHeight, (float) lineHeight))));

    g.setColour (frozen ? colours::accent : colours::dimText);
    g.drawText (frozen ? "Freeze: on" : "Freeze: off", status.removeFromTop (lineHeight), juce::Justification::centred);

    g.setColour (glitchesStopped ? colours::accent : colours::dimText);
    g.drawText (glitchesStopped ? "Glitches: off" : "Glitches: on", status.removeFromTop (lineHeight), juce::Justification::centred);

    g.setColour (tapeStopped ? colours::accent : colours::dimText);
    g.drawText (tapeStopped ? "Tape: stopped" : "Tape: running", status, juce::Justification::centred);
}

std::unique_ptr<juce::AccessibilityHandler> PerformancePad::createAccessibilityHandler()
{
    // An image is the nearest role to a canvas that screen readers name on both platforms.
    juce::AccessibilityActions actions;
    actions.addAction (juce::AccessibilityActionType::showMenu, [this] { showContextMenu(); });
    return std::make_unique<HelpTagHandler> (*this, juce::AccessibilityRole::image, actions);
}

} // namespace astralay::ui
