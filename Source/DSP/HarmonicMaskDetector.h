#pragma once
#include <JuceHeader.h>
#include <vector>

// Long-grid harmonic detector. Maintains a horizontal-median history over the
// long-FFT magnitude spectrum and emits a per-bin TONAL probability in [0,1]:
// sustained (harmonic) energy -> ~1, transient/broadband -> ~0.
class HarmonicMaskDetector
{
public:
    HarmonicMaskDetector() = default;             // explicit default ctor required by Apple Clang 17 when copy ctor is deleted
    void prepare (int numBins) noexcept;          // numBins = longFftSize/2 + 1
    void reset() noexcept;
    void setSeparation (float amount01) noexcept; // sharpness of the tonal decision

    // Push one long-grid magnitude frame and write the tonal mask (size numBins).
    void process (juce::Span<const float> magnitudes, juce::Span<float> tonalMaskOut) noexcept;

private:
    static constexpr int kMedianFrames = 17;  // long-window horizontal median (sustained-tone bias)
    int numBins_ = 0;
    float separation_ = 0.85f;
    std::vector<float> historyData_;   // kMedianFrames * numBins flat ring
    std::vector<float> scratch_;       // median workspace
    int writeIndex_ = 0;
    int framesReceived_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicMaskDetector)
};
