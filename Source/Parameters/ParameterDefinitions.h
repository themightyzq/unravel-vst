#pragma once

#include <JuceHeader.h>

namespace ParameterIDs
{
    // Core controls
    const juce::String bypass = "bypass";
    const juce::String tonalGain = "tonalGain";
    const juce::String noisyGain = "noisyGain";

    // Solo/Mute controls
    const juce::String soloTonal = "soloTonal";       // Solo tonal component
    const juce::String soloNoise = "soloNoise";       // Solo noise component
    const juce::String muteTonal = "muteTonal";       // Mute tonal component
    const juce::String muteNoise = "muteNoise";       // Mute noise component

    // Separation controls
    const juce::String separation = "separation";      // 0-100%: How aggressively to separate
    const juce::String focus = "focus";                // -100 to +100: Tonal (-) vs Noise (+) bias
    const juce::String spectralFloor = "spectralFloor"; // 0-100%: Extreme isolation gating (default 0=OFF)

    // Quality control
    const juce::String quality = "quality";        // 0: Low latency, 1: High quality
}