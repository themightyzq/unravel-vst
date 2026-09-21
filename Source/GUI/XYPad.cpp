#include "XYPad.h"
#include "Theme.h"
#include "../Parameters/ParameterDefinitions.h"
#include <zqsfx_ui/zqsfx_ui.h>

namespace
{
    // This bespoke display keeps working exactly as it did (spec requirement): only
    // its colours, background (phosphor screen), fonts, and corner radii change. Every
    // string it draws by hand routes through the house LookAndFeel's lcdFont/
    // drawLcdText when one is installed (style guide section 4), falling back to a
    // plain generic-font drawText otherwise.
    void drawScreenText(juce::Graphics& g, juce::Component& c, const juce::String& text,
                        juce::Rectangle<int> area, float px, juce::Justification just,
                        juce::Colour col)
    {
        if (auto* lnf = dynamic_cast<zqsfx::ui::LookAndFeel*>(&c.getLookAndFeel()))
            lnf->drawLcdText(g, text, area, px, just, col);
        else
        {
            g.setFont(juce::FontOptions(px));
            g.setColour(col);
            g.drawText(text, area, just);
        }
    }
}

XYPad::XYPad(juce::AudioProcessorValueTreeState& apvts_)
    : apvts(apvts_)
{
    // Colours from the shared Theme palette (tonal sky blue / noise purple / accent).
    gridColour = Theme::grid;
    thumbColour = Theme::accent;
    thumbHighlightColour = Theme::accentHi;
    tonalColour = Theme::tonal;
    noiseColour = Theme::noise;
    textColour = Theme::textBright;
    
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

    // Set up zoom control buttons. No per-instance buttonColourId/textColourOffId:
    // the house LookAndFeel's drawButtonBackground/drawButtonText always draw from
    // colour::btnText / colour::accent (hover) regardless of instance colours, so
    // those calls were dead once CustomLookAndFeel stopped overriding them.
    auto setupZoomButton = [this](juce::TextButton& btn, const juce::String& text, const juce::String& tooltip) {
        btn.setButtonText(text);
        btn.setTooltip(tooltip);
        // Keep the zoom controls out of the keyboard Tab order so the XY pad reads
        // as a single focus stop (D-8/R10). Zoom is a view-only convenience — the
        // pad itself is fully keyboard-operable via arrow keys — and it remains
        // mouse-clickable. TextButton defaults to wanting focus, so opt out here.
        btn.setWantsKeyboardFocus(false);
        addAndMakeVisible(btn);
    };

    setupZoomButton(zoomInButton, "+", "Zoom In: Increase magnification for fine control");
    setupZoomButton(zoomOutButton, "-", "Zoom Out: Decrease magnification");
    setupZoomButton(zoomResetButton, "1x", "Reset Zoom: Return to full view");

    zoomInButton.onClick = [this]() { zoomIn(); };
    zoomOutButton.onClick = [this]() { zoomOut(); };
    zoomResetButton.onClick = [this]() { resetZoom(); };

    // Enable keyboard focus for accessibility. setHasFocusOutline routes the ring
    // through the house LookAndFeel (createFocusOutlineForComponent) instead of the
    // hand-drawn ring paint() used to draw itself (style guide section 8 / Phase1
    // item 8: "Focus rings come from the house LookAndFeel; do not draw your own").
    setWantsKeyboardFocus(true);
    setHasFocusOutline(true);
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
    // Background: house phosphor screen (bezel + LCD glass + scanlines) instead of a
    // flat fill — the XY pad is one of the "custom displays" the style guide names
    // explicitly (section 5). Scanlines off: this is a large interactive control, not
    // a small readout, and the pad's own grid/gradients already carry plenty of detail.
    zqsfx::ui::LookAndFeel::drawScreen(g, getLocalBounds().toFloat(), false);

    // Draw dark boundary fill outside parameter limits when zoomed
    drawBoundaryFill(g);

    // Draw grid with major/minor hierarchy
    drawGrid(g);

    // Gradient overlays for visual orientation
    // Using saturated colors with clear axis indicator bars
    auto bounds = getLocalBounds().toFloat();

    // Use the shared tonal/noise tokens (the gradients and axis bars previously
    // hardcoded a different blue/orange than the rest of the pad; noise is purple now).
    const juce::Colour tonalHigh = tonalColour;
    const juce::Colour noiseHigh = noiseColour;

    // Strong overlapping gradients - clearly visible even when blended
    // Tonal gradient (left to right) - sky blue builds toward right
    juce::ColourGradient tonalGradient(
        tonalHigh.withAlpha(0.0f), bounds.getX(), bounds.getCentreY(),
        tonalHigh.withAlpha(0.4f), bounds.getRight(), bounds.getCentreY(),
        false);
    g.setGradientFill(tonalGradient);
    g.fillRect(bounds);

    // Noise gradient (bottom to top) - purple builds toward top
    juce::ColourGradient noiseGradient(
        noiseHigh.withAlpha(0.0f), bounds.getCentreX(), bounds.getBottom(),
        noiseHigh.withAlpha(0.4f), bounds.getCentreX(), bounds.getY(),
        false);
    g.setGradientFill(noiseGradient);
    g.fillRect(bounds);

    // Solid axis indicator bars - always visible "legend" for the axes
    const float barWidth = 6.0f;

    // Right edge bar - solid sky blue (Tonal axis indicator)
    g.setColour(tonalHigh.withAlpha(0.85f));
    g.fillRect(juce::Rectangle<float>(bounds.getRight() - barWidth, bounds.getY(),
                                        barWidth, bounds.getHeight()));

    // Top edge bar - solid purple (Noise axis indicator)
    g.setColour(noiseHigh.withAlpha(0.85f));
    g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(),
                                        bounds.getWidth(), barWidth));

    // Small corner labels on the bars for extra clarity. logoBright (not accent —
    // accent means "active", never a plain label) gives strong contrast against
    // either saturated axis-bar colour.
    // "T" label on the sky-blue bar (Tonal)
    drawScreenText(g, *this, "T",
                  { juce::roundToInt(bounds.getRight() - barWidth - 1), juce::roundToInt(bounds.getCentreY() - 6.0f),
                    juce::roundToInt(barWidth + 2), 12 },
                  11.0f, juce::Justification::centred, zqsfx::ui::colour::logoBright);

    // "N" label on the purple bar (Noise)
    drawScreenText(g, *this, "N",
                  { juce::roundToInt(bounds.getCentreX() - 6.0f), juce::roundToInt(bounds.getY()), 12,
                    juce::roundToInt(barWidth + 2) },
                  11.0f, juce::Justification::centred, zqsfx::ui::colour::logoBright);

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
        juce::String zoomText = juce::String(zoomLevel_, 1) + "x";
        drawScreenText(g, *this, zoomText,
                      { static_cast<int>(bounds.getRight()) - 70,   // Moved left to avoid buttons
                        static_cast<int>(bounds.getY()) + 6, 34, 14 },
                      12.0f, juce::Justification::right, thumbColour.withAlpha(0.8f));
    }

    // Draw axis labels
    drawAxisLabels(g);

    // Draw hint text (fades after first use)
    drawHintText(g);

    // Draw boundary flash when panning hits edge
    drawBoundaryFlash(g);

    // No manual border or focus ring here any more: zqsfx::ui::LookAndFeel::drawScreen
    // (called at the top of this method) already draws the screen's own bezel/border,
    // and setHasFocusOutline(true) (see the constructor) routes keyboard focus through
    // the house LookAndFeel's own accent ring instead of a hand-drawn one.
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
    bool changed = false;

    // Smooth animation with adaptive speed
    // Use immediate snapping when close to target (reduces unnecessary motion)
    if (!isDragging)
    {
        auto diff = targetPosition - currentPosition;
        float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        // Snap to position if very close (reduced motion when near target)
        if (distance < 0.001f)
        {
            if (currentPosition != targetPosition)
            {
                currentPosition = targetPosition;
                changed = true;
            }
        }
        else
        {
            currentPosition = currentPosition + diff * animationSpeed;
            changed = true;
        }
    }
    else if (currentPosition != targetPosition)
    {
        currentPosition = targetPosition;
        changed = true;
    }

    // Handle hint text fading
    if (showHint_)
    {
        juce::int64 elapsed = juce::Time::currentTimeMillis() - hintStartTime_;
        if (elapsed > kHintTimeoutMs)
        {
            // Start fading out
            const float prevAlpha = hintAlpha_;
            hintAlpha_ = juce::jmax(0.0f, hintAlpha_ - 0.05f);
            if (hintAlpha_ <= 0.0f)
                showHint_ = false;
            if (! juce::approximatelyEqual(hintAlpha_, prevAlpha))
                changed = true;
        }
    }

    // Decay boundary flash
    if (panBoundaryFlash_ > 0.0f)
    {
        panBoundaryFlash_ = juce::jmax(0.0f, panBoundaryFlash_ - 0.1f);
        changed = true;
    }

    // Only repaint when something actually changed, instead of redrawing the
    // whole pad 60x/second while idle.
    if (changed)
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

    // Programmatic path (preset load / host recall): mouse drags hold their
    // own gesture (mouseDown/mouseUp), but this entry had none, so hosts in
    // automation-write mode ignored preset-driven pad moves (REVIEW-QA QA-L3).
    // Guarded on isDragging so a drag in progress never double-begins.
    const bool needGesture = ! isDragging;
    auto* tonalParam = apvts.getParameter(ParameterIDs::tonalGain);
    auto* noiseParam = apvts.getParameter(ParameterIDs::noisyGain);

    if (needGesture)
    {
        if (tonalParam != nullptr) tonalParam->beginChangeGesture();
        if (noiseParam != nullptr) noiseParam->beginChangeGesture();
    }

    updateParameters();

    if (needGesture)
    {
        if (tonalParam != nullptr) tonalParam->endChangeGesture();
        if (noiseParam != nullptr) noiseParam->endChangeGesture();
    }
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
                drawScreenText(g, *this, label,
                              { juce::roundToInt(screenPos.x - 12.0f), juce::roundToInt(bounds.getBottom() - 24.0f), 24, 10 },
                              10.0f, juce::Justification::centred, tonalColour.withAlpha(0.85f));
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
                drawScreenText(g, *this, label,
                              { juce::roundToInt(bounds.getX() + 4.0f), juce::roundToInt(screenPos.y - 5.0f), 24, 10 },
                              10.0f, juce::Justification::left, noiseColour.withAlpha(0.85f));
            }
        }
    }

    // Draw boundary edges (thick colored lines at parameter limits)
    const float boundaryThickness = 3.0f;

    // Left boundary (X = 0, tonal minimum: -60dB)
    if (visibleMinX <= 0.0f && visibleMaxX >= 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        g.setColour(tonalColour.withAlpha(0.85f));
        g.drawLine(screenPos.x, bounds.getY(), screenPos.x, bounds.getBottom(), boundaryThickness);
    }

    // Right boundary (X = 1, tonal maximum: +12dB)
    if (visibleMinX <= 1.0f && visibleMaxX >= 1.0f)
    {
        auto screenPos = normalizedToScreen({1.0f, 0.0f});
        g.setColour(tonalColour.withAlpha(0.85f));
        g.drawLine(screenPos.x, bounds.getY(), screenPos.x, bounds.getBottom(), boundaryThickness);
    }

    // Top boundary (Y = 0, noise maximum: +12dB - Y is inverted)
    if (visibleMinY <= 0.0f && visibleMaxY >= 0.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 0.0f});
        g.setColour(noiseColour.withAlpha(0.85f));
        g.drawLine(bounds.getX(), screenPos.y, bounds.getRight(), screenPos.y, boundaryThickness);
    }

    // Bottom boundary (Y = 1, noise minimum: -60dB)
    if (visibleMinY <= 1.0f && visibleMaxY >= 1.0f)
    {
        auto screenPos = normalizedToScreen({0.0f, 1.0f});
        g.setColour(noiseColour.withAlpha(0.85f));
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

    if (zoomLevel_ > 1.0f)
    {
        // When zoomed, show actual dB values at corners
        auto formatDb = [](float db) -> juce::String {
            if (db <= (kMinDb + 0.1f)) return "-inf";
            return juce::String(db, 0) + "dB";
        };
        auto corner = [&](const juce::String& text, float x, float y, float w, float h,
                          juce::Justification j, juce::Colour col)
        {
            drawScreenText(g, *this, text,
                          { juce::roundToInt(x), juce::roundToInt(y), juce::roundToInt(w), juce::roundToInt(h) },
                          11.0f, j, col);
        };

        const auto tonalCol = tonalColour.withAlpha(0.85f);
        const auto noiseCol = noiseColour.withAlpha(0.85f);

        // Top-left corner: left tonal dB, top noise dB
        corner("T:" + formatDb(leftTonalDb), bounds.getX() + 4.0f, bounds.getY() + 4.0f, 55.0f, 12.0f,
              juce::Justification::left, tonalCol);
        corner("N:" + formatDb(topNoiseDb), bounds.getX() + 4.0f, bounds.getY() + 16.0f, 55.0f, 12.0f,
              juce::Justification::left, noiseCol);

        // Top-right corner: right tonal dB
        corner("T:" + formatDb(rightTonalDb), bounds.getRight() - 59.0f, bounds.getY() + 4.0f, 55.0f, 12.0f,
              juce::Justification::right, tonalCol);

        // Bottom-left corner: bottom noise dB
        corner("N:" + formatDb(bottomNoiseDb), bounds.getX() + 4.0f, bounds.getBottom() - 40.0f, 55.0f, 12.0f,
              juce::Justification::left, noiseCol);

        // Bottom-right corner: both max values
        corner("T:" + formatDb(rightTonalDb), bounds.getRight() - 59.0f, bounds.getBottom() - 52.0f, 55.0f, 12.0f,
              juce::Justification::right, tonalCol);
        corner("N:" + formatDb(bottomNoiseDb), bounds.getRight() - 59.0f, bounds.getBottom() - 40.0f, 55.0f, 12.0f,
              juce::Justification::right, noiseCol);
    }
    else
    {
        // When not zoomed, show descriptive labels
        // Top-left: Low Tonal, High Noise (noise only)
        drawScreenText(g, *this, "Noise Only",
                      { juce::roundToInt(bounds.getX() + 6.0f), juce::roundToInt(bounds.getY() + 6.0f), 70, 14 },
                      11.0f, juce::Justification::left, noiseColour.withAlpha(0.7f));

        // Top-right: High Tonal, High Noise (full mix)
        drawScreenText(g, *this, "Full Mix",
                      { juce::roundToInt(bounds.getRight() - 56.0f), juce::roundToInt(bounds.getY() + 6.0f), 50, 14 },
                      11.0f, juce::Justification::right, textColour.withAlpha(0.7f));

        // Bottom-left: Low Tonal, Low Noise (silent)
        drawScreenText(g, *this, "Silent",
                      { juce::roundToInt(bounds.getX() + 6.0f), juce::roundToInt(bounds.getBottom() - 40.0f), 40, 14 },
                      11.0f, juce::Justification::left, textColour.withAlpha(0.6f));

        // Bottom-right: High Tonal, Low Noise (tonal only)
        drawScreenText(g, *this, "Tonal Only",
                      { juce::roundToInt(bounds.getRight() - 66.0f), juce::roundToInt(bounds.getBottom() - 40.0f), 60, 14 },
                      11.0f, juce::Justification::right, tonalColour.withAlpha(0.7f));
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
    
    // Soft fill so the handle reads as a solid, grabbable object rather than
    // a thin outline lost in the gradient (D2-7).
    g.setColour((isDragging ? thumbHighlightColour : thumbColour).withAlpha(0.25f));
    g.fillEllipse(screenPos.x - thumbSize/2,
                  screenPos.y - thumbSize/2,
                  thumbSize, thumbSize);

    // Outer ring
    g.setColour(isDragging ? thumbHighlightColour : thumbColour);
    g.drawEllipse(screenPos.x - thumbSize/2,
                  screenPos.y - thumbSize/2,
                  thumbSize, thumbSize, 2.5f);

    // Inner dot
    g.setColour(isDragging ? thumbHighlightColour : thumbColour);
    g.fillEllipse(screenPos.x - 5, screenPos.y - 5, 10, 10);
    
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

    // A small LCD readout under the pad (style guide section 6: "Value readout: a
    // small LCD under the knob, VT323, glow"). Hard-edged, no rounded corners.
    zqsfx::ui::LookAndFeel::drawScreen(g, readoutBox, false);

    // Draw values side by side
    drawScreenText(g, *this, "Tonal: " + tonalStr,
                  { juce::roundToInt(readoutBox.getX() + 6.0f), juce::roundToInt(readoutBox.getY() + 4.0f), 75, 16 },
                  13.0f, juce::Justification::left, tonalColour);

    drawScreenText(g, *this, "Noise: " + noiseStr,
                  { juce::roundToInt(readoutBox.getX() + 82.0f), juce::roundToInt(readoutBox.getY() + 4.0f), 75, 16 },
                  13.0f, juce::Justification::left, noiseColour);
}

void XYPad::drawBoundaryFill(juce::Graphics& g)
{
    // Only draw boundary fill when zoomed in
    if (zoomLevel_ <= 1.0f)
        return;

    auto bounds = getLocalBounds().toFloat();
    // Pure black blackout mask (not a meaning-carrying colour) for the area outside
    // parameter limits when zoomed — the same exemption the house LookAndFeel itself
    // uses for shadows (juce::Colours::black/transparentBlack allowed outside the
    // theme file for shadow/blackout fills).
    const juce::Colour boundaryColour(juce::Colours::black);

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

    // Draw minimap background — simple lcdBg well + lcdBorder rim (like
    // zqsfx::ui::PeakMeter's own well treatment), hard-edged, no rounded corners.
    g.setColour(zqsfx::ui::colour::lcdBg.withAlpha(0.9f));
    g.fillRect(minimapBounds_);
    g.setColour(zqsfx::ui::colour::lcdBorder);
    g.drawRect(minimapBounds_, 1.0f);

    // Draw gradient hints (subtle tonal/noise indication)
    juce::ColourGradient tonalHint(
        tonalColour.withAlpha(0.0f), minimapBounds_.getX(), minimapBounds_.getCentreY(),
        tonalColour.withAlpha(0.15f), minimapBounds_.getRight(), minimapBounds_.getCentreY(),
        false);
    g.setGradientFill(tonalHint);
    g.fillRect(minimapBounds_.reduced(1));

    juce::ColourGradient noiseHint(
        noiseColour.withAlpha(0.15f), minimapBounds_.getCentreX(), minimapBounds_.getY(),
        noiseColour.withAlpha(0.0f), minimapBounds_.getCentreX(), minimapBounds_.getBottom(),
        false);
    g.setGradientFill(noiseHint);
    g.fillRect(minimapBounds_.reduced(1));

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
    drawScreenText(g, *this, "click to nav",
                  { juce::roundToInt(minimapBounds_.getX() - 2.0f), juce::roundToInt(minimapBounds_.getBottom() + 2.0f),
                    juce::roundToInt(minimapBounds_.getWidth() + 4.0f), 10 },
                  9.0f, juce::Justification::centred, textColour.withAlpha(0.5f));
}

void XYPad::zoomIn()
{
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoomLevel_ + kZoomStep);
    if (! juce::approximatelyEqual(newZoom, zoomLevel_))
    {
        zoomLevel_ = newZoom;
        repaint();
    }
}

