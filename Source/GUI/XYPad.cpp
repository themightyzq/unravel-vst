#include "XYPad.h"
#include "../Parameters/ParameterDefinitions.h"

XYPad::XYPad(juce::AudioProcessorValueTreeState& apvts_)
    : apvts(apvts_)
{
    // Setup colors
    backgroundColour = juce::Colour(0xff1a1a1a);
    gridColour = juce::Colour(0xff404040);
    thumbColour = juce::Colour(0xff00ffaa);
    thumbHighlightColour = juce::Colour(0xff00ffdd);
    tonalColour = juce::Colour(0xff4488ff);
    noiseColour = juce::Colour(0xffff8844);
    textColour = juce::Colour(0xffdddddd);
    
    // Get direct parameter pointers
    tonalGainParameter = apvts.getRawParameterValue(ParameterIDs::tonalGain);
    noiseGainParameter = apvts.getRawParameterValue(ParameterIDs::noisyGain);
    
    // Set initial position from parameters
    auto* tonalParam = tonalGainParameter;
    auto* noiseParam = noiseGainParameter;
    
    if (tonalParam && noiseParam)
    {
        // Convert from dB (-60 to +12) to normalized (0 to 1)
        float tonalDb = tonalParam->load();
        float noiseDb = noiseParam->load();
        
        float tonalNorm = (tonalDb + 60.0f) / 72.0f;  // Map -60..+12 to 0..1
        float noiseNorm = (noiseDb + 60.0f) / 72.0f;
        
        currentPosition = { tonalNorm, 1.0f - noiseNorm };  // Y inverted for UI
        targetPosition = currentPosition;
    }
    
    // Set up parameter listeners
    apvts.addParameterListener(ParameterIDs::tonalGain, this);
    apvts.addParameterListener(ParameterIDs::noisyGain, this);
    
    // Start animation timer
    startTimerHz(60);
}

XYPad::~XYPad()
{
    stopTimer();
    
    // Remove parameter listeners
    apvts.removeParameterListener(ParameterIDs::tonalGain, this);
    apvts.removeParameterListener(ParameterIDs::noisyGain, this);
}

void XYPad::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(backgroundColour);
    
    // Draw grid
    drawGrid(g);
    
    // Draw gradient overlay to show tonal/noise regions
    auto bounds = getLocalBounds().toFloat();
    
    // Tonal gradient (left to right)
    juce::ColourGradient tonalGradient(
        tonalColour.withAlpha(0.0f), bounds.getX(), bounds.getCentreY(),
        tonalColour.withAlpha(0.3f), bounds.getRight(), bounds.getCentreY(),
        false);
    g.setGradientFill(tonalGradient);
    g.fillRect(bounds);
    
    // Noise gradient (top to bottom)
    juce::ColourGradient noiseGradient(
        noiseColour.withAlpha(0.3f), bounds.getCentreX(), bounds.getY(),
        noiseColour.withAlpha(0.0f), bounds.getCentreX(), bounds.getBottom(),
        false);
    g.setGradientFill(noiseGradient);
    g.fillRect(bounds);
    
    // Draw labels
    drawLabels(g);
    
    // Draw thumb
    drawThumb(g);
    
    // Draw value readout
    drawValueReadout(g);
    
    // Border
    g.setColour(gridColour);
    g.drawRect(getLocalBounds(), 1);
}

void XYPad::resized()
{
    // Nothing specific to resize
}

void XYPad::mouseDown(const juce::MouseEvent& event)
{
    isDragging = true;
    
    // Begin gesture for both parameters
    if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
        tonalParam->beginChangeGesture();
    
    if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
        noiseParam->beginChangeGesture();
    
    auto normPos = screenToNormalized(event.position);
    targetPosition = normPos;
    
    updateParameters();
}

void XYPad::mouseDrag(const juce::MouseEvent& event)
{
    if (isDragging)
    {
        auto normPos = screenToNormalized(event.position);
        targetPosition = normPos;
        
        updateParameters();
    }
}

void XYPad::mouseUp(const juce::MouseEvent& event)
{
    isDragging = false;
    
    // End gesture for both parameters to ensure proper automation
    if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
        tonalParam->endChangeGesture();
    
    if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
        noiseParam->endChangeGesture();
}

