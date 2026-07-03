#include "LowFreqPartialTracker.h"

#include <algorithm>
#include <cmath>

void LowFreqPartialTracker::prepare(int numBins, double sampleRate) noexcept
{
    jassert(numBins > 1);
    jassert(sampleRate > 0.0);

    numBins_    = numBins;
    sampleRate_ = sampleRate;

    const int fftSize = (numBins - 1) * 2;
    binHz_ = sampleRate / static_cast<double>(fftSize);

    // Frame-rate normalization (REVIEW-QA QA-M1): the hop is architecturally
    // fftSize/4 (75% overlap). Rescale the per-frame temporal constants so the
    // confirm/release/fade times in seconds match the 48 kHz-tuned reference.
    // At exactly 48 kHz these come out identical to the Ref values.
    const double hopSize   = static_cast<double>(std::max(1, fftSize / 4));
    const double frameRate = sampleRate / hopSize;
    const double ratio     = frameRate / kRefFrameRate;

    confirmFrames_ = std::max(1, static_cast<int>(std::lround(kConfirmFramesRef * ratio)));
    releaseFrames_ = std::max(1, static_cast<int>(std::lround(kReleaseFramesRef * ratio)));

    // Match the per-second fade of the reference gain step:
    // (1 - s_new)^frameRate == (1 - s_ref)^refFrameRate.
    gainStep_ = static_cast<float>(
        1.0 - std::pow(1.0 - static_cast<double>(kGainStepRef), kRefFrameRate / frameRate));

    // Lowest bins only: every bin whose centre is at or below kMaxTrackHz, plus
    // one guard bin so a peak at the edge still has a right-hand neighbour for
    // parabolic interpolation.
    scanBins_ = std::min(numBins,
                         static_cast<int>(std::ceil(kMaxTrackHz / binHz_)) + 2);

    // The override can reach up to the harmonic-extension ceiling plus the
    // furthest possible skirt write: a harmonic's centre can land up to
    // kHarmSearchBins above its expected bin, and its skirt extends
    // kHarmSkirtRadius beyond that. Keeping this bound at (or past) the write
    // reach keeps the clear/write/apply regions symmetric.
    overrideBins_ = std::min(numBins,
                             static_cast<int>(std::ceil(kMaxHarmonicHz / binHz_))
                                 + kHarmSearchBins + kHarmSkirtRadius + 2);

    overrideMask_.assign(static_cast<size_t>(numBins), 0.0f);
    peakFreqHz_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);
    peakBinPos_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);
    peakMag_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);
    floorScratch_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);

    reset();
}

void LowFreqPartialTracker::reset() noexcept
{
    tracks_.fill(Track{});
    peakCount_ = 0;
    std::fill(overrideMask_.begin(), overrideMask_.end(), 0.0f);
}

void LowFreqPartialTracker::process(juce::Span<const float> magnitudes) noexcept
{
    jassert(magnitudes.size() == static_cast<size_t>(numBins_));

    detectPeaks(magnitudes);
    updateTracks();
    trackHarmonics(magnitudes);
    rebuildOverride(magnitudes);
}

