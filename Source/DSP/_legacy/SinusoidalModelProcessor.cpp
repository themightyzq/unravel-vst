#include "SinusoidalModelProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>

SinusoidalModelProcessor::SinusoidalModelProcessor()
    : randomGenerator(randomDevice())
    , uniformDist(0.0f, 1.0f)
{
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    analysisWindow = std::make_unique<juce::dsp::WindowingFunction<float>>(
        fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
    synthesisWindow = std::make_unique<juce::dsp::WindowingFunction<float>>(
        fftSize, juce::dsp::WindowingFunction<float>::hann);
}

void SinusoidalModelProcessor::prepare(double sampleRate_, int maxBlockSize) noexcept
{
    sampleRate = sampleRate_;
    currentBlockSize = maxBlockSize;
    
    // Allocate ring buffers (4x fftSize for safety)
    const int ringBufferSize = fftSize * 4;
    inputRingBuffer.resize(ringBufferSize, 0.0f);
    outputRingBuffer.resize(ringBufferSize, 0.0f);
    tonalRingBuffer.resize(ringBufferSize, 0.0f);
    noiseRingBuffer.resize(ringBufferSize, 0.0f);
    
    // Allocate processing buffers
    fftBuffer.resize(fftSize * 2, 0.0f);  // Complex FFT buffer
    analysisFrame.resize(fftSize, 0.0f);
    synthesisFrame.resize(fftSize, 0.0f);
    residualFrame.resize(fftSize, 0.0f);
    
    spectralEnvelope.resize(fftSize / 2 + 1, 0.0f);
    noisePhaseRandomizer.resize(fftSize / 2 + 1, 0.0f);
    
    reset();
}

void SinusoidalModelProcessor::reset() noexcept
{
    std::fill(inputRingBuffer.begin(), inputRingBuffer.end(), 0.0f);
    std::fill(outputRingBuffer.begin(), outputRingBuffer.end(), 0.0f);
    std::fill(tonalRingBuffer.begin(), tonalRingBuffer.end(), 0.0f);
    std::fill(noiseRingBuffer.begin(), noiseRingBuffer.end(), 0.0f);
    
    inputWritePos = 0;
    outputReadPos = 0;
    samplesUntilNextFrame = hopSize;
    
    activeTracks.clear();
    trackHistory.clear();
    nextTrackId = 1;
    frameCounter = 0;
}

void SinusoidalModelProcessor::processBlock(const float* inputBuffer,
                                           float* outputBuffer,
                                           float* tonalBuffer,
                                           float* noiseBuffer,
                                           int numSamples,
                                           float tonalGain,
                                           float noiseGain) noexcept
{
    const int ringBufferSize = static_cast<int>(inputRingBuffer.size());
    
    // Enhanced silence detection with stricter threshold
    float inputEnergy = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        inputEnergy += inputBuffer[i] * inputBuffer[i];
    }
    const bool isSilent = (inputEnergy / numSamples) < 1e-6f; // Stricter threshold
    
    // Reset all tracks immediately when silence is detected
    if (isSilent) {
        for (auto& track : activeTracks) {
            track.prevPhase = 0.0f;
            track.amplitude = 0.0f;
            track.prevAmplitude = 0.0f;
        }
    }
    
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Write input to ring buffer (zero if silent to prevent artifacts)
        inputRingBuffer[inputWritePos] = isSilent ? 0.0f : inputBuffer[sample];
        inputWritePos = (inputWritePos + 1) % ringBufferSize;
        
        // Check if we need to process a new frame
        if (--samplesUntilNextFrame == 0)
        {
            samplesUntilNextFrame = hopSize;
            
            // Get analysis frame from ring buffer
            int readPos = (inputWritePos - fftSize + ringBufferSize) % ringBufferSize;
            for (int i = 0; i < fftSize; ++i)
            {
                analysisFrame[i] = inputRingBuffer[(readPos + i) % ringBufferSize];
            }
            
            // ANALYSIS: Extract sinusoidal tracks
            auto peaks = analyzeFrame();
            updateTracks(peaks);
            
            // SYNTHESIS: Generate tonal component
            std::fill(synthesisFrame.begin(), synthesisFrame.end(), 0.0f);
            synthesizeSinusoids(synthesisFrame.data(), fftSize);
            
            // Compute residual (what's left after removing sinusoids)
            for (int i = 0; i < fftSize; ++i)
            {
                residualFrame[i] = analysisFrame[i] - synthesisFrame[i];
            }
            
            // Model residual as filtered noise
            std::vector<float> noiseFrame(fftSize, 0.0f);
            modelResidualNoise(residualFrame.data(), noiseFrame.data(), fftSize);
            
            // Apply synthesis window and write to output ring buffers
            synthesisWindow->multiplyWithWindowingTable(synthesisFrame.data(), fftSize);
            synthesisWindow->multiplyWithWindowingTable(noiseFrame.data(), fftSize);
            
            // Overlap-add to output ring buffers with proper scaling
            int writePos = (outputReadPos - hopSize + ringBufferSize) % ringBufferSize;
            const float overlapScale = 0.5f / (float)(fftSize / hopSize); // Proper COLA scaling
            for (int i = 0; i < fftSize; ++i)
            {
                int pos = (writePos + i) % ringBufferSize;
                tonalRingBuffer[pos] += synthesisFrame[i] * overlapScale;
                noiseRingBuffer[pos] += noiseFrame[i] * overlapScale;
            }
            
            frameCounter++;
        }
        
        // Read from output ring buffers
        float tonal = tonalRingBuffer[outputReadPos] * tonalGain;
        float noise = noiseRingBuffer[outputReadPos] * noiseGain;
        
        // Clear the position we just read
        tonalRingBuffer[outputReadPos] = 0.0f;
        noiseRingBuffer[outputReadPos] = 0.0f;
        
        // Apply soft clipping to prevent overload
        float output = tonal + noise;
        output = std::tanh(output * 0.9f); // Soft clip with reasonable headroom
        
        // Write outputs with clipping protection
        outputBuffer[sample] = std::max(-1.0f, std::min(1.0f, output));
        if (tonalBuffer) tonalBuffer[sample] = std::max(-1.0f, std::min(1.0f, tonal));
        if (noiseBuffer) noiseBuffer[sample] = std::max(-1.0f, std::min(1.0f, noise));
        
        outputReadPos = (outputReadPos + 1) % ringBufferSize;
    }
}

