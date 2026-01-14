#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <complex>
#include <atomic>

/**
 * High-Performance STFT Processor for Real-Time Audio Processing
 * 
 * This is a high-performance implementation designed for the Unravel HPSS plugin.
 * It provides Short-Time Fourier Transform with overlap-add resynthesis optimized
 * for real-time audio processing with minimal latency.
 * 
 * Key Features:
 * - Real-time safe: No heap allocations in processBlock
 * - Low latency: ~32ms at 48kHz (2048/512) or ~15ms with 1024/256 configuration
 * - Phase-coherent processing with proper COLA scaling
 * - Thread-safe design (no locks needed in audio thread)
 * - Efficient memory layout optimized for modern CPUs
 * - JUCE integration with juce::dsp::FFT and WindowingFunction
 * 
 * Specifications:
 * - Default FFT Size: 2048 samples
 * - Default Hop Size: 512 samples (25% overlap, 75% hop)
 * - Window: Hann with proper COLA scaling
 * - Frequency bins: 1025 (for real FFT)
 * - Latency: fftSize - hopSize samples
 */
class STFTProcessor
{
public:
    /**
     * Configuration structure for STFT parameters.
     * Allows runtime configuration for different latency requirements.
     */
    struct Config
    {
        int fftSize = 2048;     // FFT size (must be power of 2)
        int hopSize = 512;      // Hop size (recommended: fftSize/4)
        
        // Alternative low-latency configuration
        static Config lowLatency() noexcept 
        { 
            return {1024, 256}; // ~15ms latency at 48kHz
        }
        
        // Default high-quality configuration  
        static Config highQuality() noexcept 
        { 
            return {2048, 512}; // ~32ms latency at 48kHz
        }
        
        // Validate configuration
        bool isValid() const noexcept
        {
            return fftSize > 0 && 
                   (fftSize & (fftSize - 1)) == 0 && // Power of 2
                   hopSize > 0 && 
                   hopSize <= fftSize &&
                   fftSize <= 8192; // Reasonable upper limit
        }
        
        int getNumBins() const noexcept { return fftSize / 2 + 1; }
        int getLatencyInSamples() const noexcept { return fftSize - hopSize; }
    };

    /**
     * Constructor with configurable STFT parameters.
     * @param config STFT configuration (FFT size, hop size)
     */
    explicit STFTProcessor(const Config& config = Config::highQuality());
    
    /**
     * Destructor - no cleanup needed due to RAII
     */
    ~STFTProcessor();

    /**
     * Prepare the processor for audio processing.
     * Preallocates all buffers to ensure real-time safety.
     * 
     * @param sampleRate Sample rate (used for latency calculations)
     * @param maxBlockSize Maximum expected block size for buffer allocation
     */
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    /**
     * Push input samples and process available frames.
     * Accumulates input samples and triggers FFT processing when enough samples
     * are available. This method is real-time safe.
     * 
     * @param inputSamples Pointer to input samples
     * @param numSamples Number of input samples to process
     */
    void pushAndProcess(const float* inputSamples, int numSamples) noexcept;

    /**
     * Get the current frequency domain frame for processing.
     * Returns a span over the complex frequency domain data.
     * Only valid after a frame has been processed.
     * 
     * @return Span of complex frequency domain data (size: numBins)
     */
    juce::Span<std::complex<float>> getCurrentFrame() noexcept;

    /**
     * Set the current frequency domain frame after processing.
     * Use this after modifying the frame obtained from getCurrentFrame().
     * This triggers the inverse FFT and overlap-add processing.
     * 
     * @param frame Span of modified frequency domain data
     */
    void setCurrentFrame(juce::Span<const std::complex<float>> frame) noexcept;

    /**
     * Process output samples from the overlap-add buffer.
     * Extracts reconstructed audio samples from the internal output buffer.
     * 
     * @param outputSamples Pointer to output buffer
     * @param numSamples Number of output samples to extract
     */
    void processOutput(float* outputSamples, int numSamples) noexcept;

    /**
     * Reset all internal buffers and state.
     * Clears all buffers to zero and resets processing positions.
     */
    void reset() noexcept;

    /**
     * Check if a new frame is ready for processing.
     * @return true if getCurrentFrame() will return valid data
     */
    bool isFrameReady() const noexcept { return frameReady_.load(std::memory_order_acquire); }

    /**
     * Get the processing latency in samples.
     * @return Latency in samples (fftSize - hopSize)
     */
    int getLatencyInSamples() const noexcept { return config_.getLatencyInSamples(); }

    /**
     * Get the processing latency in milliseconds.
     * @return Latency in milliseconds
     */
    double getLatencyInMs() const noexcept 
    { 
        return (sampleRate_ > 0.0) ? (getLatencyInSamples() * 1000.0 / sampleRate_) : 0.0; 
    }

    /**
     * Get the number of frequency bins.
     * @return Number of frequency bins (fftSize/2 + 1)
     */
    int getNumBins() const noexcept { return config_.getNumBins(); }

