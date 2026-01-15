#include "XYPad.h"
#include "../Parameters/ParameterDefinitions.h"

XYPad::XYPad(juce::AudioProcessorValueTreeState& apvts_)
    : apvts(apvts_)
{
    // Setup colors - using saturated, high-contrast colors for accessibility
    backgroundColour = juce::Colour(0xff1a1a1a);
    gridColour = juce::Colour(0xff404040);
    thumbColour = juce::Colour(0xff00ffaa);
    thumbHighlightColour = juce::Colour(0xff00ffdd);
    tonalColour = juce::Colour(0xff0088ff);   // Vivid blue for colorblind accessibility
    noiseColour = juce::Colour(0xffff5500);   // Vivid orange for colorblind accessibility
    textColour = juce::Colour(0xffdddddd);
    
    // Get direct parameter pointers
    tonalGainParameter = apvts.getRawParameterValue(ParameterIDs::tonalGain);
    noiseGainParameter = apvts.getRawParameterValue(ParameterIDs::noisyGain);
    
    // Set initial position from parameters with null safety
    if (tonalGainParameter != nullptr && noiseGainParameter != nullptr)
    {
        // Convert from dB to normalized using class constants
        float tonalDb = tonalGainParameter->load();
        float noiseDb = noiseGainParameter->load();

        float tonalNorm = (tonalDb - kMinDb) / kDbRange;
        float noiseNorm = (noiseDb - kMinDb) / kDbRange;

        currentPosition = { tonalNorm, 1.0f - noiseNorm };  // Y inverted for UI
        targetPosition = currentPosition;
    }
    else
    {
        // Fallback to 0dB default if parameters not available
        jassertfalse;  // Parameters should always exist
        currentPosition = { kZeroDbNorm, 1.0f - kZeroDbNorm };
        targetPosition = currentPosition;
    }
    
    // Set up parameter listeners
    apvts.addParameterListener(ParameterIDs::tonalGain, this);
    apvts.addParameterListener(ParameterIDs::noisyGain, this);

    // Set up zoom control buttons
    auto setupZoomButton = [this](juce::TextButton& btn, const juce::String& text, const juce::String& tooltip) {
        btn.setButtonText(text);
        btn.setColour(juce::TextButton::buttonColourId, backgroundColour.darker(0.2f));
        btn.setColour(juce::TextButton::textColourOffId, thumbColour);
        btn.setTooltip(tooltip);
        addAndMakeVisible(btn);
    };

    setupZoomButton(zoomInButton, "+", "Zoom In: Increase magnification for fine control");
    setupZoomButton(zoomOutButton, "-", "Zoom Out: Decrease magnification");
    setupZoomButton(zoomResetButton, "1x", "Reset Zoom: Return to full view");

    zoomInButton.onClick = [this]() { zoomIn(); };
    zoomOutButton.onClick = [this]() { zoomOut(); };
    zoomResetButton.onClick = [this]() { resetZoom(); };

    // Enable keyboard focus for accessibility
    setWantsKeyboardFocus(true);
    setAccessible(true);
    setTitle("Mix Control XY Pad");
    setDescription("2D control for Tonal and Noise gain. Horizontal = Tonal gain, Vertical = Noise gain. "
                   "Use arrow keys to adjust, Home to reset to 0dB.");

    // Set tooltip for discoverability
    setTooltip("Drag to adjust mix. Scroll to zoom for fine control. Middle-click+drag to pan when zoomed.");

    // Initialize hint timing
    hintStartTime_ = juce::Time::currentTimeMillis();

    // Start animation timer
    startTimerHz(60);
}

XYPad::~XYPad()
{
    // Clear button callbacks before destruction to prevent dangling this pointer
    zoomInButton.onClick = nullptr;
    zoomOutButton.onClick = nullptr;
    zoomResetButton.onClick = nullptr;

    stopTimer();

    // Remove parameter listeners
    apvts.removeParameterListener(ParameterIDs::tonalGain, this);
    apvts.removeParameterListener(ParameterIDs::noisyGain, this);
}

