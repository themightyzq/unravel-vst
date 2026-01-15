#include "SpectrumDisplay.h"
#include <cmath>

SpectrumDisplay::SpectrumDisplay()
{
    setOpaque(true);
    startTimerHz(30);  // 30 FPS for smooth visuals

    // Accessibility support
    setAccessible(true);
    setTitle("Spectrum Display");
    setDescription("Real-time frequency visualization showing tonal (blue) and noise (orange) components.");
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
}

void SpectrumDisplay::setCallbacks(MagnitudeCallback magCallback,
                                   MaskCallback tonalCallback,
                                   MaskCallback noiseCallback,
                                   NumBinsCallback numBinsCallback)
{
    getMagnitudes = std::move(magCallback);
    getTonalMask = std::move(tonalCallback);
    getNoiseMask = std::move(noiseCallback);
    getNumBins = std::move(numBinsCallback);
}

void SpectrumDisplay::setEnabled(bool shouldBeEnabled)
{
    isEnabled = shouldBeEnabled;
    // Stop timer when disabled to save CPU
    if (shouldBeEnabled)
        startTimerHz(30);
    else
        stopTimer();
    repaint();
}

void SpectrumDisplay::setSampleRate(double sampleRate)
{
    currentSampleRate = sampleRate;
    repaint();
}

void SpectrumDisplay::setLogScale(bool useLog)
{
    useLogScale = useLog;
    repaint();
}

void SpectrumDisplay::mouseDown(const juce::MouseEvent& /*e*/)
{
    // Scale toggle moved to main editor footer
}