    /**
     * Get the FFT size.
     * @return FFT size in samples
     */
    int getFftSize() const noexcept { return config_.fftSize; }

    /**
     * Get the hop size.
     * @return Hop size in samples  
     */
    int getHopSize() const noexcept { return config_.hopSize; }

private:
    // Configuration
    Config config_;
    double sampleRate_ = 48000.0;
    
    // FFT processing objects
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> analysisWindow_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> synthesisWindow_;
    
    // Efficient ring buffer implementation
    class RingBuffer
    {
    public:
        void resize(int size) 
        {
            data_.resize(size * 2, 0.0f); // Double size to avoid modulo operations
            size_ = size;
            writePos_ = 0;
            readPos_ = 0;
        }
        
        void write(const float* input, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
            {
                data_[writePos_] = input[i];
                data_[writePos_ + size_] = input[i]; // Mirror for efficient reading
                writePos_ = (writePos_ + 1) % size_;
            }
        }
        
        void read(float* output, int numSamples, int offset = 0) const noexcept
        {
            const int startPos = (readPos_ + offset) % size_;
            std::copy(data_.begin() + startPos, 
                     data_.begin() + startPos + numSamples, 
                     output);
        }
        
        void readAndClear(float* output, int numSamples) noexcept
        {
            // Read data and then clear it - used for output buffer
            for (int i = 0; i < numSamples; ++i)
            {
                const int pos = (readPos_ + i) % size_;
                output[i] = data_[pos];
                data_[pos] = 0.0f;
                data_[pos + size_] = 0.0f; // Clear mirror too
            }
        }
        
        void advance(int numSamples) noexcept
        {
            readPos_ = (readPos_ + numSamples) % size_;
        }
        
        void overlapAdd(const float* input, int numSamples) noexcept
        {
            // Overlap-add at current write position
            // This is critical for STFT reconstruction
            for (int i = 0; i < numSamples; ++i)
            {
                const int pos = (writePos_ + i) % size_;
                data_[pos] += input[i];
                data_[pos + size_] = data_[pos]; // Keep mirror in sync
            }
        }
        
        void advanceWritePosition(int numSamples) noexcept
        {
            writePos_ = (writePos_ + numSamples) % size_;
        }
        
        void clear() noexcept
        {
            std::fill(data_.begin(), data_.end(), 0.0f);
            writePos_ = 0;
            readPos_ = 0;
        }
        
        int getSize() const noexcept { return size_; }

        // Get the number of readable samples (distance from read to write position)
        int getReadableDistance() const noexcept
        {
            if (writePos_ >= readPos_)
                return writePos_ - readPos_;
            else
                return size_ - readPos_ + writePos_;
        }

    private:
        std::vector<float> data_;
        int size_ = 0;
        int writePos_ = 0;
        int readPos_ = 0;
    };
    
    // Ring buffers for input and output
    RingBuffer inputBuffer_;
    RingBuffer outputBuffer_;
    
    // Processing buffers (aligned for SIMD)
    alignas(32) std::vector<float> fftInputBuffer_;      // Time domain input (windowed)
    alignas(32) std::vector<float> fftOutputBuffer_;     // Time domain output (IFFT result)
    alignas(32) std::vector<float> complexBuffer_;       // Complex FFT data (interleaved real/imag)
    alignas(32) std::vector<std::complex<float>> currentFrame_; // Current frequency domain frame
    
    // Processing state
    int samplesInInputBuffer_ = 0;
    int samplesInOutputBuffer_ = 0;
    std::atomic<bool> frameReady_{false};
    bool isInitialized_ = false;
    bool isFirstFrame_ = true;  // Tracks if we need fftSize samples for first frame
    
    // Window scaling factors for perfect reconstruction (COLA)
    float analysisScale_ = 1.0f;
    float synthesisScale_ = 1.0f;
    
    // Constants for numerical stability
    static constexpr float kEpsilon = 1e-8f;
    
    /**
     * Calculate optimal window scaling factors for COLA reconstruction.
     * Uses the overlap factor to determine proper scaling.
     */
    void calculateWindowScaling() noexcept;
    
    /**
     * Process forward FFT: windowing → FFT → complex to frame
     * This method is called when enough input samples are available.
     */
    void processForwardTransform() noexcept;
    
    /**
     * Process inverse FFT: frame to complex → IFFT → overlap-add
     * This method is called after frequency domain processing is complete.
     */
    void processInverseTransform() noexcept;
    
    /**
     * Apply analysis window with proper scaling.
     * @param data Pointer to time domain data
     * @param size Number of samples
     */
    void applyAnalysisWindow(float* data, int size) noexcept;
    
    /**
     * Apply synthesis window with proper scaling.
     * @param data Pointer to time domain data
     * @param size Number of samples
     */
    void applySynthesisWindow(float* data, int size) noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(STFTProcessor)
};