void XYPad::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(backgroundColour);

    // Draw dark boundary fill outside parameter limits when zoomed
    drawBoundaryFill(g);

    // Draw grid with major/minor hierarchy
    drawGrid(g);

    // Gradient overlays for visual orientation
    // Using saturated colors with clear axis indicator bars
    auto bounds = getLocalBounds().toFloat();

    // High-saturation colors for accessibility
    juce::Colour tonalHigh(0xff0088ff);   // Vivid blue
    juce::Colour noiseHigh(0xffff5500);   // Vivid orange

    // Strong overlapping gradients - clearly visible even when blended
    // Tonal gradient (left to right) - blue builds toward right
    juce::ColourGradient tonalGradient(
        tonalHigh.withAlpha(0.0f), bounds.getX(), bounds.getCentreY(),
        tonalHigh.withAlpha(0.4f), bounds.getRight(), bounds.getCentreY(),
        false);
    g.setGradientFill(tonalGradient);
    g.fillRect(bounds);

    // Noise gradient (bottom to top) - orange builds toward top
    juce::ColourGradient noiseGradient(
        noiseHigh.withAlpha(0.0f), bounds.getCentreX(), bounds.getBottom(),
        noiseHigh.withAlpha(0.4f), bounds.getCentreX(), bounds.getY(),
        false);
    g.setGradientFill(noiseGradient);
    g.fillRect(bounds);

    // Solid axis indicator bars - always visible "legend" for the axes
    const float barWidth = 6.0f;

    // Right edge bar - solid blue (Tonal axis indicator)
    g.setColour(tonalHigh.withAlpha(0.85f));
    g.fillRect(juce::Rectangle<float>(bounds.getRight() - barWidth, bounds.getY(),
                                        barWidth, bounds.getHeight()));

    // Top edge bar - solid orange (Noise axis indicator)
    g.setColour(noiseHigh.withAlpha(0.85f));
    g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(),
                                        bounds.getWidth(), barWidth));

    // Small corner labels on the bars for extra clarity
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));

    // "T" label on blue bar (Tonal)
    g.setColour(juce::Colours::white);
    g.drawText("T", juce::Rectangle<float>(bounds.getRight() - barWidth - 1, bounds.getCentreY() - 6,
                                            barWidth + 2, 12), juce::Justification::centred, false);

    // "N" label on orange bar (Noise)
    g.drawText("N", juce::Rectangle<float>(bounds.getCentreX() - 6, bounds.getY(),
                                            12, barWidth + 2), juce::Justification::centred, false);

    // Draw labels
    drawLabels(g);
    
    // Draw thumb
    drawThumb(g);
    
    // Draw value readout
    drawValueReadout(g);

    // Draw minimap when zoomed
    drawMinimap(g);

    // Draw zoom indicator when zoomed in
    if (zoomLevel_ > 1.0f)
    {
        g.setColour(thumbColour.withAlpha(0.8f));
        g.setFont(juce::FontOptions(11.0f));
        juce::String zoomText = juce::String(zoomLevel_, 1) + "x";
        g.drawText(zoomText,
                   bounds.getRight() - 70, bounds.getY() + 6,  // Moved left to avoid buttons
                   34, 14,
                   juce::Justification::right);
    }

    // Draw axis labels
    drawAxisLabels(g);

    // Draw hint text (fades after first use)
    drawHintText(g);

    // Draw boundary flash when panning hits edge
    drawBoundaryFlash(g);

    // Border
    g.setColour(gridColour);
    g.drawRect(getLocalBounds(), 1);

    // Focus ring for accessibility
    if (hasFocus_)
    {
        g.setColour(thumbColour.withAlpha(0.6f));
        g.drawRect(getLocalBounds().reduced(2), 3);  // Increased from 2 to 3 for better visibility
    }
}

void XYPad::resized()
{
    // Position zoom control buttons on right side, vertically stacked
    const int buttonSize = 24;
    const int buttonSpacing = 2;
    const int margin = 6;

    auto bounds = getLocalBounds();

    // Stack buttons vertically on right edge: [+] [-] [1x]
    int x = bounds.getRight() - margin - buttonSize;
    int y = bounds.getCentreY() - (buttonSize * 3 + buttonSpacing * 2) / 2;  // Centered vertically

    zoomInButton.setBounds(x, y, buttonSize, buttonSize);
    y += buttonSize + buttonSpacing;
    zoomOutButton.setBounds(x, y, buttonSize, buttonSize);
    y += buttonSize + buttonSpacing;
    zoomResetButton.setBounds(x, y, buttonSize, buttonSize);
}

