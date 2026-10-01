#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace astralay::ui
{

/** High-contrast colours: light text on a dark background, with a bright focus outline. */
namespace colours
{
    inline const juce::Colour background   { 0xff000000 };
    inline const juce::Colour panel        { 0xff141414 };
    inline const juce::Colour outline      { 0xff8a8a8a };
    inline const juce::Colour text         { 0xffffffff };
    inline const juce::Colour dimText      { 0xffd0d0d0 };
    inline const juce::Colour sliderTrack  { 0xff2a2a2a };
    inline const juce::Colour sliderFill   { 0xff1f6fb2 };
    inline const juce::Colour accent       { 0xffffd400 };   // Focus outline.
    inline const juce::Colour button       { 0xff2a2a2a };
}

/** Sizes in the editor's base coordinate space (scaled with the window). */
namespace sizes
{
    constexpr int baseWidth = 1200;
    constexpr int baseHeight = 976;
    constexpr float textHeight = 17.0f;
    constexpr float headingHeight = 19.0f;
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /** The value text drawn inside sliders. Hidden from screen readers, which read the value from
        the slider itself.
    */
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

} // namespace astralay::ui
