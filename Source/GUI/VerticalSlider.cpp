#include "VerticalSlider.h"

VerticalSlider::VerticalSlider()
{
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(-60.0, 6.0, 0.1);
    setValue(0.0);
}

VerticalSlider::~VerticalSlider() = default;

void VerticalSlider::paint(juce::Graphics& g)
{
    // Let the parent class handle the basic drawing
    juce::Slider::paint(g);
    
    // Add custom tick marks
    auto bounds = getLocalBounds();
    float x = bounds.getCentreX();
    
    g.setColour(juce::Colour(0x40FFFFFF));
    
    // Draw tick marks at important dB values
    const float dbValues[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    
    for (float db : dbValues)
    {
        if (db >= getMinimum() && db <= getMaximum())
        {
            float y = valueToProportionOfLength(db) * bounds.getHeight();
            y = bounds.getHeight() - y; // Invert for vertical slider
            
            g.drawHorizontalLine(static_cast<int>(y), x - 10, x - 5);
            g.drawHorizontalLine(static_cast<int>(y), x + 5, x + 10);
        }
    }
}

void VerticalSlider::mouseDown(const juce::MouseEvent& event)
{
    isDragging = true;
    lastY = event.position.y;
    juce::Slider::mouseDown(event);
}

void VerticalSlider::mouseDrag(const juce::MouseEvent& event)
{
    if (isDragging)
    {
        // Add fine control when shift is held
        float sensitivity = event.mods.isShiftDown() ? 0.25f : 1.0f;
        float deltaY = (event.position.y - lastY) * sensitivity;
        
        // Calculate new value
        float currentProportion = valueToProportionOfLength(getValue());
        float newProportion = currentProportion - (deltaY / getHeight());
        newProportion = juce::jlimit(0.0f, 1.0f, newProportion);
        
        setValue(proportionOfLengthToValue(newProportion));
        
        if (!event.mods.isShiftDown())
        {
            lastY = event.position.y;
        }
    }
}