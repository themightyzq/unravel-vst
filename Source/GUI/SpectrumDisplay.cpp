#include "SpectrumDisplay.h"
#include <cmath>

namespace
{
    // Every string this component draws by hand goes through the house LookAndFeel's
    // lcdFont/drawLcdText (the phosphor-screen face + glow) when one is installed,
    // falling back to a plain generic-font drawText otherwise so this never crashes or
    // draws nothing if some other LookAndFeel is ever active (style guide section 4:
    // "screen text via drawLcdText/lcdFont").
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

    // Size of every screen label on the spectrum (legend, dB scale, frequency scale, empty-state
    // message). The house LCD face (VT323) is drawn from a point height and its capitals are only
    // about 0.55 of that: the old 10 pt labels measured roughly 5.5 px cap height on the snapshot.
    // 18 pt gives roughly 10 px, the floor for readable screen text.
    constexpr float screenTextPx = 18.0f;
    constexpr int screenTextRowH = 20;   // box height that holds one line at screenTextPx
    constexpr int glyphInsetY = 4;       // rows shrink by this top and bottom to approximate glyph height

    // Axis label: screen text on a small backing chip in the screen's own glass colour, so the
    // grid line that runs through the label's anchor (a horizontal dB line, a vertical decade
    // line) does not strike through the glyphs at this size.
    void drawAxisLabel(juce::Graphics& g, juce::Component& c, const juce::String& text,
                       juce::Rectangle<int> area, juce::Justification just)
    {
        g.setColour(zqsfx::ui::colour::lcdBg.withAlpha(0.85f));
        g.fillRect(area.reduced(0, glyphInsetY).expanded(1, 0));
        drawScreenText(g, c, text, area, screenTextPx, just, Theme::textDim);
    }

    // Width in px of `text` as drawScreenText() will draw it at `px`, so the legend can lay its
    // entries out from the real glyph widths (the house LCD face and the generic fallback face
    // differ) instead of hard-coded x offsets.
    int screenTextWidth(juce::Component& c, const juce::String& text, float px)
    {
        const auto* lnf = dynamic_cast<zqsfx::ui::LookAndFeel*>(&c.getLookAndFeel());
        const juce::Font font = lnf != nullptr ? lnf->lcdFont(px) : juce::Font(juce::FontOptions(px));
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        return static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth())) + 2;
    }

    // Stroke a stream's mask-region boundary with a distinct line style so the three
    // streams are distinguishable without colour (style guide section 3 rule 2):
    // tonal solid, transient dashed, noise dotted.
    void strokeStreamBoundary(juce::Graphics& g, const juce::Path& boundary, juce::Colour colour,
                              int streamIndex)
    {
        constexpr float thickness = 1.5f;
        if (streamIndex == 0) // tonal: solid
        {
            g.setColour(colour);
            g.strokePath(boundary, juce::PathStrokeType(thickness));
            return;
        }

        juce::Path dashed;
        if (streamIndex == 1) // transient: dashed
        {
            float dashLengths[] = { 6.0f, 3.0f };
            juce::PathStrokeType(thickness).createDashedStroke(dashed, boundary, dashLengths, 2);
        }
        else // noise: dotted
        {
            float dashLengths[] = { 1.5f, 2.5f };
            juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
                .createDashedStroke(dashed, boundary, dashLengths, 2);
        }
        g.setColour(colour);
        g.fillPath(dashed);
    }
}

SpectrumDisplay::SpectrumDisplay()
{
    setOpaque(true);
    // Timer is started lazily once the component is actually shown — see
    // updateTimerState() / visibilityChanged(). Starting it here would burn
    // 30 FPS of CPU before the editor is ever on screen.

    // Accessibility support
    setAccessible(true);
    setTitle("Spectrum Display");
    setDescription("Real-time frequency visualization showing tonal (sky blue, solid), "
                   "transient (yellow, dashed), and noise (purple, dotted) components.");
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
    updateTimerState();
    repaint();
}

void SpectrumDisplay::visibilityChanged()
{
    updateTimerState();
}