void SpectrumDisplay::timerCallback()
{
    if (!isEnabled || !getMagnitudes || !getNumBins)
        return;

    const int numBins = getNumBins();
    if (numBins <= 0)
        return;

    // Resize display buffers if needed
    if (cachedNumBins != numBins)
    {
        cachedNumBins = numBins;
        displayMagnitudes.resize(numBins, 0.0f);
        displayTonalMask.resize(numBins, 0.5f);
        displayNoiseMask.resize(numBins, 0.5f);
    }

    // Get current data
    auto magnitudes = getMagnitudes();
    auto tonalMask = getTonalMask ? getTonalMask() : juce::Span<const float>();
    auto noiseMask = getNoiseMask ? getNoiseMask() : juce::Span<const float>();

    hasValidData = !magnitudes.empty();

    if (hasValidData)
    {
        // Smooth the display data
        for (size_t i = 0; i < magnitudes.size() && i < displayMagnitudes.size(); ++i)
        {
            displayMagnitudes[i] = displayMagnitudes[i] * (1.0f - smoothingCoeff) +
                                  magnitudes[i] * smoothingCoeff;
        }

        if (!tonalMask.empty())
        {
            for (size_t i = 0; i < tonalMask.size() && i < displayTonalMask.size(); ++i)
            {
                displayTonalMask[i] = displayTonalMask[i] * (1.0f - smoothingCoeff) +
                                     tonalMask[i] * smoothingCoeff;
            }
        }

        if (!noiseMask.empty())
        {
            for (size_t i = 0; i < noiseMask.size() && i < displayNoiseMask.size(); ++i)
            {
                displayNoiseMask[i] = displayNoiseMask[i] * (1.0f - smoothingCoeff) +
                                     noiseMask[i] * smoothingCoeff;
            }
        }

        repaint();
    }
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    drawBackground(g);

    if (!isEnabled)
    {
        g.setColour(juce::Colour(0xff666666));
        g.setFont(juce::Font(12.0f));
        g.drawText("Spectrum Display", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (hasValidData && cachedNumBins > 0)
    {
        drawSpectrum(g);
        drawMasks(g);
    }

    drawLabels(g);
}

void SpectrumDisplay::resized()
{
    // Nothing specific needed
}

void SpectrumDisplay::drawBackground(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    // Draw frequency grid lines (logarithmic)
    g.setColour(gridColour);

    // Draw dB grid lines
    for (float db = minDb; db <= maxDb; db += 20.0f)
    {
        const float y = dbToY(db, height);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, width);
    }

    // Draw frequency markers at musical frequencies
    const float freqMarkers[] = {100.0f, 1000.0f, 10000.0f};
    const float nyquist = 24000.0f;  // Approximate

    for (float freq : freqMarkers)
    {
        if (freq < nyquist)
        {
            // Convert frequency to bin position (logarithmic approximation)
            const float normalizedFreq = std::log10(freq / 20.0f) / std::log10(nyquist / 20.0f);
            const float x = normalizedFreq * width;
            g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
        }
    }
}

void SpectrumDisplay::drawSpectrum(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    juce::Path spectrumPath;
    bool pathStarted = false;

    for (int bin = 1; bin < cachedNumBins; ++bin)  // Skip DC
    {
        const float x = binToX(bin, cachedNumBins, width);
        const float db = magnitudeToDb(displayMagnitudes[bin]);
        const float y = dbToY(db, height);

        if (!pathStarted)
        {
            spectrumPath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            spectrumPath.lineTo(x, y);
        }
    }

    // Close path to bottom
    spectrumPath.lineTo(width, height);
    spectrumPath.lineTo(0.0f, height);
    spectrumPath.closeSubPath();

    // Fill spectrum
    g.setColour(spectrumColour);
    g.fillPath(spectrumPath);
}

void SpectrumDisplay::drawMasks(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    // Draw tonal mask (blue, top-down from magnitude)
    juce::Path tonalPath;
    bool tonalStarted = false;

    for (int bin = 1; bin < cachedNumBins; ++bin)
    {
        const float x = binToX(bin, cachedNumBins, width);
        const float db = magnitudeToDb(displayMagnitudes[bin]);
        const float spectrumY = dbToY(db, height);
        const float maskStrength = displayTonalMask[bin];

        // Draw from top, height proportional to tonal mask
        const float maskY = spectrumY + (height - spectrumY) * (1.0f - maskStrength);

        if (!tonalStarted)
        {
            tonalPath.startNewSubPath(x, spectrumY);
            tonalStarted = true;
        }
        else
        {
            tonalPath.lineTo(x, spectrumY);
        }
    }

    // Close tonal path
    for (int bin = cachedNumBins - 1; bin >= 1; --bin)
    {
        const float x = binToX(bin, cachedNumBins, width);
        const float db = magnitudeToDb(displayMagnitudes[bin]);
        const float spectrumY = dbToY(db, height);
        const float maskStrength = displayTonalMask[bin];
        const float maskY = spectrumY + (height - spectrumY) * maskStrength * 0.5f;
        tonalPath.lineTo(x, maskY);
    }
    tonalPath.closeSubPath();

    g.setColour(tonalColour);
    g.fillPath(tonalPath);

    // Draw noise mask (orange, from bottom up)
    juce::Path noisePath;
    bool noiseStarted = false;

    for (int bin = 1; bin < cachedNumBins; ++bin)
    {
        const float x = binToX(bin, cachedNumBins, width);
        const float maskStrength = displayNoiseMask[bin];
        const float maskY = height - (height * 0.15f * maskStrength);

        if (!noiseStarted)
        {
            noisePath.startNewSubPath(x, height);
            noiseStarted = true;
        }
        noisePath.lineTo(x, maskY);
    }

    noisePath.lineTo(width, height);
    noisePath.closeSubPath();

    g.setColour(noiseColour);
    g.fillPath(noisePath);
}

void SpectrumDisplay::drawLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float height = bounds.getHeight();

    g.setColour(juce::Colour(0xff888888));  // Improved contrast
    g.setFont(juce::FontOptions(10.0f));    // Minimum readable size

    // dB labels on right side
    for (float db = minDb + 20.0f; db <= maxDb; db += 20.0f)
    {
        const float y = dbToY(db, height - 16.0f);  // Leave room for freq labels
        g.drawText(juce::String(static_cast<int>(db)) + " dB",
                  getWidth() - 35, static_cast<int>(y) - 6, 30, 12,
                  juce::Justification::right);
    }

    // Legend at top
    const int legendY = 5;
    g.setColour(tonalColour.withAlpha(1.0f));
    g.fillRect(5, legendY, 8, 8);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Tonal", 15, legendY - 1, 40, 12, juce::Justification::left);

    g.setColour(noiseColour.withAlpha(1.0f));
    g.fillRect(60, legendY, 8, 8);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Noise", 70, legendY - 1, 40, 12, juce::Justification::left);

    // Draw frequency labels
    drawFrequencyLabels(g);
}

