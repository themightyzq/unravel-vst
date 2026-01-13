#include "SpectralProcessor.h"

SpectralProcessor::SpectralProcessor()
    : fftProcessor(fftOrder),
      decomposer(fftSize)
{
    // Initialize buffers
    inputBuffer.setSize(1, fftSize * 4);
    tonalOutputBuffer.setSize(1, fftSize * 4);
    noisyOutputBuffer.setSize(1, fftSize * 4);
    transientOutputBuffer.setSize(1, fftSize * 4);
    
    // Initialize processing buffers
    windowedFrame.resize(fftSize);
    fftData.resize(fftSize);
    tonalSpectrum.resize(fftSize);
    noisySpectrum.resize(fftSize);
    spectralFlux.resize(fftSize / 2);
    transientEnvelope.resize(fftSize);
    
    // Create Hann window
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
    }
}

SpectralProcessor::~SpectralProcessor() = default;

void SpectralProcessor::prepare(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;
    
    juce::ignoreUnused(maximumBlockSize);
    
    reset();
    decomposer.prepare(sampleRate);
}

void SpectralProcessor::reset()
{
    inputBuffer.clear();
    tonalOutputBuffer.clear();
    noisyOutputBuffer.clear();
    transientOutputBuffer.clear();
    
    inputWritePos = 0;
    outputReadPos = 0;
    samplesAvailable = 0;
    
    std::fill(spectralFlux.begin(), spectralFlux.end(), 0.0f);
    previousFlux = 0.0f;
    
    decomposer.reset();
}

void SpectralProcessor::process(const float* input, 
                                float* tonalOutput,
                                float* noisyOutput, 
                                float* transientOutput,
                                int numSamples)
{
    auto* inputRingBuffer = inputBuffer.getWritePointer(0);
    auto* tonalRingBuffer = tonalOutputBuffer.getReadPointer(0);
    auto* noisyRingBuffer = noisyOutputBuffer.getReadPointer(0);
    auto* transientRingBuffer = transientOutputBuffer.getReadPointer(0);
    
    const int bufferSize = inputBuffer.getNumSamples();
    
    // Write input samples to ring buffer
    for (int i = 0; i < numSamples; ++i)
    {
        inputRingBuffer[inputWritePos] = input[i];
        inputWritePos = (inputWritePos + 1) % bufferSize;
        samplesAvailable++;
        
        // Process frame when we have enough samples
        if (samplesAvailable >= fftSize && (samplesAvailable % hopSize) == 0)
        {
            processFrame();
        }
    }
    
    // Read output samples from ring buffers
    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesAvailable > fftSize * 2) // Ensure we have processed data
        {
            tonalOutput[i] = tonalRingBuffer[outputReadPos];
            noisyOutput[i] = noisyRingBuffer[outputReadPos];
            transientOutput[i] = separateTransients ? transientRingBuffer[outputReadPos] : 0.0f;
            
            outputReadPos = (outputReadPos + 1) % bufferSize;
        }
        else
        {
            // Output silence until we have enough processed data
            tonalOutput[i] = 0.0f;
            noisyOutput[i] = 0.0f;
            transientOutput[i] = 0.0f;
        }
    }
}