std::vector<SinusoidalModelProcessor::SpectralPeak> 
SinusoidalModelProcessor::analyzeFrame() noexcept
{
    // Apply analysis window
    std::copy(analysisFrame.begin(), analysisFrame.end(), fftBuffer.begin());
    analysisWindow->multiplyWithWindowingTable(fftBuffer.data(), fftSize);
    
    // Perform FFT
    fft->performRealOnlyForwardTransform(fftBuffer.data());
    
    // Extract magnitude and phase
    const int numBins = fftSize / 2 + 1;
    std::vector<float> magnitudes(numBins);
    std::vector<float> phases(numBins);
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        const float real = fftBuffer[bin * 2];
        const float imag = fftBuffer[bin * 2 + 1];
        magnitudes[bin] = std::sqrt(real * real + imag * imag);
        phases[bin] = std::atan2(imag, real);
    }
    
    // Find peaks
    return findSpectralPeaks(magnitudes.data(), phases.data(), numBins);
}

std::vector<SinusoidalModelProcessor::SpectralPeak>
SinusoidalModelProcessor::findSpectralPeaks(const float* magnitudes,
                                           const float* phases,
                                           int numBins) noexcept
{
    std::vector<SpectralPeak> peaks;
    
    // Find local maxima with improved criteria
    for (int bin = 2; bin < numBins - 2; ++bin)
    {
        // Require peak to be significantly higher than neighbors (better tonal detection)
        const float leftNeighbor = std::max(magnitudes[bin - 2], magnitudes[bin - 1]);
        const float rightNeighbor = std::max(magnitudes[bin + 1], magnitudes[bin + 2]);
        const float peakThreshold = std::max(leftNeighbor, rightNeighbor) * 1.2f; // Lower threshold for better tonal detection
        
        if (magnitudes[bin] > peakThreshold &&
            magnitudes[bin] > magnitudes[bin - 1] &&
            magnitudes[bin] > magnitudes[bin + 1] &&
            magnitudes[bin] > minPeakMagnitude)
        {
            SpectralPeak peak;
            peak.bin = bin;
            peak.phase = phases[bin];
            
            // Parabolic interpolation for precise frequency
            parabolicInterpolation(magnitudes[bin - 1], magnitudes[bin], magnitudes[bin + 1],
                                 bin, peak.frequency, peak.amplitude);
            
            // Only add if frequency is reasonable (avoid DC and Nyquist artifacts)
            if (peak.frequency > 20.0f && peak.frequency < sampleRate * 0.45f)
            {
                peaks.push_back(peak);
            }
        }
    }
    
    // Sort by amplitude (strongest first) and keep only top tracks
    std::sort(peaks.begin(), peaks.end(),
              [](const SpectralPeak& a, const SpectralPeak& b) {
                  return a.amplitude > b.amplitude;
              });
    
    if (peaks.size() > maxTracks)
    {
        peaks.resize(maxTracks);
    }
    
    return peaks;
}

