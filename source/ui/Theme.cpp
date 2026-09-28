#include "Theme.h"

namespace astralay::ui
{

namespace
{
    juce::Font textFont()
    {
        return juce::Font (juce::FontOptions (sizes::textHeight));
    }
}

LookAndFeel::LookAndFeel()
{
    setColourScheme ({ colours::panel, colours::background, colours::panel, colours::outline,
                       colours::text, colours::button, colours::text, colours::sliderFill, colours::text });

    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId, colours::sliderFill);
    setColour (juce::Slider::backgroundColourId, colours::sliderTrack);
    setColour (juce::ComboBox::backgroundColourId, colours::button);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::arrowColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::button);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, colours::outline);
    setColour (juce::TextEditor::backgroundColourId, colours::background);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::accent);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::sliderFill);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panel);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::outline);
}

juce::Font LookAndFeel::getLabelFont (juce::Label&)                 { return textFont(); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)           { return textFont(); }
juce::Font LookAndFeel::getPopupMenuFont()                          { return textFont(); }
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int)  { return textFont(); }

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setAccessible (false);
    label->setWantsKeyboardFocus (false);
    label->setInterceptsMouseClicks (false, false);
    label->setFont (textFont());
    return label;
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                    float sliderPos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    g.setColour (colours::sliderTrack);
    g.fillRect (bounds);

    g.setColour (colours::sliderFill);
    g.fillRect (bounds.withRight (sliderPos));

    g.setColour (colours::outline);
    g.drawRect (bounds, 1.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (bounds.getHeight() - 6.0f, 20.0f);
    const auto box = juce::Rectangle<float> (4.0f, (bounds.getHeight() - boxSize) * 0.5f, boxSize, boxSize);

    g.setColour (colours::outline);
    g.drawRect (box, 2.0f);

    if (button.getToggleState())
    {
        g.setColour (colours::accent);
        g.fillRect (box.reduced (4.0f));
    }

    g.setColour (colours::text);
    g.setFont (textFont());
    g.drawFittedText (button.getButtonText() + (button.getToggleState() ? ": on" : ": off"),
                      bounds.withTrimmedLeft (box.getRight() + 8.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

} // namespace astralay::ui
