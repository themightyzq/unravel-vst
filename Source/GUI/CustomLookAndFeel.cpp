#include "CustomLookAndFeel.h"

CustomLookAndFeel::CustomLookAndFeel()
{
    // Set default colours
    setColour(juce::Slider::backgroundColourId, trackColour);
    setColour(juce::Slider::thumbColourId, thumbColour);
    setColour(juce::Slider::trackColourId, primaryColour);
    
    setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickColourId, primaryColour);
}

CustomLookAndFeel::~CustomLookAndFeel() = default;

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // Draw track background
        float trackWidth = 6.0f;
        float trackX = x + width * 0.5f - trackWidth * 0.5f;
        
        g.setColour(trackColour);
        g.fillRoundedRectangle(trackX, y, trackWidth, height, trackWidth * 0.5f);
        
        // Draw filled portion (from bottom to thumb position)
        float fillHeight = height - sliderPos + y;
        g.setColour(slider.isEnabled() ? primaryColour : primaryColour.withAlpha(0.5f));
        g.fillRoundedRectangle(trackX, sliderPos, trackWidth, fillHeight, trackWidth * 0.5f);
        
        // Draw thumb
        float thumbSize = 20.0f;
        g.setColour(thumbColour);
        g.fillEllipse(x + width * 0.5f - thumbSize * 0.5f, 
                     sliderPos - thumbSize * 0.5f, 
                     thumbSize, thumbSize);
        
        // Draw thumb inner circle
        g.setColour(primaryColour);
        g.fillEllipse(x + width * 0.5f - thumbSize * 0.3f, 
                     sliderPos - thumbSize * 0.3f, 
                     thumbSize * 0.6f, thumbSize * 0.6f);
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        // Draw track background
        float trackHeight = 6.0f;
        float trackY = y + height * 0.5f - trackHeight * 0.5f;
        
        g.setColour(trackColour);
        g.fillRoundedRectangle(x, trackY, width, trackHeight, trackHeight * 0.5f);
        
        // Draw filled portion (from left to thumb position)
        float fillWidth = sliderPos - x;
        g.setColour(slider.isEnabled() ? primaryColour : primaryColour.withAlpha(0.5f));
        g.fillRoundedRectangle(x, trackY, fillWidth, trackHeight, trackHeight * 0.5f);
        
        // Draw thumb
        float thumbSize = 16.0f;
        g.setColour(thumbColour);
        g.fillEllipse(sliderPos - thumbSize * 0.5f,
                     y + height * 0.5f - thumbSize * 0.5f,
                     thumbSize, thumbSize);
        
        // Draw thumb inner circle
        g.setColour(trackColour);
        g.fillEllipse(sliderPos - thumbSize * 0.25f,
                     y + height * 0.5f - thumbSize * 0.25f,
                     thumbSize * 0.5f, thumbSize * 0.5f);
    }
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds();
    
    // Draw checkbox
    auto checkboxBounds = bounds.removeFromLeft(20).reduced(2);
    
    g.setColour(trackColour);
    g.fillRoundedRectangle(checkboxBounds.toFloat(), 2.0f);
    
    g.setColour(button.getToggleState() ? primaryColour : juce::Colour(0xFF606060));
    g.drawRoundedRectangle(checkboxBounds.toFloat(), 2.0f, 2.0f);
    
    if (button.getToggleState())
    {
        // Draw checkmark
        juce::Path tick;
        tick.startNewSubPath(checkboxBounds.getX() + 3.0f, checkboxBounds.getCentreY());
        tick.lineTo(checkboxBounds.getX() + checkboxBounds.getWidth() * 0.4f, 
                   checkboxBounds.getBottom() - 4.0f);
        tick.lineTo(checkboxBounds.getRight() - 3.0f, checkboxBounds.getY() + 4.0f);
        
        g.setColour(primaryColour);
        g.strokePath(tick, juce::PathStrokeType(2.5f));
    }
    
    // Draw text
    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(14.0f);
    g.drawText(button.getButtonText(), bounds.reduced(5, 0), 
              juce::Justification::centredLeft, true);
}