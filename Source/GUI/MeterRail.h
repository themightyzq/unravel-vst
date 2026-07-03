#pragma once

#include <JuceHeader.h>
#include "Theme.h"

/**
 * Compact level-meter rail: three per-stream bars (tonal / noise / transient,
 * in the stream colours) plus an output bar and a limiter LED.
 *
 * Fed from the editor's 30 Hz timer via setLevels() with linear levels
 * (~0..1 vs full scale); the component applies its own ballistics (instant
 * rise, exponential fall) and a decaying peak-hold on the output bar. Purely
 * message-thread; the audio thread only publishes relaxed atomics upstream.
 */
class MeterRail : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    MeterRail()
    {
        setAccessible(true);
        setTitle("Level meters");
        setDescription("Per-stream and output level meters with limiter indicator.");
        setTooltip("Levels: tonal (blue), noise (orange), transient (yellow) and "
                   "output (teal, with peak-hold tick). The red strip lights when "
                   "the safety limiter engages.");
        setInterceptsMouseClicks(true, false);   // hover for the tooltip only
    }

    void setLevels(float tonal, float noise, float transient,
                   float outRms, float outPeak, bool limiterHit)
    {
        // Instant rise, ~300 ms fall at a 30 Hz feed.
        constexpr float fall = 0.82f;
        auto ballistic = [](float current, float incoming, float fallCoeff)
        {
            return incoming > current ? incoming : current * fallCoeff;
        };

        tonal_     = ballistic(tonal_,     tonal,     fall);
        noise_     = ballistic(noise_,     noise,     fall);
        transient_ = ballistic(transient_, transient, fall);
        out_       = ballistic(out_,       outRms,    fall);

        peakHold_ = juce::jmax(outPeak, peakHold_ * 0.96f);
        if (limiterHit)
            limiterGlow_ = 1.0f;
        else
            limiterGlow_ *= 0.90f;

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // Limiter LED strip at the top (near-invisible until it fires).
        auto led = bounds.withHeight(5.0f);
        g.setColour(Theme::muteOn.withAlpha(0.06f + 0.94f * limiterGlow_));
        g.fillRoundedRectangle(led, 2.0f);

        auto barArea = bounds.withTrimmedTop(8.0f).withTrimmedBottom(12.0f);
        const float gap = 3.0f;
        const float barW = (barArea.getWidth() - 3.0f * gap) / 4.0f;

        const struct { float level; juce::Colour colour; } bars[] = {
            { tonal_,     Theme::tonal },
            { noise_,     Theme::noise },
            { transient_, Theme::transient },
            { out_,       Theme::accent },
        };

        float x = barArea.getX();
        for (const auto& bar : bars)
        {
            const auto track = juce::Rectangle<float>(x, barArea.getY(), barW, barArea.getHeight());
            g.setColour(Theme::bgLight);
            g.fillRoundedRectangle(track, 2.0f);

            const float h = proportionForLevel(bar.level) * track.getHeight();
            if (h > 0.5f)
            {
                g.setColour(bar.colour);
                g.fillRoundedRectangle(track.withTop(track.getBottom() - h), 2.0f);
            }
            x += barW + gap;
        }

        // Output peak-hold tick on the last bar.
        const float peakProp = proportionForLevel(peakHold_);
        if (peakProp > 0.001f)
        {
            const float tickY = barArea.getBottom() - peakProp * barArea.getHeight();
            g.setColour(Theme::textBright);
            g.fillRect(juce::Rectangle<float>(x - barW - gap, tickY - 0.5f, barW, 1.0f));
        }

        // "OUT" caption under the output bar (clamped inside the component);
        // stream bars are colour-coded to match the spectrum legend and footer.
        g.setColour(Theme::textDim);
        g.setFont(juce::FontOptions(8.0f));
        const float capW = barW + 8.0f;
        const float capX = juce::jmin(x - barW - gap - 4.0f, bounds.getRight() - capW);
        g.drawText("OUT",
                   juce::Rectangle<float>(capX, bounds.getBottom() - 10.0f, capW, 10.0f),
                   juce::Justification::centredRight);
    }

private:
    // -60 dB..0 dB mapped to 0..1 bar height.
    static float proportionForLevel(float linear)
    {
        if (linear <= 1.0e-3f)
            return 0.0f;
        const float db = 20.0f * std::log10(linear);
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    float tonal_ = 0.0f, noise_ = 0.0f, transient_ = 0.0f, out_ = 0.0f;
    float peakHold_ = 0.0f, limiterGlow_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterRail)
};