void XYPad::mouseDown(const juce::MouseEvent& event)
{
    // Check for minimap click (navigate to clicked position)
    if (!minimapBounds_.isEmpty() && minimapBounds_.contains(event.position))
    {
        // Convert click position within minimap to normalized coordinates
        float normX = (event.position.x - minimapBounds_.getX()) / minimapBounds_.getWidth();
        float normY = (event.position.y - minimapBounds_.getY()) / minimapBounds_.getHeight();

        // Clamp to valid range and set as new zoom center
        float halfExtent = 0.5f / zoomLevel_;
        zoomCenterX_ = juce::jlimit(halfExtent, 1.0f - halfExtent, normX);
        zoomCenterY_ = juce::jlimit(halfExtent, 1.0f - halfExtent, normY);

        repaint();
        return;
    }

    // Check for panning: middle mouse button (scroll wheel click)
    // Note: Space key not used for pan since it conflicts with DAW transport controls
    bool shouldPan = event.mods.isMiddleButtonDown();

    if (shouldPan && zoomLevel_ > 1.0f)
    {
        // Start panning
        isPanning_ = true;
        panStartCenter_ = { zoomCenterX_, zoomCenterY_ };
        panStartMouse_ = event.position;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // Normal dragging to set position
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
    if (isPanning_)
    {
        // Calculate pan delta in screen coordinates
        auto bounds = getLocalBounds().toFloat();
        float deltaX = (event.position.x - panStartMouse_.x) / bounds.getWidth();
        float deltaY = (event.position.y - panStartMouse_.y) / bounds.getHeight();

        // Convert screen delta to normalized delta (inverse of zoom)
        float normDeltaX = deltaX / zoomLevel_;
        float normDeltaY = deltaY / zoomLevel_;

        // Calculate desired new center
        float halfExtent = 0.5f / zoomLevel_;
        float desiredX = panStartCenter_.x - normDeltaX;
        float desiredY = panStartCenter_.y - normDeltaY;

        // Clamp and detect boundary hits
        float newX = juce::jlimit(halfExtent, 1.0f - halfExtent, desiredX);
        float newY = juce::jlimit(halfExtent, 1.0f - halfExtent, desiredY);

        // Flash if we hit a boundary
        if (std::abs(newX - desiredX) > 0.001f || std::abs(newY - desiredY) > 0.001f)
        {
            panBoundaryFlash_ = 0.5f;  // Trigger flash
        }

        zoomCenterX_ = newX;
        zoomCenterY_ = newY;

        // Hide hint after first pan
        showHint_ = false;

        repaint();
        return;
    }

    if (isDragging)
    {
        auto normPos = screenToNormalized(event.position);
        targetPosition = normPos;

        updateParameters();
    }
}

void XYPad::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (isPanning_)
    {
        isPanning_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Only end gestures if we were actually dragging (prevents unbalanced gesture calls)
    if (isDragging)
    {
        isDragging = false;

        // End gesture for both parameters to ensure proper automation
        if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
            tonalParam->endChangeGesture();

        if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
            noiseParam->endChangeGesture();
    }
}

void XYPad::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(event);

    // Apply zoom change
    float oldZoom = zoomLevel_;
    float delta = wheel.deltaY > 0 ? kZoomStep : -kZoomStep;
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoomLevel_ + delta);

    // Only update if zoom actually changed
    if (std::abs(newZoom - oldZoom) > 0.01f)
    {
        zoomLevel_ = newZoom;

        // Center zoom on the THUMB POSITION (current parameter value)
        // This is more intuitive for parameter control than cursor-centric zoom
        float halfExtent = 0.5f / newZoom;
        zoomCenterX_ = juce::jlimit(halfExtent, 1.0f - halfExtent, currentPosition.x);
        zoomCenterY_ = juce::jlimit(halfExtent, 1.0f - halfExtent, currentPosition.y);

        // Hide hint after first zoom interaction
        showHint_ = false;

        repaint();
    }
}

void XYPad::mouseDoubleClick(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    // Double-click reset removed to prevent accidental zoom resets
    // Use the "1x" button instead for intentional reset
}

void XYPad::timerCallback()
{
    // Smooth animation with adaptive speed
    // Use immediate snapping when close to target (reduces unnecessary motion)
    if (!isDragging)
    {
        auto diff = targetPosition - currentPosition;
        float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        // Snap to position if very close (reduced motion when near target)
        if (distance < 0.001f)
            currentPosition = targetPosition;
        else
            currentPosition = currentPosition + diff * animationSpeed;
    }
    else
    {
        currentPosition = targetPosition;
    }

    // Handle hint text fading
    if (showHint_)
    {
        juce::int64 elapsed = juce::Time::currentTimeMillis() - hintStartTime_;
        if (elapsed > kHintTimeoutMs)
        {
            // Start fading out
            hintAlpha_ = juce::jmax(0.0f, hintAlpha_ - 0.05f);
            if (hintAlpha_ <= 0.0f)
                showHint_ = false;
        }
    }

    // Decay boundary flash
    if (panBoundaryFlash_ > 0.0f)
    {
        panBoundaryFlash_ = juce::jmax(0.0f, panBoundaryFlash_ - 0.1f);
    }

    repaint();
}

void XYPad::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    // This callback can come from any thread (including audio thread during automation)
    // Use AsyncUpdater to safely defer the update to the message thread
    if (parameterID == ParameterIDs::tonalGain || parameterID == ParameterIDs::noisyGain)
    {
        triggerAsyncUpdate();
    }
}