void LowFreqPartialTracker::detectPeaks(juce::Span<const float> magnitudes) noexcept
{
    peakCount_ = 0;

    // Strongest low-band magnitude sets the prominence threshold, so a quiet
    // frame (silence) yields no peaks rather than chasing the numerical floor.
    float maxLowMag = 0.0f;
    for (int b = 0; b < scanBins_; ++b)
        maxLowMag = std::max(maxLowMag, magnitudes[static_cast<size_t>(b)]);

    if (maxLowMag <= kEps)
        return;

    // Median of the low band is the broadband floor. A real tone towers over it;
    // a broadband-noise "peak" sits only a few× above it. Requiring a peak to
    // exceed kFloorFactor × this median is the tonality gate that keeps the
    // tracker from locking onto flat noise. Assumes a tracked partial occupies
    // only a small fraction of the low band (true below kMaxTrackHz), so its
    // skirt doesn't lift the median enough to dilute the gate.
    for (int b = 0; b < scanBins_; ++b)
        floorScratch_[static_cast<size_t>(b)] = magnitudes[static_cast<size_t>(b)];
    const auto mid = floorScratch_.begin() + scanBins_ / 2;
    std::nth_element(floorScratch_.begin(), mid, floorScratch_.begin() + scanBins_);
    const float bandFloor = *mid;

    const float threshold = std::max(kProminence * maxLowMag, kFloorFactor * bandFloor);

    for (int b = 1; b < scanBins_ - 1; ++b)
    {
        const float m0 = magnitudes[static_cast<size_t>(b - 1)];
        const float m1 = magnitudes[static_cast<size_t>(b)];
        const float m2 = magnitudes[static_cast<size_t>(b + 1)];

        // Strict local maximum, prominent above both the strongest peak and the
        // band-median tonality floor.
        if (m1 <= m0 || m1 <= m2 || m1 < threshold)
            continue;

        // Parabolic (quadratic) interpolation for sub-bin peak location.
        const float denom  = m0 - 2.0f * m1 + m2;
        float offset = (std::abs(denom) > kEps) ? 0.5f * (m0 - m2) / denom : 0.0f;
        offset = juce::jlimit(-0.5f, 0.5f, offset);

        const float binPos = static_cast<float>(b) + offset;
        const float freqHz = binPos * static_cast<float>(binHz_);
        if (freqHz > static_cast<float>(kMaxTrackHz))
            continue;

        peakFreqHz_[static_cast<size_t>(peakCount_)] = freqHz;
        peakBinPos_[static_cast<size_t>(peakCount_)] = binPos;
        peakMag_[static_cast<size_t>(peakCount_)]    = m1;
        ++peakCount_;
    }
}

void LowFreqPartialTracker::updateTracks() noexcept
{
    std::array<bool, kMaxTracks> touched {};   // matched-or-spawned this frame

    // Match each peak to the nearest-in-frequency active track within the
    // continuity tolerance; otherwise spawn a new track in a free slot.
    for (int p = 0; p < peakCount_; ++p)
    {
        const float freqHz = peakFreqHz_[static_cast<size_t>(p)];

        int   best     = -1;
        float bestDev  = static_cast<float>(kMaxFreqDevHz);
        for (int t = 0; t < kMaxTracks; ++t)
        {
            if (! tracks_[static_cast<size_t>(t)].active || touched[static_cast<size_t>(t)])
                continue;
            const float dev = std::abs(tracks_[static_cast<size_t>(t)].freqHz - freqHz);
            if (dev <= bestDev)
            {
                bestDev = dev;
                best    = t;
            }
        }

        if (best >= 0)
        {
            Track& tr = tracks_[static_cast<size_t>(best)];
            tr.freqHz  = freqHz;
            tr.binPos  = peakBinPos_[static_cast<size_t>(p)];
            tr.mag     = peakMag_[static_cast<size_t>(p)];
            tr.age     = std::min(tr.age + 1, 1 << 20);
            tr.missing = 0;
            touched[static_cast<size_t>(best)] = true;
        }
        else
        {
            for (int t = 0; t < kMaxTracks; ++t)
            {
                Track& tr = tracks_[static_cast<size_t>(t)];
                if (! tr.active)
                {
                    tr = Track{};
                    tr.active  = true;
                    tr.freqHz  = freqHz;
                    tr.binPos  = peakBinPos_[static_cast<size_t>(p)];
                    tr.mag     = peakMag_[static_cast<size_t>(p)];
                    tr.age     = 1;
                    touched[static_cast<size_t>(t)] = true;
                    break;
                }
            }
            // No free slot: a transient flurry of low peaks is dropped rather
            // than evicting an established hum. The established tracks win.
        }
    }

    // Age unmatched tracks, smooth each track's override gain toward its target,
    // and retire tracks that have faded out after going unmatched too long.
    for (int t = 0; t < kMaxTracks; ++t)
    {
        Track& tr = tracks_[static_cast<size_t>(t)];
        if (! tr.active)
            continue;

        if (! touched[static_cast<size_t>(t)])
            ++tr.missing;

        const bool confirmed = tr.age >= confirmFrames_ && tr.missing <= releaseFrames_;
        const float target   = confirmed ? 1.0f : 0.0f;
        tr.gain += (target - tr.gain) * gainStep_;

        if (tr.missing > releaseFrames_ && tr.gain < 0.01f)
            tr = Track{};   // fully faded and long gone — free the slot
    }
}

