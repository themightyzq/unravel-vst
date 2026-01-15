#!/bin/bash

# Build script for Unravel plugin
# Supports macOS (AU/VST3) and Linux (VST3)

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Unravel Plugin Build Script ===${NC}"

# Check if JUCE is available
if [ ! -d "$PROJECT_DIR/JUCE" ]; then
    echo -e "${YELLOW}JUCE not found. Cloning JUCE repository...${NC}"
    cd "$PROJECT_DIR"
    git clone https://github.com/juce-framework/JUCE.git
    cd JUCE
    git checkout 7.0.9  # Use stable version
fi

# Parse arguments
BUILD_TYPE="Release"
CLEAN_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--debug] [--clean]"
            exit 1
            ;;
    esac
done

# Clean build if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${GREEN}Configuring with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

# Build
echo -e "${GREEN}Building Unravel plugin...${NC}"
cmake --build . --config $BUILD_TYPE -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

# Report build results
echo -e "${GREEN}=== Build Complete ===${NC}"

# Find and list built plugins
if [ -d "$BUILD_DIR/Unravel_artefacts" ]; then
    echo -e "${GREEN}Built plugins:${NC}"
    
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3" ]; then
        VST3_PATH="$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3"
        echo "  VST3: $VST3_PATH"

        # Verify Universal Binary architecture
        if [ -f "$VST3_PATH/Contents/MacOS/Unravel" ]; then
            echo -e "${GREEN}Verifying architecture...${NC}"
            ARCH_INFO=$(file "$VST3_PATH/Contents/MacOS/Unravel")
            echo "  $ARCH_INFO"
            if [[ "$ARCH_INFO" == *"arm64"* ]] && [[ "$ARCH_INFO" == *"x86_64"* ]]; then
                echo -e "${GREEN}  Universal Binary: OK${NC}"
            else
                echo -e "${YELLOW}  WARNING: Not a Universal Binary - Soundminer compatibility may be affected${NC}"
            fi
        fi
    fi
    
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU" ]; then
        echo "  AU: $BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU/Unravel.component"
    fi
    
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone" ]; then
        echo "  Standalone: $BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone/Unravel.app"
    fi
else
    echo -e "${RED}Build artifacts not found!${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"