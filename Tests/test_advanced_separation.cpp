#include <iostream>
#include <vector>
#include <cmath>
#include <memory>
#include "../Source/DSP/SpectralPeakTracker.h"
#include "../Source/DSP/HarmonicAnalyzer.h"
#include "../Source/DSP/AdvancedMaskEstimator.h"

// Simple test to verify the advanced separation algorithm
int main()
{
    const double sampleRate = 48000.0;
    const int fftSize = 2048;
    const int numBins = fftSize / 2 + 1;
    const double frameRate = sampleRate / 512.0;  // hopSize = 512
    
    std::cout << "Testing Advanced Tonal/Noise Separation Algorithm\n";
    std::cout << "==================================================\n\n";
    
    // Create test signal: 440Hz sine wave + noise
    std::vector<float> testMagnitudes(numBins, 0.0f);
    std::vector<float> testPhases(numBins, 0.0f);
    
    // Add harmonic content (440Hz and harmonics)
    const float fundamentalFreq = 440.0f;
    const float binFreq = sampleRate / fftSize;
    
    for (int harmonic = 1; harmonic <= 5; ++harmonic)
    {
        const float freq = fundamentalFreq * harmonic;
        const int bin = static_cast<int>(freq / binFreq);
        
        if (bin < numBins)
        {
            // Amplitude decreases with harmonic number
            testMagnitudes[bin] = 1.0f / harmonic;
            testPhases[bin] = 0.0f;
            
            std::cout << "Added harmonic " << harmonic << " at " << freq 
                     << " Hz (bin " << bin << ") with magnitude " 
                     << testMagnitudes[bin] << "\n";
        }
    }
    
    // Add some noise
    for (int bin = 100; bin < 300; ++bin)
    {
        testMagnitudes[bin] += 0.01f * (1.0f + sin(bin * 0.1f));
    }
    
    std::cout << "\nInitializing components...\n";
    
    // Initialize components
    auto peakTracker = std::make_unique<SpectralPeakTracker>();
    peakTracker->prepare(sampleRate, fftSize, 512);
    
    auto harmonicAnalyzer = std::make_unique<HarmonicAnalyzer>();
    harmonicAnalyzer->prepare(sampleRate, fftSize);
    
    auto maskEstimator = std::make_unique<AdvancedMaskEstimator>();
    maskEstimator->prepare(sampleRate, frameRate);
    
    std::cout << "Processing frame...\n\n";
    
    // Process a few frames to build up tracking
    for (int frame = 0; frame < 5; ++frame)
    {
        // Detect peaks
        auto peaks = peakTracker->processFrame(testMagnitudes.data(), testPhases.data());
        
        std::cout << "Frame " << frame << ": Detected " << peaks.size() << " peaks\n";
        
        if (frame == 4)  // Analyze the last frame in detail
        {
            // Get active partials
            auto activePartials = peakTracker->getActivePartials();
            std::cout << "Active partials: " << activePartials.size() << "\n";
            
            for (const auto& partial : activePartials)
            {
                std::cout << "  Partial ID " << partial->id 
                         << ": freq=" << partial->averageFrequency 
                         << " Hz, deviation=" << partial->frequencyDeviation 
                         << " Hz\n";
            }
            
            // Analyze harmonics
            auto harmonicGroups = harmonicAnalyzer->analyzeHarmonics(activePartials, 
                                                                    testMagnitudes.data());
            
            std::cout << "\nHarmonic groups found: " << harmonicGroups.size() << "\n";
            
            for (const auto& group : harmonicGroups)
            {
                std::cout << "  F0=" << group.fundamentalFreq 
                         << " Hz, harmonicity=" << group.harmonicity
                         << ", confidence=" << group.confidence
                         << ", partials=" << group.partialIds.size() << "\n";
            }
            
            // Generate masks
            std::vector<float> tonalMask(numBins, 0.0f);
            std::vector<float> noiseMask(numBins, 0.0f);
            
            // Create a simple MagPhaseFrame for testing
            class TestMagPhaseFrame
            {
            public:
                const float* magnitudes() const { return mags; }
                const float* phases() const { return phs; }
                float* mags;
                float* phs;
            };
            
            TestMagPhaseFrame testFrame;
            testFrame.mags = testMagnitudes.data();
            testFrame.phs = testPhases.data();
            
            // Note: Can't use AdvancedMaskEstimator directly without proper MagPhaseFrame
            // But we can test the harmonic analyzer's mask generation
            harmonicAnalyzer->computeTonalNoiseMasks(harmonicGroups, activePartials,
                                                    testMagnitudes.data(),
                                                    tonalMask.data(), noiseMask.data());
            
            // Check mask values at key frequencies
            std::cout << "\nMask values at key frequencies:\n";
            
            for (int harmonic = 1; harmonic <= 5; ++harmonic)
            {
                const float freq = fundamentalFreq * harmonic;
                const int bin = static_cast<int>(freq / binFreq);
                
                if (bin < numBins)
                {
                    std::cout << "  " << freq << " Hz (bin " << bin << "): "
                             << "tonal=" << tonalMask[bin] 
                             << ", noise=" << noiseMask[bin] << "\n";
                }
            }
            
            // Check average mask values in noise region
            float avgTonalInNoise = 0.0f;
            float avgNoiseInNoise = 0.0f;
            int noiseCount = 0;
            
            for (int bin = 100; bin < 300; ++bin)
            {
                // Skip harmonic bins
                bool isHarmonic = false;
                for (int h = 1; h <= 10; ++h)
                {
                    const int harmonicBin = static_cast<int>(fundamentalFreq * h / binFreq);
                    if (abs(bin - harmonicBin) < 3)
                    {
                        isHarmonic = true;
                        break;
                    }
                }
                
                if (!isHarmonic)
                {
                    avgTonalInNoise += tonalMask[bin];
                    avgNoiseInNoise += noiseMask[bin];
                    noiseCount++;
                }
            }
            
            if (noiseCount > 0)
            {
                avgTonalInNoise /= noiseCount;
                avgNoiseInNoise /= noiseCount;
                
                std::cout << "\nAverage mask values in noise region:\n";
                std::cout << "  Tonal: " << avgTonalInNoise << "\n";
                std::cout << "  Noise: " << avgNoiseInNoise << "\n";
            }
            
            // Verify separation quality
            std::cout << "\n=== SEPARATION QUALITY CHECK ===\n";
            
            bool harmonicsAreTonal = true;
            for (int harmonic = 1; harmonic <= 3; ++harmonic)
            {
                const int bin = static_cast<int>(fundamentalFreq * harmonic / binFreq);
                if (bin < numBins && tonalMask[bin] < 0.6f)
                {
                    harmonicsAreTonal = false;
                    std::cout << "❌ Harmonic " << harmonic << " not properly identified as tonal\n";
                }
            }
            
            if (harmonicsAreTonal)
            {
                std::cout << "✓ Harmonics properly identified as tonal\n";
            }
            
            if (avgNoiseInNoise > avgTonalInNoise)
            {
                std::cout << "✓ Noise regions properly identified\n";
            }
            else
            {
                std::cout << "❌ Noise regions not properly separated\n";
            }
            
            // Calculate overall separation metric
            float separationQuality = 0.0f;
            for (int h = 1; h <= 3; ++h)
            {
                const int bin = static_cast<int>(fundamentalFreq * h / binFreq);
                if (bin < numBins)
                {
                    separationQuality += tonalMask[bin];
                }
            }
            separationQuality /= 3.0f;
            separationQuality *= (avgNoiseInNoise / (avgTonalInNoise + 0.001f));
            
            std::cout << "\nOverall separation quality: " << separationQuality << " ";
            if (separationQuality > 1.5f)
                std::cout << "(Excellent)\n";
            else if (separationQuality > 1.0f)
                std::cout << "(Good)\n";
            else if (separationQuality > 0.5f)
                std::cout << "(Fair)\n";
            else
                std::cout << "(Poor)\n";
        }
    }
    
    std::cout << "\nTest complete!\n";
    
    return 0;
}