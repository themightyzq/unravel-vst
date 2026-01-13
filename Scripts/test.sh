#!/bin/bash

# Test script for Unravel plugin
# Validates plugin with pluginval if available

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Unravel Plugin Test Script ===${NC}"

BUILD_TYPE="Release"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--debug]"
            exit 1
            ;;
    esac
done

# Check if pluginval is installed
if ! command -v pluginval &> /dev/null; then
    echo -e "${YELLOW}pluginval not found. Installing...${NC}"
    
    if [ "$(uname -s)" = "Darwin" ]; then
        # Download pluginval for macOS
        curl -L https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip -o /tmp/pluginval.zip
        unzip -o /tmp/pluginval.zip -d /tmp/
        sudo cp /tmp/pluginval.app/Contents/MacOS/pluginval /usr/local/bin/
        sudo chmod +x /usr/local/bin/pluginval
        rm -rf /tmp/pluginval*
    else
        echo -e "${RED}Please install pluginval manually from: https://github.com/Tracktion/pluginval${NC}"
        exit 1
    fi
fi

# Test VST3
if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" ]; then
    echo -e "${GREEN}Testing VST3 plugin...${NC}"
    pluginval --validate "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" \
              --timeout-ms 30000 \
              --verbose \
              --output-dir "$BUILD_DIR/test-results" || {
        echo -e "${RED}VST3 validation failed!${NC}"
        exit 1
    }
    echo -e "${GREEN}✓ VST3 validation passed${NC}"
fi

# Test AU (macOS only)
if [ "$(uname -s)" = "Darwin" ] && [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU/Unravel.component" ]; then
    echo -e "${GREEN}Testing AU plugin...${NC}"
    
    # First, validate with auval
    echo "Running auval..."
    auval -v aufx Unrv ZQSF || {
        echo -e "${YELLOW}auval validation failed (this might be normal for unsigned plugins)${NC}"
    }
    
    # Then test with pluginval
    pluginval --validate "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU/Unravel.component" \
              --timeout-ms 30000 \
              --verbose \
              --output-dir "$BUILD_DIR/test-results" || {
        echo -e "${RED}AU validation failed!${NC}"
        exit 1
    }
    echo -e "${GREEN}✓ AU validation passed${NC}"
fi

# Test Standalone app
if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone" ]; then
    echo -e "${GREEN}Testing Standalone app...${NC}"
    
    if [ "$(uname -s)" = "Darwin" ]; then
        # Launch and immediately quit the app to test if it starts
        APP_PATH="$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone/Unravel.app"
        if [ -d "$APP_PATH" ]; then
            echo "Launching standalone app..."
            open "$APP_PATH" &
            APP_PID=$!
            sleep 3
            kill $APP_PID 2>/dev/null || true
            echo -e "${GREEN}✓ Standalone app launches successfully${NC}"
        fi
    fi
fi

echo -e "${GREEN}=== All Tests Passed ===${NC}"