void SpectrumDisplay::parentHierarchyChanged()
{
    // Catches the display being added to / removed from a shown window (e.g.
    // editor open/close). Note: a host merely minimising or occluding its
    // window does not reliably fire this — that case is not paused here.
    updateTimerState();
}

void SpectrumDisplay::updateTimerState()
{
    const bool shouldRun = isEnabled && isShowing();
    if (shouldRun && ! isTimerRunning())
        startTimerHz(30);  // 30 FPS for smooth visuals
    else if (! shouldRun && isTimerRunning())
        stopTimer();
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
        drawScreenText(g, *this, "Spectrum Display", getLocalBounds(), Theme::fontLabel,
                      juce::Justification::centred, Theme::textDim.darker(0.3f));
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
        // ASCII only: a raw UTF-8 ellipsis in a char* literal goes through
        // juce::String's Latin-1 constructor and renders as mojibake.
        drawScreenText(g, *this, "Waiting for audio...", getLocalBounds(), screenTextPx,
                      juce::Justification::centred, Theme::textDim);
    }
}

void SpectrumDisplay::resized()
{
    // Nothing specific needed
}

void SpectrumDisplay::drawBackground(juce::Graphics& g)
{
    // House phosphor-screen treatment (bezel + LCD glass + scanlines) instead of a
    // flat fill (style guide section 5: custom displays get the screen background).
    zqsfx::ui::LookAndFeel::drawScreen(g, getLocalBounds().toFloat(), true);

    auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    // Draw frequency grid lines (logarithmic). gridColour == colour::lcdFaint2; drawn
    // at reduced alpha so the grid stays subtle against the phosphor glow rather than
    // as strong as the mask outlines drawn on top of it.
    g.setColour(gridColour.withAlpha(0.5f));

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
        const float db = magnitudeToDb(displayMagnitudes[static_cast<size_t>(bin)]);
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
    // into the three streams' actual shares — tonal (sky blue) at the bottom,
    // transient (yellow) in the middle, noise (purple) on top. Because the masks
    // are mass-conserving (tonal + transient + noise = 1), the three regions
    // exactly fill the band, faithfully showing the per-frequency split.
    // The band height itself is weighted by the bin's magnitude (0 at the
    // display floor, full height at 0 dBFS), so the ribbon only shows a split
    // where there is actually energy to split — a mask share of silence is
    // meaningless and used to paint the whole width regardless of signal.
    const float bandH = height * 0.18f;

    auto energyWeight = [&](int bin)
    {
        return juce::jlimit(0.0f, 1.0f, (magnitudeToDb(displayMagnitudes[static_cast<size_t>(bin)]) - minDb) / dbRange);
    };
    auto splitTonalY = [&](int bin)
    {
        // Top of the tonal region = bottom - tonal * weightedBand
        const float t = juce::jlimit(0.0f, 1.0f, displayTonalMask[static_cast<size_t>(bin)]);
        return height - t * energyWeight(bin) * bandH;
    };
    auto splitTransientY = [&](int bin)
    {
        // Top of the (tonal + transient) stack = bottom - (tonal+transient) * weightedBand
        const float t  = juce::jlimit(0.0f, 1.0f, displayTonalMask[static_cast<size_t>(bin)]);
        const float tr = juce::jlimit(0.0f, 1.0f, displayTransientMask[static_cast<size_t>(bin)]);
        return height - juce::jmin(1.0f, t + tr) * energyWeight(bin) * bandH;
    };
    auto ribbonTopY = [&](int bin)
    {
        // Top of the whole (mass-conserving) stack = bottom - weightedBand
        return height - energyWeight(bin) * bandH;
    };

    // Each path extends to x=0 using bin-1's split height so the three regions
    // close vertically at the left edge — otherwise the curve from bin 1 back
    // to x=0 would slope diagonally and leave a visible mass-conservation gap
    // in the leftmost (~5% in LOG mode) strip.

    // Noise share (purple): energy-weighted stack top down to the tonal+transient split.
    juce::Path noisePath;
    noisePath.startNewSubPath(0.0f, ribbonTopY(1));
    for (int bin = 1; bin < cachedNumBins; ++bin)
        noisePath.lineTo(binToX(bin, cachedNumBins, width), ribbonTopY(bin));
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

    // Tonal share (sky blue): between the tonal top curve and the bottom (straight).
    juce::Path tonalPath;
    tonalPath.startNewSubPath(0.0f, height);
    tonalPath.lineTo(width, height);
    for (int bin = cachedNumBins - 1; bin >= 1; --bin)
        tonalPath.lineTo(binToX(bin, cachedNumBins, width), splitTonalY(bin));
    tonalPath.lineTo(0.0f, splitTonalY(1)); // vertical close at left edge
    tonalPath.closeSubPath();
    g.setColour(tonalColour);
    g.fillPath(tonalPath);

    // Non-colour cue (style guide section 3 rule 2 / accessibility floor item 5):
    // stroke each stream's own top boundary curve with a distinct line style so the
    // three streams stay distinguishable under a colour-blindness simulation, not
    // just by hue — tonal solid, transient dashed, noise dotted. Built as OPEN paths
    // (not the closed fill regions above) so each reads as a single curve.
    juce::Path tonalBoundary;
    tonalBoundary.startNewSubPath(0.0f, splitTonalY(1));
    for (int bin = 1; bin < cachedNumBins; ++bin)
        tonalBoundary.lineTo(binToX(bin, cachedNumBins, width), splitTonalY(bin));
    strokeStreamBoundary(g, tonalBoundary, Theme::tonal, 0);

    juce::Path transientBoundary;
    transientBoundary.startNewSubPath(0.0f, splitTransientY(1));
    for (int bin = 1; bin < cachedNumBins; ++bin)
        transientBoundary.lineTo(binToX(bin, cachedNumBins, width), splitTransientY(bin));
    strokeStreamBoundary(g, transientBoundary, Theme::transient, 1);

    juce::Path noiseBoundary;
    noiseBoundary.startNewSubPath(0.0f, ribbonTopY(1));
    for (int bin = 1; bin < cachedNumBins; ++bin)
        noiseBoundary.lineTo(binToX(bin, cachedNumBins, width), ribbonTopY(bin));
    strokeStreamBoundary(g, noiseBoundary, Theme::noise, 2);
}