void SpectralProcessor::processFrame()
{
    const int bufferSize = inputBuffer.getNumSamples();
    auto* inputRingBuffer = inputBuffer.getReadPointer(0);
    auto* tonalRingBuffer = tonalOutputBuffer.getWritePointer(0);
    auto* noisyRingBuffer = noisyOutputBuffer.getWritePointer(0);
    auto* transientRingBuffer = transientOutputBuffer.getWritePointer(0);
    
    // Get frame from ring buffer
    int readPos = (inputWritePos - fftSize + bufferSize) % bufferSize;
    
    // Apply window and copy to FFT buffer
    for (int i = 0; i < fftSize; ++i)
    {
        windowedFrame[i] = inputRingBuffer[readPos] * window[i];
        fftData[i] = std::complex<float>(windowedFrame[i], 0.0f);
        readPos = (readPos + 1) % bufferSize;
    }
    
    // Forward FFT
    fftProcessor.performFFT(fftData.data());
    
    // Detect transients if needed
    if (separateTransients)
    {
        detectTransients(fftData.data(), fftSize / 2);
    }
    
    // Perform tonal/noise decomposition
    decomposer.decompose(fftData.data(), 
                         tonalSpectrum.data(), 
                         noisySpectrum.data(),
                         fftSize / 2);
    
    // If separating transients, apply transient mask
    if (separateTransients)
    {
        for (int i = 0; i < fftSize / 2; ++i)
        {
            float transientMask = transientEnvelope[i];
            float steadyMask = 1.0f - transientMask;
            
            // Apply masks
            tonalSpectrum[i] *= steadyMask;
            noisySpectrum[i] *= steadyMask;
            
            // Mirror for negative frequencies
            if (i > 0 && i < fftSize / 2)
            {
                tonalSpectrum[fftSize - i] = std::conj(tonalSpectrum[i]);
                noisySpectrum[fftSize - i] = std::conj(noisySpectrum[i]);
            }
        }
    }
    
    // Inverse FFT for tonal component
    std::vector<std::complex<float>> tonalTime(tonalSpectrum);
    fftProcessor.performIFFT(tonalTime.data());
    
    // Inverse FFT for noisy component
    std::vector<std::complex<float>> noisyTime(noisySpectrum);
    fftProcessor.performIFFT(noisyTime.data());
    
    // Inverse FFT for transient component (if separated)
    std::vector<float> transientFrame(fftSize, 0.0f);
    if (separateTransients)
    {
        std::vector<std::complex<float>> transientSpectrum(fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            float mask = (i < fftSize / 2) ? transientEnvelope[i] : 
                        transientEnvelope[fftSize - i];
            transientSpectrum[i] = fftData[i] * mask;
        }
        fftProcessor.performIFFT(transientSpectrum.data());
        
        for (int i = 0; i < fftSize; ++i)
        {
            transientFrame[i] = transientSpectrum[i].real() * window[i];
        }
    }
    
    // Apply window and overlap-add to output buffers
    int writePos = (outputReadPos + fftSize) % bufferSize;
    for (int i = 0; i < fftSize; ++i)
    {
        // Apply inverse window and write to output buffers
        float windowGain = window[i];
        
        tonalRingBuffer[writePos] += tonalTime[i].real() * windowGain;
        noisyRingBuffer[writePos] += noisyTime[i].real() * windowGain;
        
        if (separateTransients)
        {
            transientRingBuffer[writePos] += transientFrame[i];
        }
        
        writePos = (writePos + 1) % bufferSize;
    }
}

void SpectralProcessor::detectTransients(const std::complex<float>* spectrum, int binCount)
{
    // Calculate spectral flux
    float currentFlux = 0.0f;
    for (int i = 0; i < binCount; ++i)
    {
        float magnitude = std::abs(spectrum[i]);
        float diff = magnitude - spectralFlux[i];
        if (diff > 0)
        {
            currentFlux += diff;
        }
        spectralFlux[i] = magnitude;
    }
    
    // Adaptive threshold based on local average
    float threshold = previousFlux * 1.5f + 0.1f;
    float transientStrength = 0.0f;
    
    if (currentFlux > threshold)
    {
        transientStrength = std::min(1.0f, (currentFlux - threshold) / threshold);
    }
    
    // Update transient envelope with smoothing
    const float attack = 0.9f;
    const float release = 0.95f;
    
    for (int i = 0; i < binCount; ++i)
    {
        float target = transientStrength;
        if (target > transientEnvelope[i])
        {
            transientEnvelope[i] = target * attack + transientEnvelope[i] * (1.0f - attack);
        }
        else
        {
            transientEnvelope[i] *= release;
        }
    }
    
    previousFlux = currentFlux * 0.8f + previousFlux * 0.2f;
}

void SpectralProcessor::applyWindow(const float* input, float* output)
{
    for (int i = 0; i < fftSize; ++i)
    {
        output[i] = input[i] * window[i];
    }
}

void SpectralProcessor::applyInverseWindow(float* data)
{
    // Apply inverse window for perfect reconstruction
    // Note: This assumes proper overlap-add with 75% overlap
    for (int i = 0; i < fftSize; ++i)
    {
        if (window[i] > 0.01f) // Avoid division by very small numbers
        {
            data[i] /= (window[i] * 2.0f); // Factor of 2 for overlap compensation
        }
    }
}