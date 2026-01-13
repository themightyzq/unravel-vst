#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

class TonalNoiseDecomposer
{
public:
    explicit TonalNoiseDecomposer(int fftSize);
    ~TonalNoiseDecomposer();
    
    void prepare(double sampleRate);
    void reset();
    
    void decompose(const std::complex<float>* input,
                   std::complex<float>* tonalOutput,
                   std::complex<float>* noisyOutput,
                   int numBins);
    
    void setBalance(float newBalance);
    void setSmoothing(float newSmoothing);
    
private:
    struct SpectralPeak
    {
        int bin;
        float frequency;
        float magnitude;
        float phase;
        bool isTonal;
    };
    
    struct Partial
    {
        std::vector<SpectralPeak> peaks;
        float averageFrequency;
        float averageMagnitude;
        int birthFrame;
        int age;
        bool isActive;
    };
    
    const int fftSize;
    double sampleRate = 44100.0;
    
    // Parameters
    float balance = 50.0f;      // 0-100, controls tonal detection threshold
    float smoothing = 30.0f;    // 0-100, controls artifact smoothing
    
    // Processing state
    std::vector<std::vector<float>> magnitudeHistory;
    std::vector<std::vector<float>> phaseHistory;
    std::vector<float> tonalMask;
    std::vector<float> noiseMask;
    std::vector<float> smoothedTonalMask;
    std::vector<float> smoothedNoiseMask;
    
    // Partial tracking
    std::vector<Partial> activePartials;
    std::vector<SpectralPeak> currentPeaks;
    
    int frameCount = 0;
    static constexpr int historySize = 5;
    
    // Peak detection parameters
    float peakThreshold = 0.01f;
    float minPeakProminence = 6.0f; // dB
    
    // Partial tracking parameters
    float frequencyTolerance = 50.0f; // Hz
    float magnitudeTolerance = 6.0f;  // dB
    int minPartialAge = 3;            // frames
    
    // Methods
    void detectPeaks(const std::complex<float>* spectrum, int numBins);
    void trackPartials();
    void classifyBins(int numBins);
    void smoothMasks(int numBins);
    void applyMasks(const std::complex<float>* input,
                   std::complex<float>* tonalOutput,
                   std::complex<float>* noisyOutput,
                   int numBins);
    
    float binToFrequency(int bin) const;
    int frequencyToBin(float freq) const;
    float calculateProminence(const float* magnitudes, int peakBin, int numBins) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonalNoiseDecomposer)
};