void XYPad::handleAsyncUpdate()
{
    // Now safely on the message thread - update position from parameters
    if (!isDragging)
    {
        float tonalDb = tonalGainParameter ? tonalGainParameter->load() : 0.0f;
        float noiseDb = noiseGainParameter ? noiseGainParameter->load() : 0.0f;

        // Convert from dB to normalized
        float tonalNorm = (tonalDb - kMinDb) / kDbRange;
        float noiseNorm = (noiseDb - kMinDb) / kDbRange;

        // Clamp to valid range
        tonalNorm = juce::jlimit(0.0f, 1.0f, tonalNorm);
        noiseNorm = juce::jlimit(0.0f, 1.0f, noiseNorm);

        targetPosition = { tonalNorm, 1.0f - noiseNorm }; // Y inverted for UI
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

    // First convert screen position to view coordinates (0-1 in visible area)
    float viewX = (screenPos.x - bounds.getX()) / bounds.getWidth();
    float viewY = (screenPos.y - bounds.getY()) / bounds.getHeight();

    // Apply inverse zoom transform to get actual normalized position
    // Divide by zoomLevel_ because higher zoom = smaller visible range
    float normX = zoomCenterX_ + (viewX - 0.5f) / zoomLevel_;
    float normY = zoomCenterY_ + (viewY - 0.5f) / zoomLevel_;

    return { juce::jlimit(0.0f, 1.0f, normX),
             juce::jlimit(0.0f, 1.0f, normY) };
}

juce::Point<float> XYPad::normalizedToScreen(juce::Point<float> normPos) const
{
    auto bounds = getLocalBounds().toFloat();

    // Apply zoom transform: convert normalized position to view coordinates
    // Multiply by zoomLevel_ so higher zoom = larger on-screen distance
    float viewX = 0.5f + (normPos.x - zoomCenterX_) * zoomLevel_;
    float viewY = 0.5f + (normPos.y - zoomCenterY_) * zoomLevel_;

    // Convert view coordinates to screen position
    float x = bounds.getX() + viewX * bounds.getWidth();
    float y = bounds.getY() + viewY * bounds.getHeight();

    return { x, y };
}

void XYPad::updateParameters()
{
    // Convert normalized position to dB values
    float tonalDb = kMinDb + targetPosition.x * kDbRange;  // 0..1 to -60..+12
    float noiseDb = kMinDb + (1.0f - targetPosition.y) * kDbRange;  // Y inverted
    
    // Clamp to parameter ranges
    tonalDb = juce::jlimit(kMinDb, kMaxDb, tonalDb);
    noiseDb = juce::jlimit(kMinDb, kMaxDb, noiseDb);
    
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

    // Calculate visible normalized range
    float halfExtent = 0.5f / zoomLevel_;
    float visibleMinX = zoomCenterX_ - halfExtent;
    float visibleMaxX = zoomCenterX_ + halfExtent;
    float visibleMinY = zoomCenterY_ - halfExtent;
    float visibleMaxY = zoomCenterY_ + halfExtent;

    // Grid lines at dB values - calculate step based on zoom level
    // At 1x: show 12dB intervals (-60, -48, -36, -24, -12, 0, +12)
    // At 2x: show 6dB intervals
    // At 4x: show 3dB intervals
    // At 6x+: show 1dB intervals
    float dbStep;
    if (zoomLevel_ >= 6.0f)
        dbStep = 1.0f;
    else if (zoomLevel_ >= 4.0f)
        dbStep = 3.0f;
    else if (zoomLevel_ >= 2.0f)
        dbStep = 6.0f;
    else
        dbStep = 12.0f;

    float normStep = dbStep / kDbRange;

    // Determine which lines are "major" (thicker) based on current zoom
    auto isMajorLine = [&](float db) -> bool {
        if (zoomLevel_ >= 6.0f)
            return std::fmod(std::abs(db), 3.0f) < 0.01f;  // Every 3dB is major at high zoom
        else if (zoomLevel_ >= 4.0f)
            return std::fmod(std::abs(db), 6.0f) < 0.01f;  // Every 6dB is major
        else if (zoomLevel_ >= 2.0f)
            return std::fmod(std::abs(db), 12.0f) < 0.01f; // Every 12dB is major
        else
            return true;  // All lines are major at 1x
    };

    // Draw grid lines with dB labels when zoomed in
    g.setFont(juce::FontOptions(9.0f));

    for (float norm = 0.0f; norm <= 1.0f + normStep * 0.5f; norm += normStep)
    {
        float clampedNorm = juce::jlimit(0.0f, 1.0f, norm);
        float db = kMinDb + clampedNorm * kDbRange;

        // Draw vertical lines (tonal axis)
        if (clampedNorm >= visibleMinX && clampedNorm <= visibleMaxX)
        {
            auto screenPos = normalizedToScreen({clampedNorm, 0.0f});
            bool isMajor = isMajorLine(db);

            g.setColour(gridColour.withAlpha(isMajor ? 0.35f : 0.15f));
            float thickness = isMajor ? 1.0f : 0.5f;
            g.drawLine(screenPos.x, bounds.getY(), screenPos.x, bounds.getBottom(), thickness);

            // Draw dB label on major lines when zoomed in enough
            if (isMajor && zoomLevel_ >= 2.0f && screenPos.x > bounds.getX() + 30.0f && screenPos.x < bounds.getRight() - 30.0f)
            {
                juce::String label = (db >= 0 ? "+" : "") + juce::String(static_cast<int>(db));
                g.setColour(tonalColour.withAlpha(0.5f));
                g.drawText(label,
                           juce::Rectangle<float>(screenPos.x - 12.0f, bounds.getBottom() - 24.0f, 24.0f, 10.0f),
                           juce::Justification::centred, false);
            }
        }

        // Draw horizontal lines (noise axis - Y inverted)
        float normY = 1.0f - clampedNorm;  // Invert for Y axis
        if (normY >= visibleMinY && normY <= visibleMaxY)
        {
            auto screenPos = normalizedToScreen({0.0f, normY});
            bool isMajor = isMajorLine(db);

            g.setColour(gridColour.withAlpha(isMajor ? 0.35f : 0.15f));
            float thickness = isMajor ? 1.0f : 0.5f;
            g.drawLine(bounds.getX(), screenPos.y, bounds.getRight(), screenPos.y, thickness);

            // Draw dB label on major lines when zoomed in enough
            if (isMajor && zoomLevel_ >= 2.0f && screenPos.y > bounds.getY() + 20.0f && screenPos.y < bounds.getBottom() - 40.0f)
            {
                juce::String label = (db >= 0 ? "+" : "") + juce::String(static_cast<int>(db));
                g.setColour(noiseColour.withAlpha(0.5f));
                g.drawText(label,
                           juce::Rectangle<float>(bounds.getX() + 4.0f, screenPos.y - 5.0f, 24.0f, 10.0f),
                           juce::Justification::left, false);
            }
        }
    }

    // Draw boundary edges (thick colored lines at parameter limits)
    const float boundaryThickness = 3.0f;

    // Left boundary (X = 0, tonal minimum: -60dB)
    if (visibleMinX <= 0.0f && visibleMaxX >= 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        g.setColour(tonalColour.withAlpha(0.6f));
        g.drawLine(screenPos.x, bounds.getY(), screenPos.x, bounds.getBottom(), boundaryThickness);
    }

    // Right boundary (X = 1, tonal maximum: +12dB)
    if (visibleMinX <= 1.0f && visibleMaxX >= 1.0f)
    {
        auto screenPos = normalizedToScreen({1.0f, 0.0f});
        g.setColour(tonalColour.withAlpha(0.6f));
        g.drawLine(screenPos.x, bounds.getY(), screenPos.x, bounds.getBottom(), boundaryThickness);
    }

    // Top boundary (Y = 0, noise maximum: +12dB - Y is inverted)
    if (visibleMinY <= 0.0f && visibleMaxY >= 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        g.setColour(noiseColour.withAlpha(0.6f));
        g.drawLine(bounds.getX(), screenPos.y, bounds.getRight(), screenPos.y, boundaryThickness);
    }

    // Bottom boundary (Y = 1, noise minimum: -60dB)
    if (visibleMinY <= 1.0f && visibleMaxY >= 1.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 1.0f});
        g.setColour(noiseColour.withAlpha(0.6f));
        g.drawLine(bounds.getX(), screenPos.y, bounds.getRight(), screenPos.y, boundaryThickness);
    }
}