void XYPad::zoomOut()
{
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoomLevel_ - kZoomStep);
    if (! juce::approximatelyEqual(newZoom, zoomLevel_))
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
    // The focus ring itself is now drawn by the house LookAndFeel's own overlay
    // (setHasFocusOutline(true) in the constructor), not by this component's
    // paint(), so there is nothing left to repaint here.
}

void XYPad::focusLost(FocusChangeType /*cause*/)
{
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
    // ASCII only: \u2022 in a char* literal produces UTF-8 bytes that JUCE's
    // Latin-1 String constructor renders as mojibake ("\u00e2\u20ac\u00a2").
    juce::String hintText = "Scroll to zoom | Middle-click+drag to pan";

    juce::Rectangle<float> hintBox(
        bounds.getCentreX() - 120.0f,
        bounds.getBottom() - 50.0f,
        240.0f,
        16.0f
    );

    // Background for better readability — hard rect, lcdBg/lcdBorder, no rounding.
    const auto boxBounds = hintBox.expanded(4.0f, 2.0f);
    g.setColour(zqsfx::ui::colour::lcdBg.withAlpha(0.7f * hintAlpha_));
    g.fillRect(boxBounds);
    g.setColour(zqsfx::ui::colour::lcdBorder.withAlpha(hintAlpha_));
    g.drawRect(boxBounds, 1.0f);

    drawScreenText(g, *this, hintText, hintBox.toNearestInt(), 12.0f,
                  juce::Justification::centred, textColour.withAlpha(0.8f * hintAlpha_));
}

void XYPad::drawAxisLabels(juce::Graphics& g)
{
    // Only show axis labels when not zoomed (they clutter the view when zoomed)
    if (zoomLevel_ > 1.5f)
        return;

    auto bounds = getLocalBounds().toFloat();
    const auto col = textColour.withAlpha(0.7f);

    // Bottom axis label: "TONAL" with arrows as < >
    drawScreenText(g, *this, "< Tonal >",
                  { juce::roundToInt(bounds.getCentreX() - 35.0f), juce::roundToInt(bounds.getBottom() - 16.0f), 70, 12 },
                  10.0f, juce::Justification::centred, col);

    // Left axis label: "NOISE" (rotated)
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                     bounds.getX() + 10.0f,
                                                     bounds.getCentreY()));
    drawScreenText(g, *this, "< Noise >",
                  { juce::roundToInt(bounds.getX() - 25.0f), juce::roundToInt(bounds.getCentreY() - 6.0f), 70, 12 },
                  10.0f, juce::Justification::centred, col);
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