void XYPad::timerCallback()
{
    // Smooth animation
    if (!isDragging)
    {
        currentPosition = currentPosition + (targetPosition - currentPosition) * animationSpeed;
    }
    else
    {
        currentPosition = targetPosition;
    }
    
    repaint();
}

void XYPad::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == ParameterIDs::tonalGain || parameterID == ParameterIDs::noisyGain)
    {
        // Update position from parameters (only if not currently dragging)
        if (!isDragging)
        {
            float tonalDb = tonalGainParameter ? tonalGainParameter->load() : 0.0f;
            float noiseDb = noiseGainParameter ? noiseGainParameter->load() : 0.0f;
            
            // Convert from dB to normalized
            float tonalNorm = (tonalDb + 60.0f) / 72.0f;
            float noiseNorm = (noiseDb + 60.0f) / 72.0f;
            
            // Clamp to valid range
            tonalNorm = juce::jlimit(0.0f, 1.0f, tonalNorm);
            noiseNorm = juce::jlimit(0.0f, 1.0f, noiseNorm);
            
            targetPosition = { tonalNorm, 1.0f - noiseNorm }; // Y inverted for UI
        }
    }
}

void XYPad::setPosition(float xNorm, float yNorm)
{
    targetPosition = { juce::jlimit(0.0f, 1.0f, xNorm), 
                      juce::jlimit(0.0f, 1.0f, yNorm) };
    updateParameters();
}

juce::Point<float> XYPad::screenToNormalized(juce::Point<float> screenPos) const
{
    auto bounds = getLocalBounds().toFloat();
    
    float xNorm = (screenPos.x - bounds.getX()) / bounds.getWidth();
    float yNorm = (screenPos.y - bounds.getY()) / bounds.getHeight();
    
    return { juce::jlimit(0.0f, 1.0f, xNorm), 
            juce::jlimit(0.0f, 1.0f, yNorm) };
}

juce::Point<float> XYPad::normalizedToScreen(juce::Point<float> normPos) const
{
    auto bounds = getLocalBounds().toFloat();
    
    float x = bounds.getX() + normPos.x * bounds.getWidth();
    float y = bounds.getY() + normPos.y * bounds.getHeight();
    
    return { x, y };
}

void XYPad::updateParameters()
{
    // Convert normalized position to dB values
    float tonalDb = -60.0f + targetPosition.x * 72.0f;  // 0..1 to -60..+12
    float noiseDb = -60.0f + (1.0f - targetPosition.y) * 72.0f;  // Y inverted
    
    // Clamp to parameter ranges
    tonalDb = juce::jlimit(-60.0f, 12.0f, tonalDb);
    noiseDb = juce::jlimit(-60.0f, 12.0f, noiseDb);
    
    // Update parameters directly (gesture handling is done in mouse events)
    if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
    {
        const auto& range = tonalParam->getNormalisableRange();
        float normalizedValue = range.convertTo0to1(tonalDb);
        tonalParam->setValueNotifyingHost(normalizedValue);
    }
    
    if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
    {
        const auto& range = noiseParam->getNormalisableRange();
        float normalizedValue = range.convertTo0to1(noiseDb);
        noiseParam->setValueNotifyingHost(normalizedValue);
    }
}

void XYPad::drawGrid(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    g.setColour(gridColour.withAlpha(gridLineOpacity));
    
    // Vertical lines
    for (int i = 1; i < gridDivisions; ++i)
    {
        float x = bounds.getX() + (bounds.getWidth() * i / gridDivisions);
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 0.5f);
    }
    
    // Horizontal lines
    for (int i = 1; i < gridDivisions; ++i)
    {
        float y = bounds.getY() + (bounds.getHeight() * i / gridDivisions);
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 0.5f);
    }
    
    // Center lines (thicker)
    g.setColour(gridColour.withAlpha(gridLineOpacity * 2));
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();
    g.drawLine(centerX, bounds.getY(), centerX, bounds.getBottom(), 1.0f);
    g.drawLine(bounds.getX(), centerY, bounds.getRight(), centerY, 1.0f);
}

