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
 * Peaks below kMaxTrackHz that stay frequency-stable for the confirm window
 * (~64 ms; kConfirmFramesRef frames at the 48 kHz reference frame rate) are
 * "confirmed" and produce a per-bin override that pulls the tonal mask up at
 * the partial and its skirt — so confirmed low hums leave the noise stream.
 *
 * It augments HPSS rather than replacing it: it only ever raises the tonal
 * mask (never lowers it), only acts on confirmed sustained low partials, and
 * leaves mid/high content and broadband noise untouched. Because the override
 * only reassigns skirt energy from noise to tonal (mass-conserving downstream),
 * a unity-gain full mix still reconstructs identically.
 *
 * HARMONIC EXTENSION (REVIEW-UX finding 4): a dense harmonic comb — a voice
 * with f0 in this tracker's band — defeats the HPSS vertical median at EVERY
 * harmonic, not just the low ones: with partial spacing below ~8 bins the
 * 13-bin vertical-median window centred on any partial is filled with the
 * neighbouring partials' window skirts, so the "noise" guide reads comb-skirt
 * level instead of the true floor and the whole comb classifies as noise
 * (measured: f0=147 Hz retains at −11 dB in the extracted-noise output while
 * f0=330 Hz achieves −52 dB). Since the comb's periodicity is already proven
 * by the confirmed fundamental, each harmonic k·f0 (k ≥ 2, up to
 * kMaxHarmonicHz) is verified per frame — local peak at the expected
 * position, frame-to-frame position stability, prominence over the
 * inter-harmonic valley, magnitude relative to the fundamental — and claimed
 * with the same confirm/release/fade discipline as the fundamental. Noise
 * never confirms a fundamental, and unverified harmonics claim nothing, so
 * hums without harmonics and broadband beds are untouched.
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
     * partial — or a verified harmonic of one (≤ kMaxHarmonicHz) — sits
     * (mask = max(mask, override)). In place; bins above the harmonic ceiling
     * are never touched. Const — reads the override built by the last
     * process() call.
     * @param tonalMask The pre-split tonal mask (smoothedMask), size numBins.
     */
    void applyOverride(juce::Span<float> tonalMask) const noexcept;

private:
    // --- Tuning constants -----------------------------------------------------
    // TEMPORAL constants (frame counts / per-frame steps) are REFERENCE values
    // tuned at the 48 kHz / 512-hop frame rate (93.75 frames/s); prepare()
    // rescales them to the actual frame rate so confirm/release/fade times in
    // SECONDS are sample-rate invariant (REVIEW-QA QA-M1). At 48 kHz the
    // rescale is the identity.
    static constexpr double kRefFrameRate      = 48000.0 / 512.0;  // 93.75 frames/s
    static constexpr int    kMaxTracks         = 8;      // simultaneous low partials tracked
    static constexpr double kMaxTrackHz        = 300.0;  // only track sustained tones below this
    static constexpr double kMaxFreqDevHz      = 6.0;    // per-frame match tolerance (tolerates slow glide, rejects noise jitter)
    static constexpr int    kConfirmFramesRef  = 6;      // frames of stability (~64 ms) before a track overrides
    static constexpr int    kReleaseFramesRef  = 8;      // frames (~85 ms) a track survives with no matching peak
    static constexpr int    kSkirtRadius       = 2;      // bins each side of a partial to override
    static constexpr float  kProminence        = 0.10f;  // peak must exceed kProminence × strongest low peak
    static constexpr float  kFloorFactor       = 6.0f;   // tonality gate: peak must exceed kFloorFactor × low-band median (rejects flat broadband noise)
    static constexpr float  kGainStepRef       = 0.25f;  // per-frame smoothing of a track's override gain
    static constexpr float  kEps               = 1e-12f;

    // Harmonic-extension constants (see class comment). Verification gates are
    // deliberately multi-way so broadband noise cannot sustain a false claim:
    // it would need a local max at a stable sub-bin position, prominent over
    // its local valley, for confirmFrames_ consecutive frames.
    static constexpr double kMaxHarmonicHz     = 2000.0; // claim verified harmonics of a confirmed track up to here
    static constexpr int    kMaxHarmonics      = 24;     // harmonic indices 2 .. kMaxHarmonics+1
    static constexpr int    kHarmSearchBins    = 2;      // ± bins around the expected position to look for the peak
    static constexpr float  kHarmTolBins       = 1.25f;  // |detected − k·f0| tolerance (absorbs fundamental interp bias × k)
    static constexpr float  kHarmStabBins      = 0.35f;  // frame-to-frame position stability required of the harmonic itself
    static constexpr float  kHarmProminence    = 4.0f;   // harmonic peak must exceed 4× the inter-harmonic valley (12 dB)
    static constexpr float  kHarmMagRel        = 0.01f;  // and 1% (−40 dB) of the fundamental's peak magnitude
    static constexpr int    kHarmSkirtRadius   = 3;      // bins each side of a verified harmonic considered for claiming
    static constexpr float  kHarmSkirtRel      = 0.02f;  // skirt bin is claimed (full gain) if ≥ 2% (−34 dB) of the harmonic's centre bin

    struct Harmonic
    {
        float gain = 0.0f;      // smoothed override strength [0,1]
        float pos  = -1.0f;     // last verified sub-bin position (−1 = none)
        int   run  = 0;         // consecutive frames verified
        int   miss = 1 << 20;   // consecutive frames without verification
    };

    struct Track
    {
        bool  active   = false;
        float freqHz   = 0.0f;  // tracked (continuity-updated) frequency
        float binPos   = 0.0f;  // freqHz expressed in bins, for skirt placement
        float mag      = 0.0f;  // magnitude of the matched peak (harmonic gate reference)
        float gain     = 0.0f;  // smoothed override strength [0,1]
        int   age      = 0;     // consecutive frames matched
        int   missing  = 0;     // consecutive frames without a match
        std::array<Harmonic, kMaxHarmonics> harmonics {};
    };

    int    numBins_   = 0;
    double sampleRate_ = 48000.0;
    double binHz_     = 0.0;   // sampleRate / fftSize
    int    scanBins_  = 0;     // number of low bins examined for peaks
    int    overrideBins_ = 0;  // bins the override can reach (fundamental band + harmonic extension)

    // Frame-rate-normalized temporal constants (computed in prepare() from the
    // Ref values above; identical to them at 48 kHz / hop 512).
    int   confirmFrames_ = kConfirmFramesRef;
    int   releaseFrames_ = kReleaseFramesRef;
    float gainStep_      = kGainStepRef;

    std::array<Track, kMaxTracks> tracks_ {};
    std::vector<float> overrideMask_;  // per-bin override [0,1], size numBins (only low band non-zero)

    // Per-frame scratch (sized via scanBins_, preallocated).
    std::vector<float> peakFreqHz_;
    std::vector<float> peakBinPos_;
    std::vector<float> peakMag_;
    std::vector<float> floorScratch_;   // low-band magnitudes for the median tonality floor
    int peakCount_ = 0;

    void detectPeaks(juce::Span<const float> magnitudes) noexcept;
    void updateTracks() noexcept;
    void trackHarmonics(juce::Span<const float> magnitudes) noexcept;
    void rebuildOverride(juce::Span<const float> magnitudes) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LowFreqPartialTracker)
};
