#pragma once
#include <JuceHeader.h>
#include <vector>

// Maps a mask defined on a long FFT grid (numBinsLong) onto a short FFT grid
// (numBinsShort) by frequency-domain resampling. Both grids span 0..Nyquist,
// so short bin s covers frequency fraction s/(numBinsShort-1); we average the
// long-grid mask over the long bins falling in that fraction's neighborhood.
class MaskReconciler
{
public:
    MaskReconciler() = default;  // explicit default ctor required by Apple Clang when copy ctor is deleted

    void prepare (int numBinsLong, int numBinsShort) noexcept;
    void map (juce::Span<const float> longMask, juce::Span<float> shortMaskOut) const noexcept;

private:
    int numBinsLong_ = 0, numBinsShort_ = 0;
    std::vector<int> startBin_, endBin_;  // per short bin, the long-bin averaging window [start, end)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaskReconciler)
};
