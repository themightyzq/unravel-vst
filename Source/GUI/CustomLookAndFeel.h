#pragma once

#include <JuceHeader.h>
#include "Theme.h"

/**
 * CustomLookAndFeel — Unravel's themed look, drawn from the shared Theme tokens.
 *
 * Replaces JUCE's stock control rendering so the rotary knobs, buttons, and the
 * preset combo box match the bespoke XY pad / spectrum aesthetic instead of the
 * framework defaults. Applied once on the editor (cascades to all child controls).
 */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

    // Keyboard-focus indicator. Any control that calls setHasFocusOutline(true)
    // gets this drawn around it while it holds keyboard focus (Desktop drives it
    // via createFocusOutlineForComponent on every focus change). We draw a 2px
    // Theme::accent ring so keyboard users can see where focus is (D-8/R10).
    std::unique_ptr<juce::FocusOutline> createFocusOutlineForComponent(juce::Component&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomLookAndFeel)
};
