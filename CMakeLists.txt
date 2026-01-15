cmake_minimum_required(VERSION 3.22)

# Universal Binary for macOS (Soundminer compatibility)
# Must be set BEFORE project() to take effect
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
endif()

project(Unravel VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(JUCE)

# Define the plugin
juce_add_plugin(Unravel
    COMPANY_NAME "ZQ SFX"
    PLUGIN_MANUFACTURER_CODE ZQSF
    PLUGIN_CODE Unrv
    FORMATS VST3 Standalone
    PRODUCT_NAME "Unravel"
    PLUGIN_NAME "Unravel"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Fx" "Spectral"
    COMPANY_WEBSITE "https://zqsfx.com"
    COMPANY_EMAIL "support@zqsfx.com"
    BUNDLE_ID "com.zqsfx.unravel"
    PLUGIN_DESCRIPTION "Spectral decomposition plugin for tonal/noise separation"
)

# Generate JuceHeader.h
juce_generate_juce_header(Unravel)

# Add source files
target_sources(Unravel
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/STFTProcessor.cpp
        Source/DSP/STFTProcessor.h
        Source/DSP/MagPhaseFrame.cpp
        Source/DSP/MagPhaseFrame.h
        Source/DSP/MaskEstimator.cpp
        Source/DSP/MaskEstimator.h
        Source/DSP/HPSSProcessor.cpp
        Source/DSP/HPSSProcessor.h
        Source/GUI/CustomLookAndFeel.cpp
        Source/GUI/CustomLookAndFeel.h
        Source/GUI/VerticalSlider.cpp
        Source/GUI/VerticalSlider.h
        Source/GUI/XYPad.cpp
        Source/GUI/XYPad.h
        Source/GUI/SpectrumDisplay.cpp
        Source/GUI/SpectrumDisplay.h
        Source/Parameters/ParameterDefinitions.h
)

# Add binary data (empty for now)
# juce_add_binary_data(UnravelData
#     SOURCES
#     # Add any images or resources here
# )

# Link libraries (matching HyperPrism pattern for Soundminer compatibility)
target_link_libraries(Unravel
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Plugin-specific compile definitions (matching HyperPrism exactly)
target_compile_definitions(Unravel
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_JACK=0
        JUCE_ALSA=0
)

# Set plugin binary output directory
set_target_properties(Unravel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

