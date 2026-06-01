cmake_minimum_required(VERSION 3.22)

# =============================================================================
# Unravel offline DSP measurement harness
# =============================================================================
# Standalone console exe that links the SHIPPING DSP sources (HPSSProcessor,
# STFTProcessor, MagPhaseFrame, MaskEstimator) directly and drives them through
# synthetic test signals, measuring per-stream energy at each XY-pad corner.
#
# It does NOT modify or depend on the plugin target. It re-uses the same JUCE
# checkout and the same compile definitions the plugin uses so the DSP behaves
# identically.
#
# Build & run (from Unravel/):
#   cmake -S Harness -B build-harness -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-harness
#   ./build-harness/unravel_harness
# =============================================================================

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Harness arch (native only; not shipped)")
endif()

project(UnravelHarness VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reuse the JUCE checkout that sits beside this Harness dir (../JUCE).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_BINARY_DIR}/JUCE)

# A console app gives us juce::JUCEApplicationBase-free main(); we only need
# JUCE for FFT/vector ops and juce::Span used by the DSP headers.
juce_add_console_app(unravel_harness
    PRODUCT_NAME "unravel_harness"
)

juce_generate_juce_header(unravel_harness)

target_sources(unravel_harness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/STFTProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/MagPhaseFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/MaskEstimator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/LowFreqPartialTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/HarmonicMaskDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/MaskReconciler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP/HPSSProcessor.cpp
)

target_include_directories(unravel_harness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP
)

target_link_libraries(unravel_harness PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_dsp
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Match the plugin's relevant compile definitions so the DSP TU sees the same
# JUCE configuration it does inside the plugin build.
target_compile_definitions(unravel_harness PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
