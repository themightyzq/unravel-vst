#pragma once

#include <JuceHeader.h>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <random>

/**
 * True sinusoidal + residual decomposition processor.
 * Based on McAulay-Quatieri sinusoidal modeling.
 * This performs analysis-synthesis, not filtering.
 */
class SinusoidalModelProcessor
{
public:
    struct SinusoidalTrack
    {
        int id;                     // Unique track ID
        float frequency;            // Current frequency (Hz)
        float amplitude;            // Current amplitude
        float phase;                // Current phase (radians)
        float prevFrequency;        // Previous frame frequency
        float prevAmplitude;        // Previous frame amplitude
        float prevPhase;            // Previous frame phase
        int birthFrame;             // Frame where track started
        int age;                    // Number of frames alive
        bool isActive;              // Currently active
    };
    
    struct SpectralPeak
    {
        float frequency;            // Precise frequency (Hz)
        float amplitude;            // Peak amplitude
        float phase;                // Peak phase
        int bin;                    // FFT bin index
    };
    
    SinusoidalModelProcessor();
    ~SinusoidalModelProcessor() = default;
    
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    
    /**
     * Process audio block with sinusoidal decomposition.
     * @param inputBuffer Input audio
     * @param outputBuffer Output audio (reconstructed)
     * @param tonalBuffer Tonal component output
     * @param noiseBuffer Noise component output
     * @param numSamples Number of samples to process
     * @param tonalGain Gain for tonal component
     * @param noiseGain Gain for noise component
     */
    void processBlock(const float* inputBuffer,
                     float* outputBuffer,
                     float* tonalBuffer,
                     float* noiseBuffer,
                     int numSamples,
                     float tonalGain,
                     float noiseGain) noexcept;
    
private:
    static constexpr int fftSize = 2048;
    static constexpr int fftOrder = 11;  // 2^11 = 2048
    static constexpr int hopSize = 128;  // Smaller hop for better time resolution
    static constexpr int maxTracks = 60; // Maximum simultaneous sinusoidal tracks
    static constexpr float minPeakMagnitude = 0.001f;
    static constexpr float maxFreqDeviation = 50.0f; // Hz
    
    double sampleRate = 48000.0;
    int currentBlockSize = 512;
    
    // FFT processing
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> analysisWindow;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> synthesisWindow;
    
    // Buffers
    std::vector<float> inputRingBuffer;
    std::vector<float> outputRingBuffer;
    std::vector<float> tonalRingBuffer;
    std::vector<float> noiseRingBuffer;
    
    std::vector<float> fftBuffer;
    std::vector<float> analysisFrame;
    std::vector<float> synthesisFrame;
    std::vector<float> residualFrame;
    
    // Ring buffer positions
    int inputWritePos = 0;
    int outputReadPos = 0;
    int samplesUntilNextFrame = hopSize;
    
    // Sinusoidal tracking
    std::vector<SinusoidalTrack> activeTracks;
    std::deque<std::vector<SinusoidalTrack>> trackHistory;
    int nextTrackId = 1;
    int frameCounter = 0;
    
    // Spectral envelope for noise modeling
    std::vector<float> spectralEnvelope;
    std::vector<float> noisePhaseRandomizer;
    
    // Random number generator for noise
    mutable std::random_device randomDevice;
    mutable std::mt19937 randomGenerator;
    mutable std::uniform_real_distribution<float> uniformDist;
    
    /**
     * Perform sinusoidal analysis on current frame.
     * @return Vector of detected peaks
     */
    std::vector<SpectralPeak> analyzeFrame() noexcept;
    
    /**
     * Find spectral peaks with parabolic interpolation.
     * @param magnitudes Magnitude spectrum
     * @param phases Phase spectrum
     * @param numBins Number of FFT bins
     * @return Vector of peaks with precise frequencies
     */
    std::vector<SpectralPeak> findSpectralPeaks(const float* magnitudes,
                                                const float* phases,
                                                int numBins) noexcept;
    
    /**
     * Parabolic interpolation for sub-bin frequency accuracy.
     * @param left Left bin magnitude
     * @param center Center bin magnitude (peak)
     * @param right Right bin magnitude
     * @param peakBin Peak bin index
     * @param[out] interpolatedFreq Precise frequency
     * @param[out] interpolatedMag Precise magnitude
     */
    void parabolicInterpolation(float left, float center, float right,
                               int peakBin,
                               float& interpolatedFreq,
                               float& interpolatedMag) noexcept;
    
    /**
     * Track peaks across frames to form sinusoidal tracks.
     * @param currentPeaks Peaks in current frame
     */
    void updateTracks(const std::vector<SpectralPeak>& currentPeaks) noexcept;
    
    /**
     * Synthesize sinusoidal tracks for current frame.
     * @param outputFrame Output buffer for synthesized sinusoids
     * @param frameSize Size of frame
     */
    void synthesizeSinusoids(float* outputFrame, int frameSize) noexcept;
    
    /**
     * Model residual as filtered noise.
     * @param residualFrame Input residual signal
     * @param noiseFrame Output noise model
     * @param frameSize Size of frame
     */
    void modelResidualNoise(const float* residualFrame,
                          float* noiseFrame,
                          int frameSize) noexcept;
    
    /**
     * Extract spectral envelope using cepstral smoothing.
     * @param magnitudes Magnitude spectrum
     * @param numBins Number of bins
     * @param envelope Output envelope
     */
    void extractSpectralEnvelope(const float* magnitudes,
                                int numBins,
                                float* envelope) noexcept;
    
    /**
     * Generate phase for each track using cubic interpolation.
     * @param track Sinusoidal track
     * @param numSamples Number of samples to generate
     * @param[out] phases Output phase values
     */
    void generateTrackPhase(const SinusoidalTrack& track,
                          int numSamples,
                          float* phases) noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SinusoidalModelProcessor)
};