void SinusoidalModelProcessor::parabolicInterpolation(float left, float center, float right,
                                                     int peakBin,
                                                     float& interpolatedFreq,
                                                     float& interpolatedMag) noexcept
{
    // Parabolic interpolation in dB domain
    const float eps = 1e-10f;
    const float leftDB = 20.0f * std::log10(left + eps);
    const float centerDB = 20.0f * std::log10(center + eps);
    const float rightDB = 20.0f * std::log10(right + eps);
    
    // Calculate fractional bin offset
    const float delta = 0.5f * (rightDB - leftDB) / 
                       (2.0f * centerDB - leftDB - rightDB + eps);
    
    // Clamp to reasonable range
    const float clampedDelta = std::max(-0.5f, std::min(0.5f, delta));
    
    // Interpolated frequency
    const float binFreq = sampleRate / fftSize;
    interpolatedFreq = (static_cast<float>(peakBin) + clampedDelta) * binFreq;
    
    // Interpolated magnitude
    interpolatedMag = center - 0.25f * (left - right) * clampedDelta;
}

void SinusoidalModelProcessor::updateTracks(const std::vector<SpectralPeak>& currentPeaks) noexcept
{
    // Mark all tracks as potentially dead
    for (auto& track : activeTracks)
    {
        track.isActive = false;
    }
    
    std::vector<bool> peakUsed(currentPeaks.size(), false);
    
    // Try to continue existing tracks
    for (auto& track : activeTracks)
    {
        float minFreqDiff = maxFreqDeviation;
        int bestPeakIdx = -1;
        
        // Find closest peak in frequency
        for (size_t i = 0; i < currentPeaks.size(); ++i)
        {
            if (!peakUsed[i])
            {
                float freqDiff = std::abs(currentPeaks[i].frequency - track.frequency);
                if (freqDiff < minFreqDiff)
                {
                    minFreqDiff = freqDiff;
                    bestPeakIdx = static_cast<int>(i);
                }
            }
        }
        
        if (bestPeakIdx >= 0)
        {
            // Continue track
            const auto& peak = currentPeaks[bestPeakIdx];
            track.prevFrequency = track.frequency;
            track.prevAmplitude = track.amplitude;
            track.prevPhase = track.phase;
            track.frequency = peak.frequency;
            track.amplitude = peak.amplitude;
            track.phase = peak.phase;
            track.age++;
            track.isActive = true;
            peakUsed[bestPeakIdx] = true;
        }
    }
    
    // Create new tracks for unmatched peaks
    for (size_t i = 0; i < currentPeaks.size(); ++i)
    {
        if (!peakUsed[i] && activeTracks.size() < maxTracks)
        {
            SinusoidalTrack newTrack;
            newTrack.id = nextTrackId++;
            newTrack.frequency = currentPeaks[i].frequency;
            newTrack.amplitude = currentPeaks[i].amplitude;
            newTrack.phase = currentPeaks[i].phase;
            newTrack.prevFrequency = newTrack.frequency;
            newTrack.prevAmplitude = 0.0f;  // Fade in
            newTrack.prevPhase = newTrack.phase;
            newTrack.birthFrame = frameCounter;
            newTrack.age = 1;
            newTrack.isActive = true;
            
            activeTracks.push_back(newTrack);
        }
    }
    
    // Remove dead tracks
    activeTracks.erase(
        std::remove_if(activeTracks.begin(), activeTracks.end(),
                      [](const SinusoidalTrack& track) {
                          return !track.isActive && track.age > 3;
                      }),
        activeTracks.end());
}

void SinusoidalModelProcessor::synthesizeSinusoids(float* outputFrame, int frameSize) noexcept
{
    // Clear output
    std::fill(outputFrame, outputFrame + frameSize, 0.0f);
    
    // Synthesize each active track with stricter amplitude check
    for (auto& track : activeTracks)
    {
        if (track.amplitude < 0.001f || track.prevAmplitude < 0.001f)
            continue;
        
        // Use cubic phase interpolation for smooth transitions
        const float dt = 1.0f / static_cast<float>(sampleRate);
        float currentPhase = track.prevPhase;
        
        // Phase derivative (frequency in radians per sample)
        const float omega0 = 2.0f * M_PI * track.prevFrequency / static_cast<float>(sampleRate);
        const float omega1 = 2.0f * M_PI * track.frequency / static_cast<float>(sampleRate);
        
        // Generate sinusoid for the frame
        for (int i = 0; i < frameSize; ++i)
        {
            // Linear interpolation factor
            const float t = static_cast<float>(i) / static_cast<float>(frameSize);
            
            // Interpolated amplitude
            const float currentAmp = track.prevAmplitude + t * (track.amplitude - track.prevAmplitude);
            
            // Interpolated frequency (in radians per sample)
            const float currentOmega = omega0 + t * (omega1 - omega0);
            
            // Generate sample with moderate amplitude scaling
            const float sample = currentAmp * std::cos(currentPhase) * 0.5f; // Moderate scaling
            outputFrame[i] += sample;
            
            // Update phase with proper frequency interpolation
            currentPhase += currentOmega;
        }
        
        // Update track phase for next frame (maintain phase continuity)
        track.prevPhase = currentPhase;
        while (track.prevPhase > M_PI) track.prevPhase -= 2.0f * static_cast<float>(M_PI);
        while (track.prevPhase < -M_PI) track.prevPhase += 2.0f * static_cast<float>(M_PI);
        
        // More aggressive phase reset to prevent runaway oscillation
        if (track.amplitude < 0.001f || track.prevAmplitude < 0.001f)
        {
            track.prevPhase = 0.0f;
        }
    }
}

