#pragma once

#include <JuceHeader.h>

/**
 * XY Pad component for intuitive 2D control.
 * X-axis: Tonal Gain
 * Y-axis: Noise Gain
 */
class XYPad : public juce::Component,
              public juce::SettableTooltipClient,
              public juce::Timer,
              public juce::AudioProcessorValueTreeState::Listener
{
public:
    XYPad(juce::AudioProcessorValueTreeState& apvts);
    ~XYPad() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    
    void timerCallback() override;
    
    // AudioProcessorValueTreeState::Listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    
    /**
     * Set the position from normalized values (0-1).
     */
    void setPosition(float xNorm, float yNorm);
    
    /**
     * Get current normalized position.
     */
    juce::Point<float> getNormalizedPosition() const { return currentPosition; }
    
private:
    juce::AudioProcessorValueTreeState& apvts;
    
    // Direct parameter pointers (no sliders needed)
    std::atomic<float>* tonalGainParameter;
    std::atomic<float>* noiseGainParameter;
    
    // Current position (normalized 0-1)
    juce::Point<float> currentPosition { 0.5f, 0.5f };
    juce::Point<float> targetPosition { 0.5f, 0.5f };
    
    // Visual settings
    static constexpr float thumbSize = 20.0f;
    static constexpr float gridLineOpacity = 0.2f;
    static constexpr int gridDivisions = 8;
    
    // Animation
    float animationSpeed = 0.15f;
    bool isDragging = false;
    
    // Colors
    juce::Colour backgroundColour;
    juce::Colour gridColour;
    juce::Colour thumbColour;
    juce::Colour thumbHighlightColour;
    juce::Colour tonalColour;
    juce::Colour noiseColour;
    juce::Colour textColour;
    
    /**
     * Convert screen coordinates to normalized position.
     */
    juce::Point<float> screenToNormalized(juce::Point<float> screenPos) const;
    
    /**
     * Convert normalized position to screen coordinates.
     */
    juce::Point<float> normalizedToScreen(juce::Point<float> normPos) const;
    
    /**
     * Update parameter values from current position.
     */
    void updateParameters();
    
    /**
     * Draw the background grid.
     */
    void drawGrid(juce::Graphics& g);
    
    /**
     * Draw axis labels.
     */
    void drawLabels(juce::Graphics& g);
    
    /**
     * Draw the position indicator/thumb.
     */
    void drawThumb(juce::Graphics& g);
    
    /**
     * Draw value readout.
     */
    void drawValueReadout(juce::Graphics& g);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};