#pragma once

#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "Theme.h"

/**
 * Compact level-meter rail: three per-stream bars (tonal / noise / transient,
 * in the stream colours, each with its own text label) plus an output bar and
 * a limiter LED.
 *
 * Fed from the editor's 30 Hz timer via setLevels() with linear levels
 * (~0..1 vs full scale); the component applies its own ballistics (instant
 * rise, exponential fall) and a decaying peak-hold on the output bar. Purely
 * message-thread; the audio thread only publishes relaxed atomics upstream.
 *
 * ZQ SFX house-UI migration: each bar is now a hard-edged "well" in
 * colour::lcdScreenDark with a colour::lcdBorder rim (mirrors zqsfx::ui::PeakMeter's
 * own well treatment) instead of a rounded Theme::bgLight track. The output bar
 * gets the house's fixed-dB-anchored meter gradient (meterLo/Mid/Hi, meterHot above
 * -6 dB) since it carries clip-safety meaning, not stream identity; the three
 * per-stream bars stay flat single-hue fills in their own colour-blind-safe channel
 * (that IS their meaning). No rounded corners anywhere (style guide section 6).
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
        setTooltip("Levels: tonal (sky blue), noise (purple), transient (yellow) and "
                   "output (accent orange, with peak-hold tick). The red strip lights "
                   "when the safety limiter engages.");
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
        namespace colour = zqsfx::ui::colour;
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // Limiter LED strip at the top (near-invisible until it fires). Hard rect —
        // no rounded corners.
        auto led = bounds.withHeight(5.0f);
        g.setColour(Theme::muteOn.withAlpha(0.06f + 0.94f * limiterGlow_));
        g.fillRect(led);

        auto barArea = bounds.withTrimmedTop(8.0f).withTrimmedBottom(12.0f);
        const float gap = 3.0f;
        const float barW = (barArea.getWidth() - 3.0f * gap) / 4.0f;

        const struct { float level; juce::Colour colour; const char* label; } bars[] = {
            { tonal_,     Theme::tonal,     "T" },
            { noise_,     Theme::noise,     "N" },
            { transient_, Theme::transient, "R" },
            { out_,       Theme::accent,    nullptr }, // "OUT" drawn separately below (more room)
        };

        auto* lnf = dynamic_cast<zqsfx::ui::LookAndFeel*>(&getLookAndFeel());

        float x = barArea.getX();
        for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i)
        {
            const auto& bar = bars[i];
            const auto track = juce::Rectangle<float>(x, barArea.getY(), barW, barArea.getHeight());

            // Well: lcdScreenDark fill + lcdBorder rim (zqsfx::ui::PeakMeter's own
            // treatment), hard-edged.
            g.setColour(colour::lcdScreenDark);
            g.fillRect(track);
            g.setColour(colour::lcdBorder);
            g.drawRect(track, 1.0f);

            const auto inner = track.reduced(1.0f);
            const float h = proportionForLevel(bar.level) * inner.getHeight();
            if (h > 0.5f)
            {
                const auto fillRect = inner.withTop(inner.getBottom() - h);
                if (i == 3)
                {
                    // Output bar: fixed-dB-anchored meter gradient (clip-safety
                    // meaning, not stream identity) — green low, amber above -6 dB,
                    // matching zqsfx::ui::PeakMeter's own gradient.
                    juce::ColourGradient grad(colour::meterLo, inner.getBottomLeft(),
                                              colour::meterHi, inner.getTopLeft(), false);
                    grad.addColour(0.70, colour::meterMid);
                    g.setGradientFill(grad);
                    g.fillRect(fillRect);

                    constexpr float threshFrac = 0.90f; // -6 dB in a -60..0 dB well (54/60)
                    const float threshY = inner.getBottom() - inner.getHeight() * threshFrac;
                    if (fillRect.getY() < threshY)
                    {
                        g.setColour(colour::meterHot);
                        g.fillRect(fillRect.withBottom(threshY));
                    }
                }
                else
                {
                    // Per-stream bars: flat fill in the stream's own colour-blind-safe
                    // channel — THIS is the meaning-carrying colour, not a level scale.
                    g.setColour(bar.colour);
                    g.fillRect(fillRect);
                }
            }

            // Per-stream text label (style guide: "colour never carries meaning
            // alone" — every stream meter also names itself).
            if (bar.label != nullptr)
            {
                const auto capArea = juce::Rectangle<float>(x, bounds.getBottom() - 10.0f, barW, 10.0f);
                g.setColour(bar.colour);
                if (lnf != nullptr)
                    g.setFont(lnf->silkFont(8.0f, true));
                else
                    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
                g.drawText(bar.label, capArea, juce::Justification::centred);
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

        // "OUT" caption under the output bar (clamped inside the component); stream
        // bars carry their own single-letter caption drawn in the loop above.
        g.setColour(Theme::textDim);
        if (lnf != nullptr)
            g.setFont(lnf->silkFont(8.0f, true));
        else
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
