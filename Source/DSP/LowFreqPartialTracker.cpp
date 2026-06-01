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

    // Lowest bins only: every bin whose centre is at or below kMaxTrackHz, plus
    // one guard bin so a peak at the edge still has a right-hand neighbour for
    // parabolic interpolation.
    scanBins_ = std::min(numBins,
                         static_cast<int>(std::ceil(kMaxTrackHz / binHz_)) + 2);

    overrideMask_.assign(static_cast<size_t>(numBins), 0.0f);
    peakFreqHz_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);
    peakBinPos_.assign(static_cast<size_t>(std::max(scanBins_, 1)), 0.0f);
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
    rebuildOverride();
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
                    tr.active  = true;
                    tr.freqHz  = freqHz;
                    tr.binPos  = peakBinPos_[static_cast<size_t>(p)];
                    tr.age     = 1;
                    tr.missing = 0;
                    tr.gain    = 0.0f;
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

        const bool confirmed = tr.age >= kConfirmFrames && tr.missing <= kReleaseFrames;
        const float target   = confirmed ? 1.0f : 0.0f;
        tr.gain += (target - tr.gain) * kGainStep;

        if (tr.missing > kReleaseFrames && tr.gain < 0.01f)
            tr = Track{};   // fully faded and long gone — free the slot
    }
}

void LowFreqPartialTracker::rebuildOverride() noexcept
{
    const int hi = std::min(numBins_, scanBins_ + kSkirtRadius + 1);
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

    const int hi = std::min(numBins_, scanBins_ + kSkirtRadius + 1);
    for (int b = 0; b < hi; ++b)
        tonalMask[static_cast<size_t>(b)] =
            std::max(tonalMask[static_cast<size_t>(b)], overrideMask_[static_cast<size_t>(b)]);
}