void XYPad::drawLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Calculate visible normalized range for dB labels
    float halfExtent = 0.5f / zoomLevel_;
    float visibleMinX = juce::jlimit(0.0f, 1.0f, zoomCenterX_ - halfExtent);
    float visibleMaxX = juce::jlimit(0.0f, 1.0f, zoomCenterX_ + halfExtent);
    float visibleMinY = juce::jlimit(0.0f, 1.0f, zoomCenterY_ - halfExtent);
    float visibleMaxY = juce::jlimit(0.0f, 1.0f, zoomCenterY_ + halfExtent);

    // Convert visible range to dB values
    // X-axis: tonal gain (left = low, right = high)
    float leftTonalDb = kMinDb + visibleMinX * kDbRange;
    float rightTonalDb = kMinDb + visibleMaxX * kDbRange;
    // Y-axis: noise gain (top = high, bottom = low - inverted)
    float topNoiseDb = kMinDb + (1.0f - visibleMinY) * kDbRange;
    float bottomNoiseDb = kMinDb + (1.0f - visibleMaxY) * kDbRange;

    g.setFont(juce::FontOptions(11.0f));  // Improved readability

    if (zoomLevel_ > 1.0f)
    {
        // When zoomed, show actual dB values at corners
        auto formatDb = [](float db) -> juce::String {
            if (db <= (kMinDb + 0.1f)) return "-inf";
            return juce::String(db, 0) + "dB";
        };

        // Top-left corner: left tonal dB, top noise dB
        g.setColour(tonalColour.withAlpha(0.6f));
        g.drawText("T:" + formatDb(leftTonalDb),
                   juce::Rectangle<float>(bounds.getX() + 4.0f, bounds.getY() + 4.0f, 55.0f, 12.0f),
                   juce::Justification::left, false);
        g.setColour(noiseColour.withAlpha(0.6f));
        g.drawText("N:" + formatDb(topNoiseDb),
                   juce::Rectangle<float>(bounds.getX() + 4.0f, bounds.getY() + 16.0f, 55.0f, 12.0f),
                   juce::Justification::left, false);

        // Top-right corner: right tonal dB
        g.setColour(tonalColour.withAlpha(0.6f));
        g.drawText("T:" + formatDb(rightTonalDb),
                   juce::Rectangle<float>(bounds.getRight() - 59.0f, bounds.getY() + 4.0f, 55.0f, 12.0f),
                   juce::Justification::right, false);

        // Bottom-left corner: bottom noise dB
        g.setColour(noiseColour.withAlpha(0.6f));
        g.drawText("N:" + formatDb(bottomNoiseDb),
                   juce::Rectangle<float>(bounds.getX() + 4.0f, bounds.getBottom() - 40.0f, 55.0f, 12.0f),
                   juce::Justification::left, false);

        // Bottom-right corner: both max values
        g.setColour(tonalColour.withAlpha(0.6f));
        g.drawText("T:" + formatDb(rightTonalDb),
                   juce::Rectangle<float>(bounds.getRight() - 59.0f, bounds.getBottom() - 52.0f, 55.0f, 12.0f),
                   juce::Justification::right, false);
        g.setColour(noiseColour.withAlpha(0.6f));
        g.drawText("N:" + formatDb(bottomNoiseDb),
                   juce::Rectangle<float>(bounds.getRight() - 59.0f, bounds.getBottom() - 40.0f, 55.0f, 12.0f),
                   juce::Justification::right, false);
    }
    else
    {
        // When not zoomed, show descriptive labels
        // Top-left: Low Tonal, High Noise (noise only)
        g.setColour(noiseColour.withAlpha(0.7f));
        g.drawText("Noise Only",
                   juce::Rectangle<float>(bounds.getX() + 6.0f, bounds.getY() + 6.0f, 70.0f, 14.0f),
                   juce::Justification::left, false);

        // Top-right: High Tonal, High Noise (full mix)
        g.setColour(textColour.withAlpha(0.7f));  // Improved contrast
        g.drawText("Full Mix",
                   juce::Rectangle<float>(bounds.getRight() - 56.0f, bounds.getY() + 6.0f, 50.0f, 14.0f),
                   juce::Justification::right, false);

        // Bottom-left: Low Tonal, Low Noise (silent)
        g.setColour(textColour.withAlpha(0.6f));  // Improved contrast
        g.drawText("Silent",
                   juce::Rectangle<float>(bounds.getX() + 6.0f, bounds.getBottom() - 40.0f, 40.0f, 14.0f),
                   juce::Justification::left, false);

        // Bottom-right: High Tonal, Low Noise (tonal only)
        g.setColour(tonalColour.withAlpha(0.7f));
        g.drawText("Tonal Only",
                   juce::Rectangle<float>(bounds.getRight() - 66.0f, bounds.getBottom() - 40.0f, 60.0f, 14.0f),
                   juce::Justification::right, false);
    }
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
    
    // Crosshair lines - shortened to reduce visual clutter
    g.setColour(thumbColour.withAlpha(0.3f));
    auto bounds = getLocalBounds().toFloat();
    constexpr float crosshairLength = 40.0f;  // Shortened from full width/height

    // Vertical lines (above and below thumb)
    float topEnd = juce::jmax(bounds.getY(), screenPos.y - thumbSize/2 - crosshairLength);
    float bottomStart = juce::jmin(bounds.getBottom(), screenPos.y + thumbSize/2 + crosshairLength);
    g.drawLine(screenPos.x, topEnd, screenPos.x, screenPos.y - thumbSize/2, 0.5f);
    g.drawLine(screenPos.x, screenPos.y + thumbSize/2, screenPos.x, bottomStart, 0.5f);

    // Horizontal lines (left and right of thumb)
    float leftEnd = juce::jmax(bounds.getX(), screenPos.x - thumbSize/2 - crosshairLength);
    float rightStart = juce::jmin(bounds.getRight(), screenPos.x + thumbSize/2 + crosshairLength);
    g.drawLine(leftEnd, screenPos.y, screenPos.x - thumbSize/2, screenPos.y, 0.5f);
    g.drawLine(screenPos.x + thumbSize/2, screenPos.y, rightStart, screenPos.y, 0.5f);
}

