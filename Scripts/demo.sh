#!/bin/bash

# Demo script for Unravel plugin
# Shows where plugins are installed and how to test them

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Unravel Plugin Demo ===${NC}"

# Check plugin installation
echo -e "${CYAN}Checking plugin installation...${NC}"

VST3_PATH="$HOME/Library/Audio/Plug-Ins/VST3/Unravel.vst3"

if [ -d "$VST3_PATH" ]; then
    echo -e "${GREEN}✓ VST3 plugin installed at:${NC} $VST3_PATH"
else
    echo -e "${RED}✗ VST3 plugin not found${NC}"
    echo -e "${YELLOW}  Run ./install.sh to install the plugin${NC}"
fi

echo ""
echo -e "${CYAN}Plugin Features:${NC}"
echo "• Tonal/Noise separation using advanced spectral decomposition"
echo "• Optional transient detection and separation"
echo "• Real-time processing with low latency (~46ms)"
echo "• Artifact smoothing for clean results"
echo "• Modern, intuitive interface"
echo "• Full parameter automation support"

echo ""
echo -e "${CYAN}Usage Instructions:${NC}"
echo -e "${YELLOW}1. For Logic Pro:${NC}"
echo "   - Open Logic Pro"
echo "   - Create an audio track or select existing audio"
echo "   - Insert > Audio FX > ZQ SFX > Unravel"

echo -e "${YELLOW}2. For Ableton Live:${NC}"
echo "   - Open Ableton Live"
echo "   - Load audio on an audio track"
echo "   - Audio Effects > ZQ SFX > Unravel"

echo -e "${YELLOW}3. For other DAWs:${NC}"
echo "   - Open your DAW's plugin manager"
echo "   - Scan for new VST3 plugins"
echo "   - Look for Unravel under ZQ SFX"

echo ""
echo -e "${CYAN}Parameter Guide:${NC}"
echo -e "${YELLOW}Tonal Gain:${NC} Controls harmonic content (-∞ to +6dB)"
echo -e "${YELLOW}Noisy Gain:${NC} Controls noise/texture content (-∞ to +6dB)"
echo -e "${YELLOW}Transient Gain:${NC} Controls percussive elements (when enabled)"
echo -e "${YELLOW}Separate Transients:${NC} Enable 3-way decomposition"
echo -e "${YELLOW}Tonal/Noisy Balance:${NC} Adjust detection sensitivity (0-100)"
echo -e "${YELLOW}Artifact Smoothing:${NC} Reduce processing artifacts (0-100)"

echo ""
echo -e "${CYAN}Common Use Cases:${NC}"
echo "• ${GREEN}Dialog Cleanup:${NC} Set Noisy Gain to -∞ to remove background noise"
echo "• ${GREEN}Drum Isolation:${NC} Enable transients, boost Transient Gain"
echo "• ${GREEN}Ambient Textures:${NC} Boost Noisy Gain, reduce Tonal Gain"
echo "• ${GREEN}Vocal Enhancement:${NC} Fine-tune balance for speech clarity"

echo ""
echo -e "${CYAN}Quick Test:${NC}"
echo "Load the VST3 plugin in your DAW and test with audio material."
echo "The plugin should appear under: ZQ SFX > Unravel"

echo ""
echo -e "${GREEN}=== Demo Complete ===${NC}"
echo -e "${YELLOW}Happy processing with Unravel!${NC}"