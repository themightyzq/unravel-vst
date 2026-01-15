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
              public juce::AudioProcessorValueTreeState::Listener,
              private juce::AsyncUpdater
{
public:
    XYPad(juce::AudioProcessorValueTreeState& apvts);
    ~XYPad() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void visibilityChanged() override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    
    void timerCallback() override;
    
    // AudioProcessorValueTreeState::Listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // AsyncUpdater - for thread-safe parameter updates
    void handleAsyncUpdate() override;
    
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
    
    // dB range constants (matching ParameterDefinitions.h)
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 12.0f;
    static constexpr float kDbRange = kMaxDb - kMinDb;  // 72.0f
    static constexpr float kZeroDbNorm = (0.0f - kMinDb) / kDbRange;  // 0dB position in normalized space

    // Visual settings
    static constexpr float thumbSize = 20.0f;
    static constexpr float gridLineOpacity = 0.2f;
    static constexpr int gridDivisions = 8;
    
    // Animation
    float animationSpeed = 0.15f;
    bool isDragging = false;
    bool hasFocus_ = false;

    // Panning state (middle mouse button)
    bool isPanning_ = false;
    juce::Point<float> panStartCenter_;      // Zoom center when pan started
    juce::Point<float> panStartMouse_;       // Mouse position when pan started

    // Zoom state
    float zoomLevel_ = 1.0f;           // 1.0 = no zoom, 10.0 = max zoom
    float zoomCenterX_ = 0.5f;         // Zoom center in normalized coords
    float zoomCenterY_ = 0.5f;
    static constexpr float kMinZoom = 1.0f;
    static constexpr float kMaxZoom = 10.0f;
    static constexpr float kZoomStep = 0.5f;

    // Zoom control buttons
    juce::TextButton zoomInButton;
    juce::TextButton zoomOutButton;
    juce::TextButton zoomResetButton;

    // Minimap interaction
    juce::Rectangle<float> minimapBounds_;  // Cached for click detection

    // Hint text state (fades after first interaction)
    bool showHint_ = true;              // Show hint until first zoom/pan
    float hintAlpha_ = 1.0f;            // Fade-out alpha
    juce::int64 hintStartTime_ = 0;     // Time when component became visible
    static constexpr int kHintTimeoutMs = 10000;  // Fade hint after 10 seconds

    // Pan boundary feedback
    float panBoundaryFlash_ = 0.0f;     // Flash alpha when hitting boundary

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

    /**
     * Draw dark boundary fill outside parameter limits when zoomed.
     */
    void drawBoundaryFill(juce::Graphics& g);

    /**
     * Draw minimap showing current viewport position within full parameter space.
     */
    void drawMinimap(juce::Graphics& g);

    /**
     * Draw hint text for discoverability (fades after first use).
     */
    void drawHintText(juce::Graphics& g);

    /**
     * Draw axis labels for orientation.
     */
    void drawAxisLabels(juce::Graphics& g);

    /**
     * Draw pan boundary flash feedback.
     */
    void drawBoundaryFlash(juce::Graphics& g);

    /**
     * Zoom in centered on current view center.
     */
    void zoomIn();

    /**
     * Zoom out centered on current view center.
     */
    void zoomOut();

    /**
     * Reset zoom to 1x.
     */
    void resetZoom();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};