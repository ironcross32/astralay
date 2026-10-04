#include "KeyLayer.h"
#include "Announcer.h"

namespace astralay::ui
{

int digitForKey (const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return -1;

    const auto code = key.getKeyCode();

    if (code >= '0' && code <= '9')
        return code - '0';

    static const std::array<int, 10> numberPad { juce::KeyPress::numberPad0, juce::KeyPress::numberPad1,
                                                 juce::KeyPress::numberPad2, juce::KeyPress::numberPad3,
                                                 juce::KeyPress::numberPad4, juce::KeyPress::numberPad5,
                                                 juce::KeyPress::numberPad6, juce::KeyPress::numberPad7,
                                                 juce::KeyPress::numberPad8, juce::KeyPress::numberPad9 };

    for (int digit = 0; digit < (int) numberPad.size(); ++digit)
        if (code == numberPad[(size_t) digit])
            return digit;

    return -1;
}

//==============================================================================
KeyLayer::KeyLayer (Announcer& announcerToUse)
    : announcer (announcerToUse)
{
}

KeyLayer::~KeyLayer()
{
    close();
}

void KeyLayer::open (const juce::KeyPress& opener, const juce::String& prompt, Handler handlerToUse, int timeoutMs)
{
    close();

    handler = std::move (handlerToUse);
    openedBy = opener;

    // A listener on the focused control is asked before the control itself, so keys the control
    // would otherwise use, such as the arrows on a slider, come here instead.
    listeningOn = juce::Component::getCurrentlyFocusedComponent();

    if (listeningOn != nullptr)
        listeningOn->addKeyListener (this);

    juce::Desktop::getInstance().addFocusChangeListener (this);
    startTimer (timeoutMs);

    announcer.announce (prompt);
}

void KeyLayer::close()
{
    if (! isOpen())
        return;

    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener (this);

    if (listeningOn != nullptr)
        listeningOn->removeKeyListener (this);

    listeningOn = nullptr;
    handler = nullptr;
}

bool KeyLayer::handleKey (const juce::KeyPress& key)
{
    if (! isOpen())
        return false;

    if (key == openedBy)
        return true;

    // Closed first, so that the handler can open another layer.
    const auto used = handler;
    close();

    if (! used (key))
        announcer.announce (cancelMessage());

    return true;
}

bool KeyLayer::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return handleKey (key);
}

void KeyLayer::globalFocusChanged (juce::Component* focused)
{
    if (focused != listeningOn.getComponent())
        close();
}

void KeyLayer::timerCallback()
{
    close();
}

} // namespace astralay::ui