void SpectrumDisplay::drawLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float height = bounds.getHeight();

    // dB labels on right side — same dbToY mapping as the grid lines in
    // drawBackground, so each label sits exactly on its line. The 0 dB label
    // is nudged inside the top edge instead of skipped. LCD readout style
    // (numeric value on the phosphor screen). Right-aligned against dbLabelRightMargin so the
    // widest label ("-20 dB") ends inside the display; the LOG/LIN toggle (PluginEditor::resized)
    // sits left of that column. The rects are remembered so the frequency labels can steer clear.
    //
    // Density: the loop runs bottom to top, so the 0 dB label (nudged down from the top edge) is
    // placed last and is dropped when it would sit on the -20 dB label. That happens on short
    // displays (about 88 px or less, i.e. the minimum and default editor heights); the 0 dB line
    // is the display's top border and the -20/-40/-60 labels carry the scale. Rects are shrunk
    // vertically to the glyph height for the test, so labels one row apart still count as clear.
    dbLabelRects.clear();
    for (float db = minDb + 20.0f; db <= maxDb; db += 20.0f)
    {
        const float y = dbToY(db, height);
        const int textY = juce::jlimit(2, getHeight() - screenTextRowH - 2,
                                       static_cast<int>(y) - screenTextRowH / 2);
        const juce::String text = juce::String(static_cast<int>(db)) + " dB";
        const int textW = screenTextWidth(*this, text, screenTextPx);
        const juce::Rectangle<int> area { getWidth() - dbLabelRightMargin - textW, textY, textW, screenTextRowH };

        bool clear = true;
        for (const auto& placed : dbLabelRects)
            clear = clear && ! area.reduced(0, glyphInsetY).intersects(placed.reduced(0, glyphInsetY));
        if (! clear)
            continue;

        drawAxisLabel(g, *this, text, area, juce::Justification::centredRight);
        dbLabelRects.push_back(area);
    }

    // Legend at top — three streams, in the same order as the ribbon stacks. Each
    // swatch is followed by its name (colour is never the only signal — the dash
    // pattern on the curve itself is the other cue, per strokeStreamBoundary above).
    //
    // Size: screenTextPx (see the anonymous namespace). Entries are laid out left to right from measured text widths, so they cannot overlap at any
    // editor size; the whole row is about 250 px wide, clear of the LOG toggle (at x 372 of the
    // 460 px wide display at the 480 px minimum editor width).
    constexpr float legendFontPx = screenTextPx;
    constexpr int legendY = 3;
    constexpr int legendRowH = screenTextRowH;
    constexpr int swatchSize = 10;
    constexpr int swatchGap = 4;
    constexpr int entryGap = 14;

    int legendX = 5;
    const auto drawLegendEntry = [&](const juce::String& name, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(1.0f));
        g.fillRect(legendX, legendY + (legendRowH - swatchSize) / 2, swatchSize, swatchSize);
        legendX += swatchSize + swatchGap;

        const int textW = screenTextWidth(*this, name, legendFontPx);
        drawScreenText(g, *this, name, { legendX, legendY, textW, legendRowH }, legendFontPx,
                      juce::Justification::centredLeft, Theme::textDim);
        legendX += textW + entryGap;
    };

    drawLegendEntry("Tonal",     tonalColour);
    drawLegendEntry("Transient", transientColour);
    drawLegendEntry("Noise",     noiseColour);

    // Draw frequency labels
    drawFrequencyLabels(g);
}