void XYPad::drawLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setFont(juce::Font(9.0f));

    // Corner labels only - clean and non-overlapping
    // Top-left: Low Tonal, High Noise (noise only)
    g.setColour(noiseColour.withAlpha(0.7f));
    g.drawText("Noise Only",
               bounds.getX() + 6, bounds.getY() + 6,
               70, 14,
               juce::Justification::left);

    // Top-right: High Tonal, High Noise (full mix)
    g.setColour(textColour.withAlpha(0.5f));
    g.drawText("Full Mix",
               bounds.getRight() - 56, bounds.getY() + 6,
               50, 14,
               juce::Justification::right);

    // Bottom-left: Low Tonal, Low Noise (silent)
    g.setColour(textColour.withAlpha(0.4f));
    g.drawText("Silent",
               bounds.getX() + 6, bounds.getBottom() - 40,
               40, 14,
               juce::Justification::left);

    // Bottom-right: High Tonal, Low Noise (tonal only)
    g.setColour(tonalColour.withAlpha(0.7f));
    g.drawText("Tonal Only",
               bounds.getRight() - 66, bounds.getBottom() - 40,
               60, 14,
               juce::Justification::right);
}

void XYPad::drawThumb(juce::Graphics& g)
{
    auto screenPos = normalizedToScreen(currentPosition);
    
    // Shadow
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillEllipse(screenPos.x - thumbSize/2 + 2, 
                  screenPos.y - thumbSize/2 + 2, 
                  thumbSize, thumbSize);
    
    // Outer ring
    g.setColour(isDragging ? thumbHighlightColour : thumbColour);
    g.drawEllipse(screenPos.x - thumbSize/2, 
                  screenPos.y - thumbSize/2, 
                  thumbSize, thumbSize, 2.0f);
    
    // Inner dot
    g.setColour(isDragging ? thumbHighlightColour : thumbColour);
    g.fillEllipse(screenPos.x - 4, screenPos.y - 4, 8, 8);
    
    // Crosshair lines
    g.setColour(thumbColour.withAlpha(0.3f));
    auto bounds = getLocalBounds().toFloat();
    
    // Vertical line
    g.drawLine(screenPos.x, bounds.getY(), screenPos.x, screenPos.y - thumbSize/2, 0.5f);
    g.drawLine(screenPos.x, screenPos.y + thumbSize/2, screenPos.x, bounds.getBottom(), 0.5f);
    
    // Horizontal line
    g.drawLine(bounds.getX(), screenPos.y, screenPos.x - thumbSize/2, screenPos.y, 0.5f);
    g.drawLine(screenPos.x + thumbSize/2, screenPos.y, bounds.getRight(), screenPos.y, 0.5f);
}

void XYPad::drawValueReadout(juce::Graphics& g)
{
    // Convert position to dB values for display
    float tonalDb = -60.0f + currentPosition.x * 72.0f;
    float noiseDb = -60.0f + (1.0f - currentPosition.y) * 72.0f;

    // Format strings
    juce::String tonalStr = tonalDb <= -59.9f ? "-inf" : juce::String(tonalDb, 1) + " dB";
    juce::String noiseStr = noiseDb <= -59.9f ? "-inf" : juce::String(noiseDb, 1) + " dB";

    // Draw background box - bottom center
    auto bounds = getLocalBounds().toFloat();
    const float boxWidth = 160.0f;
    const float boxHeight = 24.0f;
    juce::Rectangle<float> readoutBox(
        bounds.getCentreX() - boxWidth / 2.0f,
        bounds.getBottom() - boxHeight - 8.0f,
        boxWidth,
        boxHeight
    );

    g.setColour(backgroundColour.withAlpha(0.9f));
    g.fillRoundedRectangle(readoutBox, 4.0f);

    g.setColour(gridColour);
    g.drawRoundedRectangle(readoutBox, 4.0f, 1.0f);

    // Draw values side by side
    g.setFont(juce::Font(10.0f));

    g.setColour(tonalColour);
    g.drawText("Tonal: " + tonalStr,
               readoutBox.getX() + 6, readoutBox.getY() + 4,
               75, 16,
               juce::Justification::left);

    g.setColour(noiseColour);
    g.drawText("Noise: " + noiseStr,
               readoutBox.getX() + 82, readoutBox.getY() + 4,
               75, 16,
               juce::Justification::left);
}