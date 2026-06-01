cmake_minimum_required(VERSION 3.22)

# Universal Binary for macOS (Soundminer compatibility)
# Must be set BEFORE project() to take effect
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    # Deployment target must be set BEFORE project() so the recorded
    # LC_BUILD_VERSION pins what users can install on. Without this, CI
    # binaries inherit the runner's SDK default (currently macOS 14+ on
    # macos-latest) and would refuse to load on older systems.
    #
    # 11.0 is the hard floor for Universal Binaries: arm64's minimum is
    # macOS 11 (Big Sur, the first arm64 OS), and a Universal Binary's
    # effective minos is the max of all slice minimums. Setting this lower
    # is silently overridden by the linker. If we ever drop the arm64
    # slice, this can be lowered to 10.13.
    # No FORCE — a developer passing -DCMAKE_OSX_DEPLOYMENT_TARGET=... should
    # be able to override this for local testing. CI uses the default.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# 1.3.1: audit-fix pass — host-bypass PDC, preset-load snap, deployment
# target pinned, AU subtype identity changed (Unrv → UnRv). The AU identity
# change requires a version bump so Logic's (mfr, subtype, version) AU cache
# refreshes instead of colliding with the 1.3.0 entry.
project(Unravel VERSION 1.3.1)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(JUCE)

# Resolve COPY_PLUGIN_AFTER_BUILD before juce_add_plugin reads it.
# In CI the system plugin folder is unwritable (Windows requires admin) and
# uninteresting (CI uploads artefacts rather than installing).
if(DEFINED ENV{CI})
    set(UNRAVEL_COPY_PLUGIN_AFTER_BUILD FALSE)
else()
    set(UNRAVEL_COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

# Define the plugin
juce_add_plugin(Unravel
    COMPANY_NAME "ZQ SFX"
    PLUGIN_MANUFACTURER_CODE ZQSF
    # AU subtype: 4 chars, at least one uppercase per Apple's AU spec.
    # The earlier "Unrv" was tolerated by auval but caused case-fold collisions
    # in some host AU caches on re-install. v1.3.x onward: "UnRv". This is an
    # AU identity change — old sessions referring to "Unrv" will not find the
    # plugin until resaved.
    PLUGIN_CODE UnRv
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Unravel"
    PLUGIN_NAME "Unravel"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # COPY_PLUGIN_AFTER_BUILD installs to the system plugin folder after a
    # local build. Enabled for developer ergonomics; suppressed in CI where
    # the system path is unwritable on Windows (admin required) and
    # uninteresting on all platforms (CI uploads artefacts, doesn't install).
    # JUCE evaluates this argument at configure time, so a generator
    # expression won't gate the post-build step — use a plain bool.
    COPY_PLUGIN_AFTER_BUILD ${UNRAVEL_COPY_PLUGIN_AFTER_BUILD}
    # VST3 subcategories must use Steinberg SDK names so hosts can route
    # correctly. "Spectral" is non-standard; HPSS source separation is closest
    # to Restoration, and brightness adds an EQ stage.
    VST3_CATEGORIES "Fx" "Restoration" "EQ"
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
        Source/DSP/HarmonicMaskDetector.cpp
        Source/DSP/HarmonicMaskDetector.h
        Source/DSP/HPSSProcessor.cpp
        Source/DSP/HPSSProcessor.h
        Source/GUI/CustomLookAndFeel.cpp
        Source/GUI/CustomLookAndFeel.h
        Source/GUI/XYPad.cpp
        Source/GUI/XYPad.h
        Source/GUI/SpectrumDisplay.cpp
        Source/GUI/SpectrumDisplay.h
        Source/GUI/Theme.h
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