void XYPad::drawValueReadout(juce::Graphics& g)
{
    // Convert position to dB values for display
    float tonalDb = kMinDb + currentPosition.x * kDbRange;
    float noiseDb = kMinDb + (1.0f - currentPosition.y) * kDbRange;

    // Format strings (show -inf near minimum dB)
    juce::String tonalStr = tonalDb <= (kMinDb + 0.1f) ? "-inf" : juce::String(tonalDb, 1) + " dB";
    juce::String noiseStr = noiseDb <= (kMinDb + 0.1f) ? "-inf" : juce::String(noiseDb, 1) + " dB";

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
    g.setFont(juce::FontOptions(10.0f));

    g.setColour(tonalColour);
    g.drawText("Tonal: " + tonalStr,
               juce::Rectangle<float>(readoutBox.getX() + 6.0f, readoutBox.getY() + 4.0f, 75.0f, 16.0f),
               juce::Justification::left, false);

    g.setColour(noiseColour);
    g.drawText("Noise: " + noiseStr,
               juce::Rectangle<float>(readoutBox.getX() + 82.0f, readoutBox.getY() + 4.0f, 75.0f, 16.0f),
               juce::Justification::left, false);
}

void XYPad::drawBoundaryFill(juce::Graphics& g)
{
    // Only draw boundary fill when zoomed in
    if (zoomLevel_ <= 1.0f)
        return;

    auto bounds = getLocalBounds().toFloat();
    const juce::Colour boundaryColour(0xff000000);  // Pure black for clear boundary

    // Calculate visible normalized range
    float halfExtent = 0.5f / zoomLevel_;
    float visibleMinX = zoomCenterX_ - halfExtent;
    float visibleMaxX = zoomCenterX_ + halfExtent;
    float visibleMinY = zoomCenterY_ - halfExtent;
    float visibleMaxY = zoomCenterY_ + halfExtent;

    g.setColour(boundaryColour);

    // Fill left boundary area (X < 0)
    if (visibleMinX < 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        float fillWidth = screenPos.x - bounds.getX();
        if (fillWidth > 0)
            g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), fillWidth, bounds.getHeight()));
    }

    // Fill right boundary area (X > 1)
    if (visibleMaxX > 1.0f)
    {
        auto screenPos = normalizedToScreen({1.0f, 0.0f});
        float fillWidth = bounds.getRight() - screenPos.x;
        if (fillWidth > 0)
            g.fillRect(juce::Rectangle<float>(screenPos.x, bounds.getY(), fillWidth, bounds.getHeight()));
    }

    // Fill top boundary area (Y < 0)
    if (visibleMinY < 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        float fillHeight = screenPos.y - bounds.getY();
        if (fillHeight > 0)
            g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), bounds.getWidth(), fillHeight));
    }

    // Fill bottom boundary area (Y > 1)
    if (visibleMaxY > 1.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 1.0f});
        float fillHeight = bounds.getBottom() - screenPos.y;
        if (fillHeight > 0)
            g.fillRect(juce::Rectangle<float>(bounds.getX(), screenPos.y, bounds.getWidth(), fillHeight));
    }
}

