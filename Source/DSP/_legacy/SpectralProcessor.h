#pragma once

#include <JuceHeader.h>
#include "FFTProcessor.h"
#include "TonalNoiseDecomposer.h"

class SpectralProcessor
{
public:
    SpectralProcessor();
    ~SpectralProcessor();
    
    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    
    void process(const float* input, 
                 float* tonalOutput,
                 float* noisyOutput, 
                 float* transientOutput,
                 int numSamples);
    
    void setBalance(float newBalance) { decomposer.setBalance(newBalance); }
    void setSmoothing(float newSmoothing) { decomposer.setSmoothing(newSmoothing); }
    void setSeparateTransients(bool separate) { separateTransients = separate; }
    
private:
    static constexpr int fftOrder = 11; // 2048 samples
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4; // 75% overlap
    
    FFTProcessor fftProcessor;
    TonalNoiseDecomposer decomposer;
    
    // Ring buffers for input/output
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> tonalOutputBuffer;
    juce::AudioBuffer<float> noisyOutputBuffer;
    juce::AudioBuffer<float> transientOutputBuffer;
    
    int inputWritePos = 0;
    int outputReadPos = 0;
    int samplesAvailable = 0;
    
    // Processing buffers
    std::vector<float> windowedFrame;
    std::vector<std::complex<float>> fftData;
    std::vector<std::complex<float>> tonalSpectrum;
    std::vector<std::complex<float>> noisySpectrum;
    
    // Transient detection
    std::vector<float> spectralFlux;
    std::vector<float> transientEnvelope;
    float previousFlux = 0.0f;
    bool separateTransients = false;
    
    // Window function
    std::vector<float> window;
    
    void processFrame();
    void detectTransients(const std::complex<float>* spectrum, int binCount);
    void applyWindow(const float* input, float* output);
    void applyInverseWindow(float* data);
    
    double currentSampleRate = 44100.0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralProcessor)
};