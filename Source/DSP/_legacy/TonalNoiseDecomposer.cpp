#include "TonalNoiseDecomposer.h"

TonalNoiseDecomposer::TonalNoiseDecomposer(int size)
    : fftSize(size)
{
    const int numBins = fftSize / 2;
    
    // Initialize history buffers
    magnitudeHistory.resize(historySize);
    phaseHistory.resize(historySize);
    for (int i = 0; i < historySize; ++i)
    {
        magnitudeHistory[i].resize(numBins, 0.0f);
        phaseHistory[i].resize(numBins, 0.0f);
    }
    
    // Initialize masks
    tonalMask.resize(numBins, 0.0f);
    noiseMask.resize(numBins, 1.0f);
    smoothedTonalMask.resize(numBins, 0.0f);
    smoothedNoiseMask.resize(numBins, 1.0f);
    
    // Reserve space for peaks and partials
    currentPeaks.reserve(100);
    activePartials.reserve(50);
}

TonalNoiseDecomposer::~TonalNoiseDecomposer() = default;

void TonalNoiseDecomposer::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void TonalNoiseDecomposer::reset()
{
    frameCount = 0;
    
    // Clear history
    for (auto& hist : magnitudeHistory)
    {
        std::fill(hist.begin(), hist.end(), 0.0f);
    }
    for (auto& hist : phaseHistory)
    {
        std::fill(hist.begin(), hist.end(), 0.0f);
    }
    
    // Reset masks
    std::fill(tonalMask.begin(), tonalMask.end(), 0.0f);
    std::fill(noiseMask.begin(), noiseMask.end(), 1.0f);
    std::fill(smoothedTonalMask.begin(), smoothedTonalMask.end(), 0.0f);
    std::fill(smoothedNoiseMask.begin(), smoothedNoiseMask.end(), 1.0f);
    
    // Clear tracking
    currentPeaks.clear();
    activePartials.clear();
}

void TonalNoiseDecomposer::setBalance(float newBalance)
{
    balance = juce::jlimit(0.0f, 100.0f, newBalance);
    
    // Adjust detection thresholds based on balance
    peakThreshold = 0.001f + (100.0f - balance) * 0.0001f;  // More conservative with lower balance
    minPeakProminence = 3.0f + (100.0f - balance) * 0.09f;  // 3-12 dB range
    minPartialAge = static_cast<int>(2 + (100.0f - balance) * 0.03f); // 2-5 frames
}

void TonalNoiseDecomposer::setSmoothing(float newSmoothing)
{
    smoothing = juce::jlimit(0.0f, 100.0f, newSmoothing);
}

void TonalNoiseDecomposer::decompose(const std::complex<float>* input,
                                     std::complex<float>* tonalOutput,
                                     std::complex<float>* noisyOutput,
                                     int numBins)
{
    // Update history (circular buffer)
    int histIndex = frameCount % historySize;
    
    for (int i = 0; i < numBins; ++i)
    {
        magnitudeHistory[histIndex][i] = std::abs(input[i]);
        phaseHistory[histIndex][i] = std::arg(input[i]);
    }
    
    // Perform decomposition steps
    detectPeaks(input, numBins);
    trackPartials();
    classifyBins(numBins);
    smoothMasks(numBins);
    applyMasks(input, tonalOutput, noisyOutput, numBins);
    
    frameCount++;
}

void TonalNoiseDecomposer::detectPeaks(const std::complex<float>* spectrum, int numBins)
{
    currentPeaks.clear();
    
    const float* magnitudes = magnitudeHistory[frameCount % historySize].data();
    const float* phases = phaseHistory[frameCount % historySize].data();
    
    // Find local maxima
    for (int i = 1; i < numBins - 1; ++i)
    {
        float mag = magnitudes[i];
        
        // Check if local maximum
        if (mag > magnitudes[i - 1] && mag > magnitudes[i + 1] && mag > peakThreshold)
        {
            float prominence = calculateProminence(magnitudes, i, numBins);
            
            if (prominence >= minPeakProminence)
            {
                SpectralPeak peak;
                peak.bin = i;
                peak.frequency = binToFrequency(i);
                peak.magnitude = mag;
                peak.phase = phases[i];
                peak.isTonal = false; // Will be determined by tracking
                
                currentPeaks.push_back(peak);
            }
        }
    }
}

