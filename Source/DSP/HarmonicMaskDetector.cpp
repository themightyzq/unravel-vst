#include "HarmonicMaskDetector.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// HarmonicMaskDetector
// =============================================================================
// Mirrors MaskEstimator's proven HPSS + Wiener math, but on the LONG analysis
// grid and emitting ONLY the tonal probability:
//   - horizontal median across time frames per bin   -> H (sustained guide)
//   - vertical median across a frequency window       -> P (broadband guide)
//   - Wiener gain = H^2 / (H^2 + P^2), then ^maskExponent for sharpness.
//
// At long-FFT resolution a sustained tone occupies very few bins with a
// negligible window skirt, so H stays high at the tone bin across frames while
// the surrounding bins (and thus the vertical median P) stay low -> the bin
// reads strongly tonal. Empty bins have H ~ 0 -> tonal ~ 0.
//
// RT-safety: everything is pre-allocated in prepare(); process() does NO
// allocation and NO locks. Medians run in the pre-allocated scratch_ buffer.
// =============================================================================

namespace
{
    // Match MaskEstimator's constants for consistency.
    constexpr float kEps = 1e-8f;                  // numerical stability epsilon
    constexpr int   kVerticalMedianSize = 13;      // mirror MaskEstimator::verticalMedianSize

    // NOTE: mutates data[]
    // nth_element median, mirroring MaskEstimator::computeMedian().
    float computeMedian (float* data, int size) noexcept
    {
        if (size <= 0)
            return 0.0f;
        if (size == 1)
            return data[0];

        const int medianIndex = size / 2;
        std::nth_element (data, data + medianIndex, data + size);

        if (size % 2 == 1)
            return data[medianIndex];

        const float median1 = data[medianIndex];
        std::nth_element (data, data + medianIndex - 1, data + size);
        const float median2 = data[medianIndex - 1];
        return (median1 + median2) * 0.5f;
    }
}

void HarmonicMaskDetector::prepare (int numBins) noexcept
{
    jassert (numBins > 0);

    numBins_ = numBins;

    historyData_.assign (static_cast<size_t> (kMedianFrames) * static_cast<size_t> (numBins), 0.0f);
    scratch_.assign (static_cast<size_t> (std::max (kMedianFrames, kVerticalMedianSize)), 0.0f);

    writeIndex_ = 0;
    framesReceived_ = 0;
}

void HarmonicMaskDetector::reset() noexcept
{
    if (numBins_ <= 0)
        return;

    std::fill (historyData_.begin(), historyData_.end(), 0.0f);
    writeIndex_ = 0;
    framesReceived_ = 0;
}

void HarmonicMaskDetector::setSeparation (float amount01) noexcept
{
    separation_ = juce::jlimit (0.0f, 1.0f, amount01);
}

void HarmonicMaskDetector::process (juce::Span<const float> magnitudes,
                                    juce::Span<float> tonalMaskOut) noexcept
{
    jassert (magnitudes.size() == static_cast<size_t> (numBins_));
    jassert (tonalMaskOut.size() == static_cast<size_t> (numBins_));

    const int numBins = numBins_;

    // 1) Write the incoming magnitude frame into the history ring.
    float* writePos = historyData_.data() + (static_cast<size_t> (writeIndex_) * static_cast<size_t> (numBins));
    std::copy (magnitudes.data(), magnitudes.data() + numBins, writePos);
    const int currentIndex = writeIndex_;  // most-recently-written frame
    writeIndex_ = (writeIndex_ + 1) % kMedianFrames;
    if (framesReceived_ < kMedianFrames)
        ++framesReceived_;

    const float* currentFrame = historyData_.data()
                              + (static_cast<size_t> (currentIndex) * static_cast<size_t> (numBins));

    // Mask exponent from separation amount, identical curve to MaskEstimator
    // (computeMasks): y = 0.3 + 2t + 2.7t^2.
    const float t = separation_;
    const float maskExponent = 0.3f + t * (2.0f + t * 2.7f);

    const float minPower = kEps * 100.0f;  // minimum power floor (mirror MaskEstimator)
    const int validFrames = std::max (1, framesReceived_);
    const int halfWindow = kVerticalMedianSize / 2;

    for (int bin = 0; bin < numBins; ++bin)
    {
        // 2) Horizontal median across time frames for this bin -> H (sustained guide).
        // Gather only the VALID frames from the ring (newest going back).
        for (int f = 0; f < validFrames; ++f)
        {
            // f = 0 -> oldest valid frame, validFrames-1 -> current frame.
            const int frameSlot = ((currentIndex - (validFrames - 1) + f) % kMedianFrames + kMedianFrames) % kMedianFrames;
            scratch_[static_cast<size_t> (f)] =
                historyData_[static_cast<size_t> (frameSlot) * static_cast<size_t> (numBins)
                             + static_cast<size_t> (bin)];
        }
        const float H = computeMedian (scratch_.data(), validFrames);

        // 3) Vertical median across a frequency window of the CURRENT frame -> P.
        const int startBin = std::max (0, bin - halfWindow);
        const int endBin = std::min (numBins, bin + halfWindow + 1);
        const int windowSize = endBin - startBin;
        for (int i = 0; i < windowSize; ++i)
            scratch_[static_cast<size_t> (i)] = currentFrame[startBin + i];
        const float P = computeMedian (scratch_.data(), windowSize);

        // 4) Wiener-style tonal decision, floored to minPower for stability.
        float tonalPower = std::max (H * H, minPower);
        float noisePower = std::max (P * P, minPower);

        const float totalPower = tonalPower + noisePower;
        const float wienerGain = (totalPower > kEps)
            ? std::clamp (tonalPower / totalPower, 0.0f, 1.0f)
            : 0.0f;  // NOT 0.5 — match MaskEstimator's no-phantom-share fallback.

        const float sharpened = std::pow (std::max (wienerGain, 0.0f), maskExponent);
        float mask = std::isfinite (sharpened) ? sharpened : 0.0f;
        tonalMaskOut[static_cast<size_t> (bin)] = juce::jlimit (0.0f, 1.0f, mask);
    }
}