void XYPad::drawMinimap(juce::Graphics& g)
{
    // Only show minimap when zoomed in
    if (zoomLevel_ <= 1.0f)
    {
        minimapBounds_ = {};  // Clear bounds when not visible
        return;
    }

    auto bounds = getLocalBounds().toFloat();

    // Minimap dimensions and position (top-left corner, with margin)
    const float minimapSize = 60.0f;
    const float margin = 6.0f;
    minimapBounds_ = juce::Rectangle<float>(
        bounds.getX() + margin,
        bounds.getY() + 24.0f,  // Below the corner labels
        minimapSize,
        minimapSize
    );

    // Draw minimap background
    g.setColour(backgroundColour.withAlpha(0.9f));
    g.fillRoundedRectangle(minimapBounds_, 3.0f);

    // Draw minimap border - highlight if clickable
    g.setColour(gridColour.brighter(0.2f));
    g.drawRoundedRectangle(minimapBounds_, 3.0f, 1.0f);

    // Draw gradient hints (subtle tonal/noise indication)
    juce::ColourGradient tonalHint(
        tonalColour.withAlpha(0.0f), minimapBounds_.getX(), minimapBounds_.getCentreY(),
        tonalColour.withAlpha(0.15f), minimapBounds_.getRight(), minimapBounds_.getCentreY(),
        false);
    g.setGradientFill(tonalHint);
    g.fillRoundedRectangle(minimapBounds_.reduced(1), 2.0f);

    juce::ColourGradient noiseHint(
        noiseColour.withAlpha(0.15f), minimapBounds_.getCentreX(), minimapBounds_.getY(),
        noiseColour.withAlpha(0.0f), minimapBounds_.getCentreX(), minimapBounds_.getBottom(),
        false);
    g.setGradientFill(noiseHint);
    g.fillRoundedRectangle(minimapBounds_.reduced(1), 2.0f);

    // Calculate viewport rectangle in minimap space
    float halfExtent = 0.5f / zoomLevel_;
    float viewMinX = juce::jlimit(0.0f, 1.0f, zoomCenterX_ - halfExtent);
    float viewMaxX = juce::jlimit(0.0f, 1.0f, zoomCenterX_ + halfExtent);
    float viewMinY = juce::jlimit(0.0f, 1.0f, zoomCenterY_ - halfExtent);
    float viewMaxY = juce::jlimit(0.0f, 1.0f, zoomCenterY_ + halfExtent);

    juce::Rectangle<float> viewportRect(
        minimapBounds_.getX() + viewMinX * minimapBounds_.getWidth(),
        minimapBounds_.getY() + viewMinY * minimapBounds_.getHeight(),
        (viewMaxX - viewMinX) * minimapBounds_.getWidth(),
        (viewMaxY - viewMinY) * minimapBounds_.getHeight()
    );

    // Draw viewport rectangle
    g.setColour(thumbColour.withAlpha(0.3f));
    g.fillRect(viewportRect);
    g.setColour(thumbColour);
    g.drawRect(viewportRect, 1.5f);

    // Draw current position dot on minimap
    float dotX = minimapBounds_.getX() + currentPosition.x * minimapBounds_.getWidth();
    float dotY = minimapBounds_.getY() + currentPosition.y * minimapBounds_.getHeight();
    g.setColour(thumbColour);
    g.fillEllipse(dotX - 3.0f, dotY - 3.0f, 6.0f, 6.0f);

    // Draw "click to navigate" hint at bottom of minimap
    g.setColour(textColour.withAlpha(0.5f));
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("click to nav",
               juce::Rectangle<float>(minimapBounds_.getX() - 2.0f, minimapBounds_.getBottom() + 2.0f,
                                       minimapBounds_.getWidth() + 4.0f, 10.0f),
               juce::Justification::centred, false);
}

void XYPad::zoomIn()
{
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoomLevel_ + kZoomStep);
    if (newZoom != zoomLevel_)
    {
        zoomLevel_ = newZoom;
        repaint();
    }
}

void XYPad::zoomOut()
{
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoomLevel_ - kZoomStep);
    if (newZoom != zoomLevel_)
    {
        zoomLevel_ = newZoom;
        repaint();
    }
}