void TonalNoiseDecomposer::trackPartials()
{
    // Age existing partials
    for (auto& partial : activePartials)
    {
        partial.age++;
        partial.isActive = false;
    }
    
    // Match peaks to existing partials
    for (auto& peak : currentPeaks)
    {
        float bestDistance = std::numeric_limits<float>::max();
        Partial* bestMatch = nullptr;
        
        for (auto& partial : activePartials)
        {
            if (!partial.peaks.empty())
            {
                const auto& lastPeak = partial.peaks.back();
                float freqDiff = std::abs(peak.frequency - lastPeak.frequency);
                float magDiff = std::abs(20.0f * std::log10(peak.magnitude / (lastPeak.magnitude + 0.0001f)));
                
                if (freqDiff < frequencyTolerance && magDiff < magnitudeTolerance)
                {
                    float distance = freqDiff / frequencyTolerance + magDiff / magnitudeTolerance;
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestMatch = &partial;
                    }
                }
            }
        }
        
        if (bestMatch && bestDistance < 1.0f)
        {
            // Continue existing partial
            bestMatch->peaks.push_back(peak);
            bestMatch->isActive = true;
            
            // Update average frequency and magnitude
            float alpha = 0.3f;
            bestMatch->averageFrequency = alpha * peak.frequency + (1.0f - alpha) * bestMatch->averageFrequency;
            bestMatch->averageMagnitude = alpha * peak.magnitude + (1.0f - alpha) * bestMatch->averageMagnitude;
            
            // Mark as tonal if stable enough
            if (bestMatch->age >= minPartialAge)
            {
                peak.isTonal = true;
            }
        }
        else
        {
            // Create new partial
            Partial newPartial;
            newPartial.peaks.push_back(peak);
            newPartial.averageFrequency = peak.frequency;
            newPartial.averageMagnitude = peak.magnitude;
            newPartial.birthFrame = frameCount;
            newPartial.age = 0;
            newPartial.isActive = true;
            
            activePartials.push_back(newPartial);
        }
    }
    
    // Remove inactive partials
    activePartials.erase(
        std::remove_if(activePartials.begin(), activePartials.end(),
                      [](const Partial& p) { return !p.isActive && p.age > 10; }),
        activePartials.end()
    );
}

void TonalNoiseDecomposer::classifyBins(int numBins)
{
    // Reset masks
    std::fill(tonalMask.begin(), tonalMask.end(), 0.0f);
    std::fill(noiseMask.begin(), noiseMask.end(), 1.0f);
    
    // Mark tonal bins based on stable partials
    for (const auto& partial : activePartials)
    {
        if (partial.age >= minPartialAge && partial.isActive)
        {
            // Get current peak in this partial
            if (!partial.peaks.empty())
            {
                const auto& peak = partial.peaks.back();
                int centerBin = peak.bin;
                
                // Mark surrounding bins as tonal (with gaussian weighting)
                int spread = 3; // bins
                for (int i = std::max(0, centerBin - spread); 
                     i < std::min(numBins, centerBin + spread + 1); ++i)
                {
                    float distance = std::abs(i - centerBin);
                    float weight = std::exp(-0.5f * (distance * distance) / 1.0f);
                    tonalMask[i] = std::max(tonalMask[i], weight);
                }
            }
        }
    }
    
    // Create complementary noise mask
    for (int i = 0; i < numBins; ++i)
    {
        noiseMask[i] = 1.0f - tonalMask[i];
    }
}

