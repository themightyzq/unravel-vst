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

void SpectrumDisplay::setSnapshotCallback(SnapshotCallback cb)
{
    getSnapshot = std::move(cb);
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

void SpectrumDisplay::timerCallback()
{
    if (!isEnabled || !getSnapshot)
        return;

    // Pull a consistent snapshot of the latest analysis frame into our own buffers.
    if (!getSnapshot(snapMag_, snapTonal_, snapTransient_, snapNoise_))
        return;

    const int numBins = static_cast<int>(snapMag_.size());
    if (numBins <= 0)
        return;

    // Resize display buffers if the bin count changed
    if (cachedNumBins != numBins)
    {
        cachedNumBins = numBins;
        displayMagnitudes.resize(static_cast<size_t>(numBins), 0.0f);
        displayTonalMask.resize(static_cast<size_t>(numBins), 0.33f);
        displayTransientMask.resize(static_cast<size_t>(numBins), 0.33f);
        displayNoiseMask.resize(static_cast<size_t>(numBins), 0.34f);
    }

    // Detect whether the snapshot carries any real energy (silent / bypassed
    // frames are all zeros) so paint() can show a "waiting for audio" hint.
    float maxMag = 0.0f;
    for (float m : snapMag_)
        maxMag = juce::jmax(maxMag, m);
    hasSignal_ = maxMag > 1.0e-3f;

    // Smooth the display data toward the snapshot
    for (size_t i = 0; i < snapMag_.size() && i < displayMagnitudes.size(); ++i)
        displayMagnitudes[i] = displayMagnitudes[i] * (1.0f - smoothingCoeff) + snapMag_[i] * smoothingCoeff;

    for (size_t i = 0; i < snapTonal_.size() && i < displayTonalMask.size(); ++i)
        displayTonalMask[i] = displayTonalMask[i] * (1.0f - smoothingCoeff) + snapTonal_[i] * smoothingCoeff;

    for (size_t i = 0; i < snapTransient_.size() && i < displayTransientMask.size(); ++i)
        displayTransientMask[i] = displayTransientMask[i] * (1.0f - smoothingCoeff) + snapTransient_[i] * smoothingCoeff;

    for (size_t i = 0; i < snapNoise_.size() && i < displayNoiseMask.size(); ++i)
        displayNoiseMask[i] = displayNoiseMask[i] * (1.0f - smoothingCoeff) + snapNoise_[i] * smoothingCoeff;

    repaint();
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    drawBackground(g);

    if (!isEnabled)
    {
        g.setColour(juce::Colour(0xff666666));
        g.setFont(juce::FontOptions(Theme::fontLabel));
        g.drawText("Spectrum Display", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // cachedNumBins is set on the first successful snapshot, so it doubles as
    // a "have any frames been published yet?" flag — no separate `hasValidData`
    // tracking needed.
    if (cachedNumBins > 0)
    {
        drawSpectrum(g);
        drawMasks(g);
    }

    drawLabels(g);

    // Empty state: nothing flowing yet (silent / bypassed) — tell the user the
    // display is alive and waiting rather than just showing a flat line.
    if (!hasSignal_)
    {
        g.setColour(Theme::textDim);
        g.setFont(juce::FontOptions(Theme::fontSmall));
        g.drawText("Waiting for audio…", getLocalBounds(), juce::Justification::centred);
    }
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

    // Draw frequency grid lines at musical frequencies, using the same freqToX
    // mapping as the labels and the spectrum so everything lines up.
    const float nyquist = static_cast<float>(currentSampleRate * 0.5);
    const float freqMarkers[] = {100.0f, 1000.0f, 10000.0f};

    for (float freq : freqMarkers)
    {
        if (freq > 20.0f && freq < nyquist)
            g.drawVerticalLine(static_cast<int>(freqToX(freq, width)), 0.0f, height);
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

    if (cachedNumBins <= 1)
        return;

    // Bottom "mask ribbon": at each frequency the band [bandTop..bottom] is split
    // into the three streams' actual shares — tonal (blue) at the bottom,
    // transient (yellow) in the middle, noise (orange) on top. Because the masks
    // are mass-conserving (tonal + transient + noise = 1), the three regions
    // exactly fill the band, faithfully showing the per-frequency split.
    const float bandH   = height * 0.18f;
    const float bandTop = height - bandH;

    auto splitTonalY = [&](int bin)
    {
        // Top of the tonal region = bottom - tonal * bandH
        const float t = juce::jlimit(0.0f, 1.0f, displayTonalMask[bin]);
        return height - t * bandH;
    };
    auto splitTransientY = [&](int bin)
    {
        // Top of the (tonal + transient) stack = bottom - (tonal+transient)*bandH
        const float t  = juce::jlimit(0.0f, 1.0f, displayTonalMask[bin]);
        const float tr = juce::jlimit(0.0f, 1.0f, displayTransientMask[bin]);
        return height - juce::jmin(1.0f, t + tr) * bandH;
    };

    // Each path extends to x=0 using bin-1's split height so the three regions
    // close vertically at the left edge — otherwise the curve from bin 1 back
    // to x=0 would slope diagonally and leave a visible mass-conservation gap
    // in the leftmost (~5% in LOG mode) strip.

    // Noise share (orange): bandTop (straight top) down to the tonal+transient split.
    juce::Path noisePath;
    noisePath.startNewSubPath(0.0f, bandTop);
    noisePath.lineTo(width, bandTop);
    for (int bin = cachedNumBins - 1; bin >= 1; --bin)
        noisePath.lineTo(binToX(bin, cachedNumBins, width), splitTransientY(bin));
    noisePath.lineTo(0.0f, splitTransientY(1)); // vertical close at left edge
    noisePath.closeSubPath();
    g.setColour(noiseColour);
    g.fillPath(noisePath);

    // Transient share (yellow): between the (tonal+transient) top curve and the tonal top curve.
    juce::Path transientPath;
    transientPath.startNewSubPath(0.0f, splitTransientY(1));   // start at x=0, top of stack
    for (int bin = 1; bin < cachedNumBins; ++bin)
        transientPath.lineTo(binToX(bin, cachedNumBins, width), splitTransientY(bin));
    for (int bin = cachedNumBins - 1; bin >= 1; --bin)
        transientPath.lineTo(binToX(bin, cachedNumBins, width), splitTonalY(bin));
    transientPath.lineTo(0.0f, splitTonalY(1));                // vertical close at left edge
    transientPath.closeSubPath();
    g.setColour(transientColour);
    g.fillPath(transientPath);

    // Tonal share (blue): between the tonal top curve and the bottom (straight).
    juce::Path tonalPath;
    tonalPath.startNewSubPath(0.0f, height);
    tonalPath.lineTo(width, height);
    for (int bin = cachedNumBins - 1; bin >= 1; --bin)
        tonalPath.lineTo(binToX(bin, cachedNumBins, width), splitTonalY(bin));
    tonalPath.lineTo(0.0f, splitTonalY(1)); // vertical close at left edge
    tonalPath.closeSubPath();
    g.setColour(tonalColour);
    g.fillPath(tonalPath);
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

    // Legend at top — three streams, in the same order as the ribbon stacks
    const int legendY = 5;

    g.setColour(tonalColour.withAlpha(1.0f));
    g.fillRect(5, legendY, 8, 8);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Tonal", 15, legendY - 1, 50, 12, juce::Justification::left);

    g.setColour(transientColour.withAlpha(1.0f));
    g.fillRect(60, legendY, 8, 8);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Transient", 70, legendY - 1, 60, 12, juce::Justification::left);

    g.setColour(noiseColour.withAlpha(1.0f));
    g.fillRect(130, legendY, 8, 8);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Noise", 140, legendY - 1, 50, 12, juce::Justification::left);

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

    // Musical frequency markers, positioned with the same freqToX mapping the
    // spectrum and grid use (so labels sit exactly under their grid lines in
    // both LOG and LIN modes).
    const float nyquist = static_cast<float>(currentSampleRate * 0.5);
    const float freqMarkers[] = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f};

    for (float freq : freqMarkers)
    {
        if (freq > 20.0f && freq < nyquist)
        {
            const float x = freqToX(freq, width);
            if (x > 25.0f && x < width - 35.0f)
                g.drawText(formatFrequency(freq),
                          static_cast<int>(x) - 20, labelY, 40, 12,
                          juce::Justification::centred);
        }
    }
}

float SpectrumDisplay::binToFrequency(int bin, int totalBins) const
{
    if (totalBins <= 1) return 0.0f;
    const float nyquist = static_cast<float>(currentSampleRate * 0.5);
    // Bin (totalBins-1) maps to nyquist for a real FFT (numBins = fftSize/2 + 1).
    return (static_cast<float>(bin) / static_cast<float>(totalBins - 1)) * nyquist;
}

juce::String SpectrumDisplay::formatFrequency(float freq) const
{
    if (freq >= 1000.0f)
        return juce::String(freq / 1000.0f, 1) + "k";
    else
        return juce::String(static_cast<int>(freq));
}

float SpectrumDisplay::freqToX(float freq, float width) const
{
    const float nyquist = static_cast<float>(currentSampleRate * 0.5);
    if (nyquist <= 0.0f || width <= 0.0f)
        return 0.0f;

    if (useLogScale)
    {
        // True log-frequency axis from 20 Hz to Nyquist (equal pixels per octave).
        const float fMin = 20.0f;
        const float f = juce::jlimit(fMin, nyquist, freq);
        const float x = (std::log10(f / fMin) / std::log10(nyquist / fMin)) * width;
        return juce::jlimit(0.0f, width, x);
    }

    // Linear: 0..Nyquist across the full width.
    return juce::jlimit(0.0f, width, (freq / nyquist) * width);
}

float SpectrumDisplay::binToX(int bin, int totalBins, float width) const
{
    return freqToX(binToFrequency(bin, totalBins), width);
}

float SpectrumDisplay::dbToY(float db, float height) const
{
    const float normalized = (db - minDb) / dbRange;
    return height * (1.0f - juce::jlimit(0.0f, 1.0f, normalized));
}

float SpectrumDisplay::magnitudeToDb(float magnitude) const
{
    if (magnitude <= 0.0f) return minDb;

    // Approximate dBFS. The analysis-frame bin magnitude for a full-scale sine
    // through a Hann-windowed FFT peaks near fftSize/4, so normalise by that
    // reference instead of treating the raw bin magnitude as dBFS.
    const int fftSize = (cachedNumBins > 1) ? 2 * (cachedNumBins - 1) : 2048;
    const float reference = static_cast<float>(fftSize) * 0.25f;
    const float db = 20.0f * std::log10(magnitude / reference);
    return juce::jlimit(minDb, maxDb, db);
}
