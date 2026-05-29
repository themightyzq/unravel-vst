#pragma once

#include <JuceHeader.h>

namespace ParameterIDs
{
    // Core controls
    const juce::String bypass = "bypass";
    const juce::String tonalGain = "tonalGain";
    const juce::String noisyGain = "noisyGain";
    const juce::String transientGain = "transientGain";   // Transient (short/impulsive) component gain

    // Solo/Mute controls
    const juce::String soloTonal = "soloTonal";       // Solo tonal component
    const juce::String soloNoise = "soloNoise";       // Solo noise component
    const juce::String soloTransient = "soloTransient"; // Solo transient component
    const juce::String muteTonal = "muteTonal";       // Mute tonal component
    const juce::String muteNoise = "muteNoise";       // Mute noise component
    const juce::String muteTransient = "muteTransient"; // Mute transient component

    // Separation controls
    const juce::String separation = "separation";      // 0-100%: How aggressively to separate
    const juce::String focus = "focus";                // -100 to +100: Tonal (-) vs Noise (+) bias
    const juce::String spectralFloor = "spectralFloor"; // 0-100%: Extreme isolation gating (default 0=OFF)

    // Post-processing
    const juce::String brightness = "brightness";             // High shelf filter for treble adjustment
}