void LowFreqPartialTracker::trackHarmonics(juce::Span<const float> magnitudes) noexcept
{
    // Verify and age the harmonic series of each confirmed track (see the
    // class comment: a dense comb defeats the vertical median at EVERY
    // harmonic, so the proven periodicity of the fundamental is extended
    // upward under strict per-harmonic gates).
    for (Track& tr : tracks_)
    {
        if (! tr.active)
            continue;

        const bool trackConfirmed = tr.age >= confirmFrames_ && tr.missing <= releaseFrames_;
        const float spacingBins   = static_cast<float>(tr.freqHz / binHz_);

        // Valley probes sit halfway to the neighbouring harmonics (at least
        // clear of the peak's own main lobe, which is ±2 bins for Hann).
        const float valleyDist = std::max(2.0f, 0.5f * spacingBins);

        for (int k = 0; k < kMaxHarmonics; ++k)
        {
            Harmonic& h = tr.harmonics[static_cast<size_t>(k)];
            const int harmonicIndex = k + 2;
            const double freqHz = static_cast<double>(tr.freqHz) * harmonicIndex;
            const float expected = static_cast<float>(tr.binPos) * static_cast<float>(harmonicIndex);
            const int centre = static_cast<int>(std::lround(expected));

            const bool inRange = trackConfirmed
                                 && freqHz <= kMaxHarmonicHz
                                 && centre - kHarmSearchBins >= 1
                                 && centre + kHarmSearchBins < numBins_ - 1;

            bool verified = false;
            if (inRange)
            {
                // Strongest strict local maximum within the search window.
                int   bestBin = -1;
                float bestMag = 0.0f;
                for (int b = centre - kHarmSearchBins; b <= centre + kHarmSearchBins; ++b)
                {
                    const float m0 = magnitudes[static_cast<size_t>(b - 1)];
                    const float m1 = magnitudes[static_cast<size_t>(b)];
                    const float m2 = magnitudes[static_cast<size_t>(b + 1)];
                    if (m1 > m0 && m1 > m2 && m1 > bestMag)
                    {
                        bestBin = b;
                        bestMag = m1;
                    }
                }

                if (bestBin >= 0 && bestMag >= kHarmMagRel * tr.mag)
                {
                    // Sub-bin position via the same parabolic interpolation as
                    // the fundamental detector.
                    const float m0 = magnitudes[static_cast<size_t>(bestBin - 1)];
                    const float m2 = magnitudes[static_cast<size_t>(bestBin + 1)];
                    const float denom = m0 - 2.0f * bestMag + m2;
                    float offset = (std::abs(denom) > kEps) ? 0.5f * (m0 - m2) / denom : 0.0f;
                    offset = juce::jlimit(-0.5f, 0.5f, offset);
                    const float pos = static_cast<float>(bestBin) + offset;

                    // Gate 1: harmonic relationship — the peak sits where the
                    // fundamental predicts (tolerance absorbs interp bias × k).
                    const bool atHarmonic = std::abs(pos - expected) <= kHarmTolBins;

                    // Gate 2: the harmonic's own frame-to-frame stability (the
                    // same discriminator that separates the fundamental from
                    // noise; 75%-overlap noise can hold a peak for a frame or
                    // two but not at a stable sub-bin position). A fresh run
                    // (run == 0) re-seeds the position; an ongoing run must
                    // stay put.
                    const bool stable = h.run == 0 || h.pos < 0.0f
                                        || std::abs(pos - h.pos) <= kHarmStabBins;

                    // Gate 3: prominence over the inter-harmonic valley — a
                    // real partial towers over the gap between partials.
                    const int vLo = juce::jlimit(1, numBins_ - 1,
                                                 static_cast<int>(std::lround(pos - valleyDist)));
                    const int vHi = juce::jlimit(1, numBins_ - 1,
                                                 static_cast<int>(std::lround(pos + valleyDist)));
                    const float valley = std::min(magnitudes[static_cast<size_t>(vLo)],
                                                  magnitudes[static_cast<size_t>(vHi)]);
                    const bool prominent = bestMag >= kHarmProminence * valley;

                    if (atHarmonic && stable && prominent)
                    {
                        verified = true;
                        h.pos  = pos;
                        h.run  = std::min(h.run + 1, 1 << 20);
                        h.miss = 0;
                    }
                }
            }

            if (! verified)
            {
                // Tolerate short dropouts (a consonant burst perturbs one or
                // two frames) exactly like the fundamental's release window;
                // only a sustained absence resets the confirmation run. The
                // last position is kept so the fading gain keeps painting
                // where the harmonic was (and re-seeds via run == 0 above).
                h.miss = std::min(h.miss + 1, 1 << 20);
                if (h.miss > releaseFrames_)
                    h.run = 0;
            }

            const bool claimed = h.run >= confirmFrames_ && h.miss <= releaseFrames_;
            const float target = claimed ? tr.gain : 0.0f;
            h.gain += (target - h.gain) * gainStep_;
        }
    }
}

