#pragma once

/**
 * JUCE Include Wrapper
 *
 * This header provides JUCE includes that work both for:
 * - The main plugin build (uses generated JuceHeader.h)
 * - Standalone tests (uses direct JUCE module includes)
 */

#if defined(JUCE_STANDALONE_APPLICATION) && JUCE_STANDALONE_APPLICATION == 1
    // Standalone/Test build - include JUCE modules directly
    #include <juce_core/juce_core.h>
    #include <juce_audio_basics/juce_audio_basics.h>
    #include <juce_dsp/juce_dsp.h>
#else
    // Plugin build - use generated header
    #include <JuceHeader.h>
#endif
