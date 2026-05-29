#include "CustomLookAndFeel.h"

CustomLookAndFeel::CustomLookAndFeel()
{
    // Fallback colour scheme so even un-styled controls read as themed.
    setColour(juce::Slider::rotarySliderFillColourId, Theme::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, Theme::bgLight);
    setColour(juce::Slider::thumbColourId, Theme::accent);
    setColour(juce::Slider::textBoxTextColourId, Theme::textBright);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, Theme::bgMid);

    setColour(juce::ComboBox::backgroundColourId, Theme::bgLight);
    setColour(juce::ComboBox::textColourId, Theme::textBright);
    setColour(juce::ComboBox::outlineColourId, Theme::bgLight);
    setColour(juce::ComboBox::arrowColourId, Theme::accent);

    setColour(juce::PopupMenu::backgroundColourId, Theme::bgMid);
    setColour(juce::PopupMenu::textColourId, Theme::textBright);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, Theme::textBright);

    setColour(juce::TextButton::buttonColourId, Theme::bgLight);
    setColour(juce::TextButton::textColourOffId, Theme::textBright);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
}

void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds  = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                          static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax(2.5f, radius * 0.14f);
    const float arcR  = radius - lineW * 0.5f;
    const auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Background track arc
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Filled value arc
    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Thumb dot on the arc
    const juce::Point<float> thumb(centre.x + arcR * std::cos(toAngle - juce::MathConstants<float>::halfPi),
                                   centre.y + arcR * std::sin(toAngle - juce::MathConstants<float>::halfPi));
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillEllipse(juce::Rectangle<float>(lineW * 1.6f, lineW * 1.6f).withCentre(thumb));
}

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // Match the rotary aesthetic: a thin rounded track + a filled value portion
    // in the slider's trackColourId + a small dot thumb in thumbColourId.
    const float trackThickness = juce::jmax(3.0f, juce::jmin(static_cast<float>(width), static_cast<float>(height)) * 0.08f);
    const float thumbSize      = trackThickness * 2.6f;

    const auto trackBg   = slider.findColour(juce::Slider::backgroundColourId);
    const auto trackFill = slider.findColour(juce::Slider::trackColourId);
    const auto thumb     = slider.findColour(juce::Slider::thumbColourId);

    if (style == juce::Slider::LinearVertical)
    {
        const float trackX = x + width * 0.5f - trackThickness * 0.5f;
        const auto  bgRect = juce::Rectangle<float>(trackX, static_cast<float>(y),
                                                    trackThickness, static_cast<float>(height));

        g.setColour(trackBg);
        g.fillRoundedRectangle(bgRect, trackThickness * 0.5f);

        if (slider.isEnabled())
        {
            // Vertical slider: sliderPos is the y of the thumb; value increases upward.
            const float fillTop    = sliderPos;
            const float fillBottom = static_cast<float>(y + height);
            if (fillBottom > fillTop)
            {
                const auto fillRect = juce::Rectangle<float>(trackX, fillTop, trackThickness, fillBottom - fillTop);
                g.setColour(trackFill);
                g.fillRoundedRectangle(fillRect, trackThickness * 0.5f);
            }
        }

        g.setColour(thumb);
        g.fillEllipse(juce::Rectangle<float>(thumbSize, thumbSize)
                          .withCentre({ x + width * 0.5f, sliderPos }));
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        const float trackY = y + height * 0.5f - trackThickness * 0.5f;
        const auto  bgRect = juce::Rectangle<float>(static_cast<float>(x), trackY,
                                                    static_cast<float>(width), trackThickness);

        g.setColour(trackBg);
        g.fillRoundedRectangle(bgRect, trackThickness * 0.5f);

        if (slider.isEnabled())
        {
            const float fillLeft  = static_cast<float>(x);
            const float fillRight = sliderPos;
            if (fillRight > fillLeft)
            {
                const auto fillRect = juce::Rectangle<float>(fillLeft, trackY, fillRight - fillLeft, trackThickness);
                g.setColour(trackFill);
                g.fillRoundedRectangle(fillRect, trackThickness * 0.5f);
            }
        }

        g.setColour(thumb);
        g.fillEllipse(juce::Rectangle<float>(thumbSize, thumbSize)
                          .withCentre({ sliderPos, y + height * 0.5f }));
    }
    else
    {
        // Three-value / two-value / bar-style: fall back to the base class with
        // the real min/max so its secondary thumbs draw correctly.
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                               sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}

void CustomLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& /*backgroundColour*/,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    auto base = button.getToggleState()
        ? button.findColour(juce::TextButton::buttonOnColourId)
        : button.findColour(juce::TextButton::buttonColourId);

    if (shouldDrawButtonAsDown)
        base = base.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        base = base.brighter(0.12f);

    g.setColour(base);
    g.fillRoundedRectangle(bounds, Theme::cornerRadius);

    g.setColour(Theme::grid);
    g.drawRoundedRectangle(bounds, Theme::cornerRadius, 1.0f);
}

void CustomLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                     int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                     juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, Theme::cornerRadius);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, Theme::cornerRadius, 1.0f);

    // Down-arrow on the right
    const float arrowSize = 8.0f;
    juce::Rectangle<float> arrowZone(bounds.getRight() - 18.0f, bounds.getCentreY() - arrowSize * 0.5f,
                                     arrowSize, arrowSize);
    juce::Path arrow;
    arrow.addTriangle(arrowZone.getX(), arrowZone.getY(),
                      arrowZone.getRight(), arrowZone.getY(),
                      arrowZone.getCentreX(), arrowZone.getBottom());
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.fillPath(arrow);
}

juce::Font CustomLookAndFeel::getTextButtonFont(juce::TextButton&, int /*buttonHeight*/)
{
    return juce::Font(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
}

juce::Font CustomLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(Theme::fontLabel));
}
