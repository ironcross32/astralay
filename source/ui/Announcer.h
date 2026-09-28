#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace astralay::ui
{

/** Speaks messages through the user's screen reader.

    On Windows, JUCE's AccessibilityHandler::postAnnouncement speaks through SAPI (the system voice)
    rather than NVDA, JAWS or Narrator. This class instead raises UI Automation notification events
    from a hidden child window of the editor, which screen readers speak in their own voice. It falls
    back to postAnnouncement if UI Automation notifications aren't available (Windows before 10
    version 1709). On other platforms it uses postAnnouncement, which already reaches VoiceOver.
*/
class Announcer final
{
public:
    /** window is the component whose native window hosts the announcements (the editor). */
    explicit Announcer (juce::Component& window);
    ~Announcer();

    /** Speaks text. When interrupt is true, it cuts off whatever the screen reader is saying. */
    void announce (const juce::String& text, bool interrupt = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE (Announcer)
};

/** Implemented by the editor so any control inside it can make announcements. */
class AnnouncementTarget
{
public:
    virtual ~AnnouncementTarget() = default;
    virtual void announce (const juce::String& text) = 0;
};

/** Announces through the nearest AnnouncementTarget above the component (or through JUCE's
    fallback if there is none).
*/
void announceFrom (juce::Component& source, const juce::String& text);

} // namespace astralay::ui
