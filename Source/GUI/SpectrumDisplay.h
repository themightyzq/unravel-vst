#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>

/**
 * SpectrumDisplay - Real-time spectral visualization for tonal/noise separation
 *
 * Displays:
 * - Magnitude spectrum (gray/white)
 * - Tonal mask overlay (blue)
 * - Noise mask overlay (orange)
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
    // Callback types for getting data from processor
    using MagnitudeCallback = std::function<juce::Span<const float>()>;
    using MaskCallback = std::function<juce::Span<const float>()>;
    using NumBinsCallback = std::function<int()>;

    SpectrumDisplay();
    ~SpectrumDisplay() override;

    /**
     * Set callbacks for retrieving spectrum data.
     * @param magCallback Callback returning current magnitudes
     * @param tonalCallback Callback returning current tonal mask
     * @param noiseCallback Callback returning current noise mask
     * @param numBinsCallback Callback returning number of frequency bins
     */
    void setCallbacks(MagnitudeCallback magCallback,
                     MaskCallback tonalCallback,
                     MaskCallback noiseCallback,
                     NumBinsCallback numBinsCallback);

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

    // Mouse interaction for scale toggle
    void mouseDown(const juce::MouseEvent& e) override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Timer callback for visual updates
    void timerCallback() override;

    // Data callbacks
    MagnitudeCallback getMagnitudes;
    MaskCallback getTonalMask;
    MaskCallback getNoiseMask;
    NumBinsCallback getNumBins;

    // Display settings
    static constexpr float minDb = -80.0f;
    static constexpr float maxDb = 0.0f;
    static constexpr float dbRange = maxDb - minDb;

    // Cached display data
    std::vector<float> displayMagnitudes;
    std::vector<float> displayTonalMask;
    std::vector<float> displayNoiseMask;
    int cachedNumBins = 0;

    // Smoothing for visual display
    static constexpr float smoothingCoeff = 0.3f;

    // State
    bool isEnabled = true;
    bool hasValidData = false;
    bool useLogScale = true;  // Default to logarithmic
    double currentSampleRate = 48000.0;

    // Drawing helpers
    void drawBackground(juce::Graphics& g);
    void drawSpectrum(juce::Graphics& g);
    void drawMasks(juce::Graphics& g);
    void drawLabels(juce::Graphics& g);
    void drawFrequencyLabels(juce::Graphics& g);
    void drawScaleToggle(juce::Graphics& g);

    // Utility
    float binToX(int bin, int totalBins, float width) const;
    float xToBin(float x, int totalBins, float width) const;
    float dbToY(float db, float height) const;
    float magnitudeToDb(float magnitude) const;
    float binToFrequency(int bin, int totalBins) const;
    juce::String formatFrequency(float freq) const;

    // Scale toggle button area
    juce::Rectangle<int> scaleToggleArea;

    // Colors
    juce::Colour backgroundColour{0xff0a0a0a};
    juce::Colour gridColour{0xff1a1a1a};
    juce::Colour spectrumColour{0xff444444};
    juce::Colour tonalColour{0x884488ff};
    juce::Colour noiseColour{0x88ff8844};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
