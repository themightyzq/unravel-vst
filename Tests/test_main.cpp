/**
 * Unified Test Runner for Unravel DSP Tests
 *
 * This file provides the single main() entry point for all DSP unit tests.
 * Each test file defines a test class with a runAllTests() method that
 * is called from here.
 */

#include "DSP/JuceIncludes.h"
#include <iostream>

// Forward declarations of test runner classes
class HPSSProcessorTest;
class STFTProcessorTest;
class MagPhaseFrameTest;
class MaskEstimatorTest;

// External test functions - each test file provides a runAllTests function
// These are declared in each test file

// From test_hpss_processor.cpp
namespace hpss_tests { void run(); }

// From test_stft_processor.cpp
namespace stft_tests { void run(); }

// From test_magphase_frame.cpp
namespace magphase_tests { void run(); }

// From test_mask_estimator.cpp
namespace mask_tests { void run(); }

// From test_debug_passthrough.cpp
namespace debug_passthrough_tests { void run(); }

int main()
{
    std::cout << "======================================" << std::endl;
    std::cout << "  Unravel DSP Unit Test Suite" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    int totalPassed = 0;
    int totalFailed = 0;

    // Run HPSS Processor tests
    std::cout << "--- Running HPSS Processor Tests ---" << std::endl;
    try {
        hpss_tests::run();
        totalPassed++;
    } catch (const std::exception& e) {
        std::cerr << "HPSS tests failed with exception: " << e.what() << std::endl;
        totalFailed++;
    }
    std::cout << std::endl;

    // Run STFT Processor tests
    std::cout << "--- Running STFT Processor Tests ---" << std::endl;
    try {
        stft_tests::run();
        totalPassed++;
    } catch (const std::exception& e) {
        std::cerr << "STFT tests failed with exception: " << e.what() << std::endl;
        totalFailed++;
    }
    std::cout << std::endl;

    // Run MagPhaseFrame tests
    std::cout << "--- Running MagPhaseFrame Tests ---" << std::endl;
    try {
        magphase_tests::run();
        totalPassed++;
    } catch (const std::exception& e) {
        std::cerr << "MagPhaseFrame tests failed with exception: " << e.what() << std::endl;
        totalFailed++;
    }
    std::cout << std::endl;

    // Run MaskEstimator tests
    std::cout << "--- Running MaskEstimator Tests ---" << std::endl;
    try {
        mask_tests::run();
        totalPassed++;
    } catch (const std::exception& e) {
        std::cerr << "MaskEstimator tests failed with exception: " << e.what() << std::endl;
        totalFailed++;
    }
    std::cout << std::endl;

    // Run Debug Passthrough tests (diagnosis)
    std::cout << "--- Running Debug Passthrough Tests ---" << std::endl;
    try {
        debug_passthrough_tests::run();
        totalPassed++;
    } catch (const std::exception& e) {
        std::cerr << "Debug Passthrough tests failed with exception: " << e.what() << std::endl;
        totalFailed++;
    }
    std::cout << std::endl;

    // Summary
    std::cout << "======================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Test suites passed: " << totalPassed << std::endl;
    std::cout << "Test suites failed: " << totalFailed << std::endl;

    if (totalFailed == 0) {
        std::cout << "\nAll test suites completed successfully!" << std::endl;
        return 0;
    } else {
        std::cout << "\nSome test suites failed." << std::endl;
        return 1;
    }
}
