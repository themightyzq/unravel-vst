#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

/**
 * LowFreqPartialTracker — recovers sustained low-frequency tonal content that
 * the HPSS median classifier misses.
 *
 * At low frequencies a 2048-point STFT has too few bins (≈ 23 Hz/bin) for the
 * vertical-median "percussive" guide to separate a hum from the broadband bed:
 * the tone's own leakage skirt fills the median window, so the Wiener mask
 * under-classifies the hum as tonal and it bleeds into the noise stream.
 *
 * This tracker uses the signature the median ignores — temporal frequency
 * stability. A real hum holds a precise, steady frequency frame after frame
 * (recovered sub-bin via parabolic interpolation); broadband noise does not.
 * Peaks below kMaxTrackHz that stay frequency-stable for kConfirmFrames are
 * "confirmed" and produce a per-bin override that pulls the tonal mask up at
 * the partial and its skirt — so confirmed low hums leave the noise stream.
 *
 * It augments HPSS rather than replacing it: it only ever raises the tonal
 * mask (never lowers it), only acts on confirmed sustained low partials, and
 * leaves mid/high content and broadband noise untouched. Because the override
 * only reassigns skirt energy from noise to tonal (mass-conserving downstream),
 * a unity-gain full mix still reconstructs identically.
 *
 * Real-time safe: all state is fixed-size and allocated in prepare(); process()
 * and applyOverride() never allocate or lock.
 */
class LowFreqPartialTracker
{
public:
    LowFreqPartialTracker() = default;

    /**
     * @param numBins     Number of spectrum bins (fftSize/2 + 1).
     * @param sampleRate  Sample rate, for bin↔Hz conversion.
     */
    void prepare(int numBins, double sampleRate) noexcept;

    /** Clear all tracks and the override buffer. */
    void reset() noexcept;

    /**
     * Analyse one magnitude frame: pick prominent low-frequency peaks, match
     * them to existing tracks by frequency continuity, age/retire tracks, and
     * rebuild the per-bin override from confirmed tracks. Call once per frame
     * before applyOverride().
     * @param magnitudes Magnitude spectrum, size numBins.
     */
    void process(juce::Span<const float> magnitudes) noexcept;

    /**
     * Raise the tonal mask toward 1.0 wherever a confirmed sustained low
     * partial sits (mask = max(mask, override)). In place; only the low band is
     * touched. Const — reads the override built by the last process() call.
     * @param tonalMask The pre-split tonal mask (smoothedMask), size numBins.
     */
    void applyOverride(juce::Span<float> tonalMask) const noexcept;

private:
    // --- Tuning constants -----------------------------------------------------
    static constexpr int    kMaxTracks      = 8;      // simultaneous low partials tracked
    static constexpr double kMaxTrackHz     = 300.0;  // only track sustained tones below this
    static constexpr double kMaxFreqDevHz   = 6.0;    // per-frame match tolerance (tolerates slow glide, rejects noise jitter)
    static constexpr int    kConfirmFrames  = 6;      // frames of stability before a track overrides
    static constexpr int    kReleaseFrames  = 8;      // frames a track survives with no matching peak
    static constexpr int    kSkirtRadius    = 2;      // bins each side of a partial to override
    static constexpr float  kProminence     = 0.10f;  // peak must exceed kProminence × strongest low peak
    static constexpr float  kFloorFactor    = 6.0f;   // tonality gate: peak must exceed kFloorFactor × low-band median (rejects flat broadband noise)
    static constexpr float  kGainStep       = 0.25f;  // per-frame smoothing of a track's override gain
    static constexpr float  kEps            = 1e-12f;

    struct Track
    {
        bool  active   = false;
        float freqHz   = 0.0f;  // tracked (continuity-updated) frequency
        float binPos   = 0.0f;  // freqHz expressed in bins, for skirt placement
        float gain     = 0.0f;  // smoothed override strength [0,1]
        int   age      = 0;     // consecutive frames matched
        int   missing  = 0;     // consecutive frames without a match
    };

    int    numBins_   = 0;
    double sampleRate_ = 48000.0;
    double binHz_     = 0.0;   // sampleRate / fftSize
    int    scanBins_  = 0;     // number of low bins examined for peaks

    std::array<Track, kMaxTracks> tracks_ {};
    std::vector<float> overrideMask_;  // per-bin override [0,1], size numBins (only low band non-zero)

    // Per-frame scratch (sized via scanBins_, preallocated).
    std::vector<float> peakFreqHz_;
    std::vector<float> peakBinPos_;
    std::vector<float> floorScratch_;   // low-band magnitudes for the median tonality floor
    int peakCount_ = 0;

    void detectPeaks(juce::Span<const float> magnitudes) noexcept;
    void updateTracks() noexcept;
    void rebuildOverride() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LowFreqPartialTracker)
};
