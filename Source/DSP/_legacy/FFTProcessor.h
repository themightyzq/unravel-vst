#pragma once

#include <JuceHeader.h>
#include <complex>

class FFTProcessor
{
public:
    explicit FFTProcessor(int order);
    ~FFTProcessor();
    
    void performFFT(std::complex<float>* data);
    void performIFFT(std::complex<float>* data);
    
    int getSize() const { return size; }
    int getOrder() const { return fftOrder; }
    
private:
    const int fftOrder;
    const int size;
    
    juce::dsp::FFT fft;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFTProcessor)
};