void XYPad::resetZoom()
{
    zoomLevel_ = 1.0f;
    zoomCenterX_ = 0.5f;
    zoomCenterY_ = 0.5f;
    repaint();
}

bool XYPad::keyPressed(const juce::KeyPress& key)
{
    // Step size: smaller when zoomed in for finer control
    const float baseStep = 0.02f;
    const float step = baseStep / zoomLevel_;

    bool handled = false;
    float newX = targetPosition.x;
    float newY = targetPosition.y;

    if (key == juce::KeyPress::leftKey)
    {
        newX = juce::jlimit(0.0f, 1.0f, targetPosition.x - step);
        handled = true;
    }
    else if (key == juce::KeyPress::rightKey)
    {
        newX = juce::jlimit(0.0f, 1.0f, targetPosition.x + step);
        handled = true;
    }
    else if (key == juce::KeyPress::upKey)
    {
        newY = juce::jlimit(0.0f, 1.0f, targetPosition.y - step);  // Up = decrease Y (higher noise)
        handled = true;
    }
    else if (key == juce::KeyPress::downKey)
    {
        newY = juce::jlimit(0.0f, 1.0f, targetPosition.y + step);  // Down = increase Y (lower noise)
        handled = true;
    }
    else if (key == juce::KeyPress::homeKey)
    {
        // Home: reset to center (0dB both)
        newX = kZeroDbNorm;  // 0dB tonal position
        newY = 1.0f - kZeroDbNorm;  // 0dB noise (Y inverted)
        handled = true;
    }

    if (handled)
    {
        // Begin gesture for automation recording
        if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
            tonalParam->beginChangeGesture();
        if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
            noiseParam->beginChangeGesture();

        targetPosition = { newX, newY };
        updateParameters();

        // End gesture
        if (auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain))
            tonalParam->endChangeGesture();
        if (auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain))
            noiseParam->endChangeGesture();
    }

    return handled;
}

void XYPad::focusGained(FocusChangeType /*cause*/)
{
    hasFocus_ = true;
    repaint();
}

void XYPad::focusLost(FocusChangeType /*cause*/)
{
    hasFocus_ = false;
    repaint();
}

void XYPad::visibilityChanged()
{
    // Manage timer based on visibility to save CPU when not displayed
    if (isVisible())
        startTimerHz(60);
    else
        stopTimer();
}

bool XYPad::keyStateChanged(bool /*isKeyDown*/)
{
    // Space key panning removed - conflicts with DAW transport controls
    // Panning is done via middle mouse button (scroll wheel click) instead
    return false;
}

void XYPad::modifierKeysChanged(const juce::ModifierKeys& /*modifiers*/)
{
    // Cursor is managed by mouse events for middle-button panning
    if (!isPanning_)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void XYPad::drawHintText(juce::Graphics& g)
{
    if (!showHint_ || hintAlpha_ <= 0.0f)
        return;

    auto bounds = getLocalBounds().toFloat();

    // Draw hint at bottom center
    juce::String hintText = "Scroll to zoom \u2022 Middle-click+drag to pan";

    g.setColour(textColour.withAlpha(0.6f * hintAlpha_));
    g.setFont(juce::FontOptions(11.0f));

    juce::Rectangle<float> hintBox(
        bounds.getCentreX() - 120.0f,
        bounds.getBottom() - 50.0f,
        240.0f,
        16.0f
    );

    // Background for better readability
    g.setColour(backgroundColour.withAlpha(0.7f * hintAlpha_));
    g.fillRoundedRectangle(hintBox.expanded(4.0f, 2.0f), 4.0f);

    g.setColour(textColour.withAlpha(0.8f * hintAlpha_));
    g.drawText(hintText, hintBox, juce::Justification::centred, false);
}

void XYPad::drawAxisLabels(juce::Graphics& g)
{
    // Only show axis labels when not zoomed (they clutter the view when zoomed)
    if (zoomLevel_ > 1.5f)
        return;

    auto bounds = getLocalBounds().toFloat();

    g.setFont(juce::FontOptions(10.0f));
    g.setColour(textColour.withAlpha(0.4f));

    // Bottom axis label: "TONAL" with arrows as < >
    g.drawText("< Tonal >",
               juce::Rectangle<float>(bounds.getCentreX() - 35.0f, bounds.getBottom() - 16.0f, 70.0f, 12.0f),
               juce::Justification::centred, false);

    // Left axis label: "NOISE" (rotated)
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                     bounds.getX() + 10.0f,
                                                     bounds.getCentreY()));
    g.drawText("< Noise >",
               juce::Rectangle<float>(bounds.getX() - 25.0f, bounds.getCentreY() - 6.0f, 70.0f, 12.0f),
               juce::Justification::centred, false);
    g.restoreState();
}

void XYPad::drawBoundaryFlash(juce::Graphics& g)
{
    if (panBoundaryFlash_ <= 0.0f)
        return;

    auto bounds = getLocalBounds().toFloat();

    // Draw a subtle flash on the edges
    g.setColour(thumbColour.withAlpha(panBoundaryFlash_ * 0.3f));
    g.drawRect(bounds, 3.0f);
}