void SpectrumDisplay::drawFrequencyLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const float width = static_cast<float>(bounds.getWidth());
    const int labelY = bounds.getHeight() - 14;

    g.setColour(juce::Colour(0xff888888));  // Consistent contrast
    g.setFont(juce::FontOptions(10.0f));    // Minimum readable size

    // Frequency markers to display
    const float nyquist = static_cast<float>(currentSampleRate / 2.0);

    if (useLogScale)
    {
        // Logarithmic scale: show musical frequencies
        const float freqMarkers[] = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f};

        for (float freq : freqMarkers)
        {
            if (freq < nyquist && freq > 20.0f)
            {
                // Calculate bin from frequency
                const int bin = static_cast<int>((freq / nyquist) * static_cast<float>(cachedNumBins > 0 ? cachedNumBins : 512));
                const float x = binToX(bin, cachedNumBins > 0 ? cachedNumBins : 512, width);

                // Only draw if within bounds and not too close to edges
                if (x > 25.0f && x < width - 35.0f)
                {
                    g.drawText(formatFrequency(freq),
                              static_cast<int>(x) - 20, labelY, 40, 12,
                              juce::Justification::centred);
                }
            }
        }
    }
    else
    {
        // Linear scale: show evenly spaced frequencies
        const int numLabels = 5;
        for (int i = 1; i < numLabels; ++i)
        {
            const float freq = (static_cast<float>(i) / static_cast<float>(numLabels)) * nyquist;
            const float x = (static_cast<float>(i) / static_cast<float>(numLabels)) * width;

            if (x > 25.0f && x < width - 35.0f)
            {
                g.drawText(formatFrequency(freq),
                          static_cast<int>(x) - 20, labelY, 40, 12,
                          juce::Justification::centred);
            }
        }
    }
}

void SpectrumDisplay::drawScaleToggle(juce::Graphics& g)
{
    // Draw scale toggle button in top right
    scaleToggleArea = juce::Rectangle<int>(getWidth() - 45, 3, 42, 14);

    // Background
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(scaleToggleArea.toFloat(), 3.0f);

    // Border
    g.setColour(juce::Colour(0xff444444));
    g.drawRoundedRectangle(scaleToggleArea.toFloat(), 3.0f, 1.0f);

    // Text
    g.setColour(juce::Colour(0xff00ffaa));
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(useLogScale ? "LOG" : "LIN", scaleToggleArea, juce::Justification::centred);
}

float SpectrumDisplay::binToFrequency(int bin, int totalBins) const
{
    if (totalBins <= 0) return 0.0f;
    const float nyquist = static_cast<float>(currentSampleRate / 2.0);
    return (static_cast<float>(bin) / static_cast<float>(totalBins)) * nyquist;
}

juce::String SpectrumDisplay::formatFrequency(float freq) const
{
    if (freq >= 1000.0f)
        return juce::String(freq / 1000.0f, 1) + "k";
    else
        return juce::String(static_cast<int>(freq));
}

float SpectrumDisplay::xToBin(float x, int totalBins, float width) const
{
    if (width <= 0.0f || totalBins <= 0) return 0.0f;

    if (useLogScale)
    {
        // Inverse of log scaling
        const float normalizedX = x / width;
        const float logScaled = std::pow(10.0f, normalizedX) - 1.0f;
        return (logScaled / 9.0f) * static_cast<float>(totalBins);
    }
    else
    {
        // Linear scaling
        return (x / width) * static_cast<float>(totalBins);
    }
}

float SpectrumDisplay::binToX(int bin, int totalBins, float width) const
{
    if (bin <= 0) return 0.0f;
    if (bin >= totalBins) return width;

    const float normalizedBin = static_cast<float>(bin) / static_cast<float>(totalBins);

    if (useLogScale)
    {
        // Logarithmic frequency scaling: log(1 + x*9) / log(10) for range 0-1
        const float logScaled = std::log10(1.0f + normalizedBin * 9.0f);
        return logScaled * width;
    }
    else
    {
        // Linear frequency scaling
        return normalizedBin * width;
    }
}

float SpectrumDisplay::dbToY(float db, float height) const
{
    const float normalized = (db - minDb) / dbRange;
    return height * (1.0f - juce::jlimit(0.0f, 1.0f, normalized));
}

float SpectrumDisplay::magnitudeToDb(float magnitude) const
{
    if (magnitude <= 0.0f) return minDb;
    const float db = 20.0f * std::log10(magnitude);
    return juce::jlimit(minDb, maxDb, db);
}
