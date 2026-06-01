#include "MaskReconciler.h"
#include <cmath>
#include <algorithm>

void MaskReconciler::prepare (int numBinsLong, int numBinsShort) noexcept
{
    numBinsLong_  = numBinsLong;
    numBinsShort_ = numBinsShort;

    startBin_.resize ((size_t) numBinsShort);
    endBin_.resize   ((size_t) numBinsShort);

    if (numBinsShort <= 1)
    {
        // Degenerate case: single output bin — average the entire long mask.
        startBin_[0] = 0;
        endBin_[0]   = numBinsLong;
        return;
    }

    const double ratio = (double) (numBinsLong - 1) / (double) (numBinsShort - 1);

    for (int s = 0; s < numBinsShort; ++s)
    {
        // Long-grid center for this short bin.
        // Averaging window covers [(s-0.5)*ratio, (s+0.5)*ratio].
        const double lo = (s - 0.5) * ratio;
        const double hi = (s + 0.5) * ratio;

        int start = (int) std::floor (lo);
        int end   = (int) std::ceil  (hi);

        // Clamp: start >= 0, end <= numBinsLong, window always >= 1 bin.
        start = std::max (0, start);
        end   = std::min (numBinsLong, std::max (start + 1, end));

        startBin_[(size_t) s] = start;
        endBin_[(size_t) s]   = end;
    }
}

void MaskReconciler::map (juce::Span<const float> longMask,
                          juce::Span<float>       shortMaskOut) const noexcept
{
    for (int s = 0; s < numBinsShort_; ++s)
    {
        const int start = startBin_[(size_t) s];
        const int end   = endBin_[(size_t) s];
        const int count = end - start;

        float sum = 0.0f;
        for (int b = start; b < end; ++b)
            sum += longMask[(size_t) b];

        shortMaskOut[(size_t) s] = sum / (float) count;
    }
}
