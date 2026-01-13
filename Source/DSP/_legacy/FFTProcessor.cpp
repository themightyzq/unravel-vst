#include "FFTProcessor.h"

FFTProcessor::FFTProcessor(int order)
    : fftOrder(order),
      size(1 << order),
      fft(order)
{
}

FFTProcessor::~FFTProcessor() = default;

void FFTProcessor::performFFT(std::complex<float>* data)
{
    // Convert complex data to interleaved real/imag format for JUCE
    std::vector<float> interleavedData(size * 2);
    
    for (int i = 0; i < size; ++i)
    {
        interleavedData[i * 2] = data[i].real();
        interleavedData[i * 2 + 1] = data[i].imag();
    }
    
    // Perform forward FFT
    fft.performFrequencyOnlyForwardTransform(interleavedData.data());
    
    // Convert back to complex format
    for (int i = 0; i < size; ++i)
    {
        data[i] = std::complex<float>(interleavedData[i * 2], interleavedData[i * 2 + 1]);
    }
}

void FFTProcessor::performIFFT(std::complex<float>* data)
{
    // Convert complex data to interleaved real/imag format for JUCE
    std::vector<float> interleavedData(size * 2);
    
    for (int i = 0; i < size; ++i)
    {
        interleavedData[i * 2] = data[i].real();
        interleavedData[i * 2 + 1] = data[i].imag();
    }
    
    // Perform inverse FFT
    fft.performRealOnlyInverseTransform(interleavedData.data());
    
    // Convert back to complex format and normalize
    const float normalizationFactor = 1.0f / static_cast<float>(size);
    for (int i = 0; i < size; ++i)
    {
        data[i] = std::complex<float>(interleavedData[i * 2] * normalizationFactor, 
                                     interleavedData[i * 2 + 1] * normalizationFactor);
    }
}