void SinusoidalModelProcessor::modelResidualNoise(const float* residualFrame,
                                                 float* noiseFrame,
                                                 int frameSize) noexcept
{
    // Apply window to residual
    std::copy(residualFrame, residualFrame + frameSize, fftBuffer.begin());
    analysisWindow->multiplyWithWindowingTable(fftBuffer.data(), frameSize);
    
    // FFT of residual
    fft->performRealOnlyForwardTransform(fftBuffer.data());
    
    // Extract magnitude spectrum
    const int numBins = frameSize / 2 + 1;
    std::vector<float> magnitudes(numBins);
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        const float real = fftBuffer[bin * 2];
        const float imag = fftBuffer[bin * 2 + 1];
        magnitudes[bin] = std::sqrt(real * real + imag * imag);
    }
    
    // Extract spectral envelope
    extractSpectralEnvelope(magnitudes.data(), numBins, spectralEnvelope.data());
    
    // Generate filtered noise with proper randomization
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Random magnitude with envelope shape (proper Rayleigh distribution for noise)
        const float u1 = uniformDist(randomGenerator);
        const float u2 = uniformDist(randomGenerator);
        
        // Box-Muller transform for Gaussian noise
        const float gaussianNoise = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * M_PI * u2);
        const float noiseMag = spectralEnvelope[bin] * std::abs(gaussianNoise) * 0.5f;
        
        // Random phase uniformly distributed
        const float noisePhase = 2.0f * M_PI * uniformDist(randomGenerator) - M_PI;
        
        // Convert to complex with proper scaling
        fftBuffer[bin * 2] = noiseMag * std::cos(noisePhase);
        fftBuffer[bin * 2 + 1] = noiseMag * std::sin(noisePhase);
    }
    
    // Inverse FFT
    fft->performRealOnlyInverseTransform(fftBuffer.data());
    
    // Copy to output with scaling
    const float scale = 1.0f / frameSize;
    for (int i = 0; i < frameSize; ++i)
    {
        noiseFrame[i] = fftBuffer[i] * scale;
    }
}

void SinusoidalModelProcessor::extractSpectralEnvelope(const float* magnitudes,
                                                      int numBins,
                                                      float* envelope) noexcept
{
    // Convert to dB domain for better envelope extraction
    std::vector<float> dBMagnitudes(numBins);
    const float minDb = -80.0f;
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        const float linearMag = std::max(magnitudes[bin], 1e-8f);
        dBMagnitudes[bin] = 20.0f * std::log10(linearMag);
    }
    
    // Spectral smoothing in dB domain (logarithmic frequency spacing aware)
    const int baseSmoothing = 3;
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Adaptive smoothing width based on frequency
        const float freq = bin * sampleRate / (2.0f * numBins);
        const int smoothingWidth = std::max(baseSmoothing, static_cast<int>(baseSmoothing * std::log(freq + 100.0f) / std::log(1000.0f)));
        
        float sum = 0.0f;
        int count = 0;
        
        for (int offset = -smoothingWidth; offset <= smoothingWidth; ++offset)
        {
            int idx = bin + offset;
            if (idx >= 0 && idx < numBins)
            {
                sum += dBMagnitudes[idx];
                count++;
            }
        }
        
        const float smoothedDb = count > 0 ? sum / count : dBMagnitudes[bin];
        
        // Convert back to linear domain with floor
        envelope[bin] = std::pow(10.0f, std::max(smoothedDb, minDb) / 20.0f);
    }
    
    // Additional median-like smoothing for better envelope
    std::vector<float> temp(numBins);
    std::copy(envelope, envelope + numBins, temp.begin());
    
    for (int bin = 2; bin < numBins - 2; ++bin)
    {
        // 5-point smoothing filter
        envelope[bin] = 0.1f * temp[bin - 2] + 
                       0.2f * temp[bin - 1] + 
                       0.4f * temp[bin] + 
                       0.2f * temp[bin + 1] + 
                       0.1f * temp[bin + 2];
    }
}