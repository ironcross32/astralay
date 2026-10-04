#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace astralay::ui
{

class Announcer;

/** The digit a key types, 0 to 9, from the number row or the number pad. Returns -1 for any other
    key and for a digit pressed with a modifier.
*/
int digitForKey (const juce::KeyPress& key);

/** A layered keystroke: one shortcut opens the layer, which speaks a prompt and then takes the
    next key, whatever it is and whichever control has focus.

    The owner calls open when its shortcut is pressed, giving a handler for the key that follows.
    The handler returns true if the key is one of the layer's, having done and announced whatever
    it stands for; for any other key it returns false and the layer announces "Canceled". Either
    way the key is used up and the layer closes. It also closes, saying nothing, if no key comes
    within the timeout or focus moves.

    While open, the layer listens on the focused control, so it sees the key before the control
    does. The owner should also pass its own key presses to handleKey first, which covers keys
    arriving while nothing has focus.
*/
class KeyLayer final : private juce::KeyListener,
                       private juce::FocusChangeListener,
                       private juce::Timer
{
public:
    using Handler = std::function<bool (const juce::KeyPress&)>;

    explicit KeyLayer (Announcer& announcerToUse);
    ~KeyLayer() override;

    /** Opens the layer, replacing any that is open. opener is the shortcut that opened it:
        repeats of it, from the key being held, are ignored rather than taken as the next key.
    */
    void open (const juce::KeyPress& opener, const juce::String& prompt, Handler handler,
               int timeoutMs = defaultTimeoutMs);

    /** Closes the layer without announcing anything. */
    void close();

    bool isOpen() const { return handler != nullptr; }

    /** Gives the layer a key. Returns false, leaving the key alone, unless the layer is open. */
    bool handleKey (const juce::KeyPress& key);

    static juce::String cancelMessage() { return "Canceled"; }

    static constexpr int defaultTimeoutMs = 2000;

private:
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
    void globalFocusChanged (juce::Component* focused) override;
    void timerCallback() override;

    Announcer& announcer;
    Handler handler;
    juce::KeyPress openedBy;
    juce::Component::SafePointer<juce::Component> listeningOn;

    JUCE_DECLARE_NON_COPYABLE (KeyLayer)
};

} // namespace astralay::ui
