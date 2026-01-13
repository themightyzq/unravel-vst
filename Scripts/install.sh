#!/bin/bash

# Install script for Unravel plugin
# Copies built plugins to system plugin directories

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Unravel Plugin Installation Script ===${NC}"

# Default to Release build
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

# Check if build exists
if [ ! -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE" ]; then
    echo -e "${RED}Build not found! Please run build.sh first.${NC}"
    exit 1
fi

# Detect OS
OS="$(uname -s)"

if [ "$OS" = "Darwin" ]; then
    echo -e "${GREEN}Installing on macOS...${NC}"
    
    # Install VST3
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" ]; then
        echo "Installing VST3..."
        VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
        mkdir -p "$VST3_DIR"
        rm -rf "$VST3_DIR/Unravel.vst3"
        cp -R "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" "$VST3_DIR/"
        echo -e "${GREEN}✓ VST3 installed to $VST3_DIR${NC}"
    fi
    
    # Install AU
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU/Unravel.component" ]; then
        echo "Installing AU..."
        AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
        mkdir -p "$AU_DIR"
        rm -rf "$AU_DIR/Unravel.component"
        cp -R "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/AU/Unravel.component" "$AU_DIR/"
        echo -e "${GREEN}✓ AU installed to $AU_DIR${NC}"
        
        # Kill and restart AU host to reload cache
        echo "Restarting AU host process..."
        killall -9 AudioComponentRegistrar 2>/dev/null || true
    fi
    
    # Install Standalone app
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone/Unravel.app" ]; then
        echo "Installing Standalone app..."
        APP_DIR="/Applications"
        rm -rf "$APP_DIR/Unravel.app"
        cp -R "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/Standalone/Unravel.app" "$APP_DIR/"
        echo -e "${GREEN}✓ Standalone app installed to $APP_DIR${NC}"
    fi
    
elif [ "$OS" = "Linux" ]; then
    echo -e "${GREEN}Installing on Linux...${NC}"
    
    # Install VST3
    if [ -d "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" ]; then
        echo "Installing VST3..."
        VST3_DIR="$HOME/.vst3"
        mkdir -p "$VST3_DIR"
        rm -rf "$VST3_DIR/Unravel.vst3"
        cp -R "$BUILD_DIR/Unravel_artefacts/$BUILD_TYPE/VST3/Unravel.vst3" "$VST3_DIR/"
        echo -e "${GREEN}✓ VST3 installed to $VST3_DIR${NC}"
    fi
    
else
    echo -e "${RED}Unsupported OS: $OS${NC}"
    exit 1
fi

echo -e "${GREEN}=== Installation Complete ===${NC}"
echo -e "${YELLOW}Note: You may need to rescan plugins in your DAW${NC}"