void TonalNoiseDecomposer::smoothMasks(int numBins)
{
    float smoothingFactor = smoothing / 100.0f;
    float temporalSmooth = 0.1f + smoothingFactor * 0.4f;  // 0.1 to 0.5
    float spectralSmooth = 1.0f + smoothingFactor * 4.0f;  // 1 to 5 bins
    
    // Temporal smoothing
    for (int i = 0; i < numBins; ++i)
    {
        smoothedTonalMask[i] = tonalMask[i] * temporalSmooth + 
                               smoothedTonalMask[i] * (1.0f - temporalSmooth);
        smoothedNoiseMask[i] = noiseMask[i] * temporalSmooth + 
                               smoothedNoiseMask[i] * (1.0f - temporalSmooth);
    }
    
    // Spectral smoothing (median filter)
    if (smoothingFactor > 0.1f)
    {
        int filterSize = static_cast<int>(spectralSmooth);
        std::vector<float> tempTonal(numBins);
        std::vector<float> tempNoise(numBins);
        
        for (int i = 0; i < numBins; ++i)
        {
            std::vector<float> tonalWindow;
            std::vector<float> noiseWindow;
            
            for (int j = std::max(0, i - filterSize); 
                 j < std::min(numBins, i + filterSize + 1); ++j)
            {
                tonalWindow.push_back(smoothedTonalMask[j]);
                noiseWindow.push_back(smoothedNoiseMask[j]);
            }
            
            // Get median
            std::sort(tonalWindow.begin(), tonalWindow.end());
            std::sort(noiseWindow.begin(), noiseWindow.end());
            
            tempTonal[i] = tonalWindow[tonalWindow.size() / 2];
            tempNoise[i] = noiseWindow[noiseWindow.size() / 2];
        }
        
        smoothedTonalMask = tempTonal;
        smoothedNoiseMask = tempNoise;
    }
    
    // Ensure masks sum to 1
    for (int i = 0; i < numBins; ++i)
    {
        float sum = smoothedTonalMask[i] + smoothedNoiseMask[i];
        if (sum > 0.001f)
        {
            smoothedTonalMask[i] /= sum;
            smoothedNoiseMask[i] /= sum;
        }
    }
}

void TonalNoiseDecomposer::applyMasks(const std::complex<float>* input,
                                      std::complex<float>* tonalOutput,
                                      std::complex<float>* noisyOutput,
                                      int numBins)
{
    for (int i = 0; i < numBins; ++i)
    {
        tonalOutput[i] = input[i] * smoothedTonalMask[i];
        noisyOutput[i] = input[i] * smoothedNoiseMask[i];
    }
    
    // Handle DC and Nyquist
    tonalOutput[0] = input[0] * smoothedTonalMask[0];
    noisyOutput[0] = input[0] * smoothedNoiseMask[0];
    
    // Mirror for negative frequencies (for real FFT)
    for (int i = 1; i < numBins; ++i)
    {
        tonalOutput[fftSize - i] = std::conj(tonalOutput[i]);
        noisyOutput[fftSize - i] = std::conj(noisyOutput[i]);
    }
}

float TonalNoiseDecomposer::binToFrequency(int bin) const
{
    return static_cast<float>(bin * sampleRate / fftSize);
}

int TonalNoiseDecomposer::frequencyToBin(float freq) const
{
    return static_cast<int>(freq * fftSize / sampleRate + 0.5f);
}

float TonalNoiseDecomposer::calculateProminence(const float* magnitudes, int peakBin, int numBins) const
{
    float peakMag = magnitudes[peakBin];
    
    // Find local minimum on left
    float leftMin = peakMag;
    for (int i = peakBin - 1; i >= std::max(0, peakBin - 10); --i)
    {
        if (magnitudes[i] < leftMin)
        {
            leftMin = magnitudes[i];
        }
        if (magnitudes[i] > magnitudes[i + 1])
        {
            break; // Found another peak
        }
    }
    
    // Find local minimum on right
    float rightMin = peakMag;
    for (int i = peakBin + 1; i < std::min(numBins, peakBin + 10); ++i)
    {
        if (magnitudes[i] < rightMin)
        {
            rightMin = magnitudes[i];
        }
        if (magnitudes[i] > magnitudes[i - 1])
        {
            break; // Found another peak
        }
    }
    
    float minVal = std::min(leftMin, rightMin);
    if (minVal > 0.0001f)
    {
        return 20.0f * std::log10(peakMag / minVal);
    }
    
    return 0.0f;
}