#pragma once

#include <JuceHeader.h>

class VerticalSlider : public juce::Slider
{
public:
    VerticalSlider();
    ~VerticalSlider() override;
    
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    
private:
    bool isDragging = false;
    float lastY = 0.0f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalSlider)
};