void SpectrumDisplay::drawFrequencyLabels(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const float width = static_cast<float>(bounds.getWidth());

    // Musical frequency markers, positioned with the same freqToX mapping the
    // spectrum and grid use (so labels sit exactly under their grid lines in
    // both LOG and LIN modes).
    //
    // Density: at 18 pt a label is 30 to 40 px wide, and adjacent markers are only about 45 px
    // apart on the 460 px wide minimum display, so not every marker fits. Decade markers
    // (100, 1k, 10k) are placed first, then the rest wherever they clear everything already
    // placed: earlier labels, and the dB scale column (its rects are recorded by drawLabels).
    // A label is also skipped if it would leave the display. Rects are shrunk vertically by
    // clashInsetY only, so a frequency label that would sit within a few px under a dB label
    // (the 10.0k label at the default height) is dropped rather than left cramped.
    const float nyquist = static_cast<float>(currentSampleRate * 0.5);
    const int labelY = bounds.getHeight() - screenTextRowH;
    constexpr int labelGap = 6;
    constexpr int clashInsetY = 2;   // tighter than glyphInsetY: keeps a few px between rows

    std::vector<juce::Rectangle<int>> occupied;
    for (const auto& r : dbLabelRects)
        occupied.push_back(r.reduced(0, clashInsetY));

    const auto tryPlace = [&](float freq)
    {
        if (! (freq > 20.0f && freq < nyquist))
            return;

        const juce::String text = formatFrequency(freq);
        const int textW = screenTextWidth(*this, text, screenTextPx);
        const int x = static_cast<int>(freqToX(freq, width));
        const juce::Rectangle<int> area { x - textW / 2, labelY, textW, screenTextRowH };
        if (area.getX() < 2 || area.getRight() > bounds.getWidth() - 2)
            return;

        const auto padded = area.reduced(0, clashInsetY).expanded(labelGap / 2, 0);
        for (const auto& o : occupied)
            if (padded.intersects(o))
                return;

        drawAxisLabel(g, *this, text, area, juce::Justification::centred);
        occupied.push_back(area.reduced(0, clashInsetY));
    };

    for (float freq : { 100.0f, 1000.0f, 10000.0f })
        tryPlace(freq);
    for (float freq : { 50.0f, 200.0f, 500.0f, 2000.0f, 5000.0f, 20000.0f })
        tryPlace(freq);
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
