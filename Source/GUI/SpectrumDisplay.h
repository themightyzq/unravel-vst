#pragma once

#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include <vector>
#include <functional>
#include "Theme.h"

/**
 * SpectrumDisplay - Real-time spectral visualization for tonal/transient/noise
 * separation.
 *
 * Displays:
 * - Magnitude spectrum (neutral LCD-green silhouette)
 * - Tonal mask overlay (sky blue, solid outline)
 * - Transient mask overlay (yellow, dashed outline)
 * - Noise mask overlay (purple, dotted outline)
 *
 * ZQ SFX house-UI migration: the background is now the house phosphor screen
 * (zqsfx::ui::LookAndFeel::drawScreen) instead of a flat fill, the grid is
 * colour::lcdFaint2, and all hand-drawn text routes through the house
 * silkFont/lcdFont via drawLcdText where the LookAndFeel can be reached (style
 * guide section 5: "screen text via drawLcdText/lcdFont"). The three streams are
 * distinguishable without colour: each mask region's top boundary is also stroked
 * with a distinct line style (tonal solid, transient dashed, noise dotted).
 *
 * Features:
 * - Logarithmic/Linear frequency scaling (toggleable)
 * - Decibel magnitude scaling
 * - Frequency labels (Hz/kHz)
 * - Smooth visual updates at 30Hz
 * - Efficient rendering using cached paths
 */
class SpectrumDisplay : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    // Callback that fills the four buffers with a consistent snapshot of the
    // latest analysis frame (magnitudes + tonal/transient/noise masks). Returns
    // false if no data is available. The display owns the buffers — it never
    // reads the processor's live DSP storage.
    using SnapshotCallback = std::function<bool(std::vector<float>& /* magnitudes */,
                                                std::vector<float>& /* tonalMask */,
                                                std::vector<float>& /* transientMask */,
                                                std::vector<float>& /* noiseMask */)>;

    SpectrumDisplay();
    ~SpectrumDisplay() override;

    /** Set the callback used to pull a thread-safe spectrum snapshot from the processor. */
    void setSnapshotCallback(SnapshotCallback cb);

    /**
     * Set the sample rate for accurate frequency display.
     * @param sampleRate Current sample rate in Hz
     */
    void setSampleRate(double sampleRate);

    /**
     * Toggle between logarithmic and linear frequency scaling.
     * @param useLog True for logarithmic, false for linear
     */
    void setLogScale(bool useLog);

    /**
     * Get current scale mode.
     * @return True if using logarithmic scale
     */
    bool isLogScale() const { return useLogScale; }

    /**
     * Enable or disable the display.
     * When disabled, shows placeholder text and stops updates.
     */
    void setEnabled(bool shouldBeEnabled);

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    // Pause the 30 FPS timer whenever the display is not actually on screen
    // (editor closed/obscured by the host) — saves CPU when nothing is visible.
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    // Timer callback for visual updates
    void timerCallback() override;

    // Start/stop the refresh timer to match "enabled AND showing".
    void updateTimerState();

    // Data callback (thread-safe snapshot) + scratch buffers it fills.
    SnapshotCallback getSnapshot;
    std::vector<float> snapMag_;
    std::vector<float> snapTonal_;
    std::vector<float> snapTransient_;
    std::vector<float> snapNoise_;

    // Display settings
    static constexpr float minDb = -80.0f;
    static constexpr float maxDb = 0.0f;
    static constexpr float dbRange = maxDb - minDb;

    // Cached display data
    std::vector<float> displayMagnitudes;
    std::vector<float> displayTonalMask;
    std::vector<float> displayTransientMask;
    std::vector<float> displayNoiseMask;
    int cachedNumBins = 0;

    // Smoothing for visual display
    static constexpr float smoothingCoeff = 0.3f;

    // State
    bool isEnabled = true;
    bool hasSignal_ = false;   // true once the snapshot carries non-trivial energy
    bool useLogScale = true;  // Default to logarithmic
    double currentSampleRate = 48000.0;

    // Drawing helpers
    void drawBackground(juce::Graphics& g);
    void drawSpectrum(juce::Graphics& g);
    void drawMasks(juce::Graphics& g);
    void drawLabels(juce::Graphics& g);
    void drawFrequencyLabels(juce::Graphics& g);

    // Utility — a single frequency→x mapping (true log or linear) keeps the
    // spectrum, the grid lines, and the frequency labels all consistent.
    float freqToX(float freq, float width) const;
    float binToX(int bin, int totalBins, float width) const;
    float dbToY(float db, float height) const;
    float magnitudeToDb(float magnitude) const;
    float binToFrequency(int bin, int totalBins) const;
    juce::String formatFrequency(float freq) const;

    // Colors
    const juce::Colour gridColour       { Theme::grid };                       // == colour::lcdFaint2 (was Theme::bgLight)
    const juce::Colour spectrumColour   { zqsfx::ui::colour::lcdFaint };        // neutral magnitude fill (was raw 0xff444444)
    const juce::Colour tonalColour      { Theme::tonal.withAlpha(0.53f) };      // translucent tonal overlay
    const juce::Colour transientColour  { Theme::transient.withAlpha(0.6f) };   // translucent transient overlay
    const juce::Colour noiseColour      { Theme::noise.withAlpha(0.53f) };      // translucent noise overlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