void LowFreqPartialTracker::rebuildOverride(juce::Span<const float> magnitudes) noexcept
{
    const int hi = std::min(numBins_, overrideBins_);
    std::fill(overrideMask_.begin(), overrideMask_.begin() + hi, 0.0f);

    int   lowestCenter = -1;
    float lowestGain   = 0.0f;

    for (const Track& tr : tracks_)
    {
        if (! tr.active || tr.gain <= kEps)
            continue;

        const int center = static_cast<int>(std::lround(tr.binPos));

        for (int k = -kSkirtRadius; k <= kSkirtRadius; ++k)
        {
            const int bin = center + k;
            if (bin < 0 || bin >= numBins_)
                continue;
            // Gentle taper across the skirt so the edges don't over-claim
            // neighbouring noise, while the partial itself is fully claimed.
            const float falloff = 1.0f - 0.1f * static_cast<float>(std::abs(k));
            const float v = tr.gain * falloff;
            overrideMask_[static_cast<size_t>(bin)] =
                std::max(overrideMask_[static_cast<size_t>(bin)], v);
        }

        // Claim each verified harmonic. The skirt is MAGNITUDE-CONDITIONAL
        // rather than tapered: a fixed taper leaves ~10% of the mask open at
        // the ±1-bin skirt, and since a Hann main lobe puts ~half the partial's
        // amplitude there, that capped the achievable rejection at ~−26 dB per
        // harmonic (measured). Instead, every bin within the skirt radius that
        // is partial-DOMINATED — at least kHarmSkirtRel of the harmonic's
        // centre-bin magnitude in the current frame — is claimed at full track
        // gain; valley bins below that stay with the noise stream, so the gaps
        // between partials keep passing broadband beds through.
        for (const Harmonic& h : tr.harmonics)
        {
            if (h.gain <= kEps || h.pos < 0.0f)
                continue;

            const int hCenter = juce::jlimit(0, numBins_ - 1,
                                             static_cast<int>(std::lround(h.pos)));
            const float centreMag = std::max(magnitudes[static_cast<size_t>(hCenter)], kEps);

            for (int k = -kHarmSkirtRadius; k <= kHarmSkirtRadius; ++k)
            {
                const int bin = hCenter + k;
                if (bin < 0 || bin >= numBins_)
                    continue;
                if (k != 0 && magnitudes[static_cast<size_t>(bin)] < kHarmSkirtRel * centreMag)
                    continue;
                overrideMask_[static_cast<size_t>(bin)] =
                    std::max(overrideMask_[static_cast<size_t>(bin)], h.gain);
            }
        }

        if (lowestCenter < 0 || center < lowestCenter)
        {
            lowestCenter = center;
            lowestGain   = tr.gain;
        }
    }

    // The energy below the lowest confirmed partial is that partial's lower
    // leakage skirt (a 2048 STFT spreads a low tone across several bins). Claim
    // it down to DC at full gain (no taper) so the hum's lower tail leaves the
    // noise stream too — this sub-fundamental region is dominated by the
    // partial's tail, not independent content, and the span stays small because
    // tracked partials are bounded by kMaxTrackHz.
    if (lowestCenter > 0)
    {
        const int top = std::min(lowestCenter, numBins_);
        for (int bin = 0; bin < top; ++bin)
            overrideMask_[static_cast<size_t>(bin)] =
                std::max(overrideMask_[static_cast<size_t>(bin)], lowestGain);
    }
}

void LowFreqPartialTracker::applyOverride(juce::Span<float> tonalMask) const noexcept
{
    jassert(tonalMask.size() == static_cast<size_t>(numBins_));

    const int hi = std::min(numBins_, overrideBins_);
    for (int b = 0; b < hi; ++b)
        tonalMask[static_cast<size_t>(b)] =
            std::max(tonalMask[static_cast<size_t>(b)], overrideMask_[static_cast<size_t>(b)]);
}
