#!/bin/bash

# =============================================================================
# Unravel Plugin Code Signing and Notarization Script
# =============================================================================
#
# This script signs and notarizes the Unravel VST3 plugin for macOS.
#
# Prerequisites:
#   1. Apple Developer Program membership ($99/year)
#   2. Developer ID Application certificate installed in Keychain
#   3. App-specific password created at appleid.apple.com
#
# Usage:
#   ./sign_and_notarize.sh
#
# =============================================================================

set -e

# Configuration
PLUGIN_NAME="Unravel"
BUNDLE_ID="com.zqsfx.unravel"
DEVELOPER_ID="Developer ID Application: Zachary Quarles (AJU6TF97JM)"
TEAM_ID="AJU6TF97JM"

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Plugin paths
VST3_PATH="$BUILD_DIR/Unravel_artefacts/Release/VST3/Unravel.vst3"

# Install paths
INSTALLED_VST3="$HOME/Library/Audio/Plug-Ins/VST3/Unravel.vst3"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=============================================="
echo "  Unravel Plugin Signing & Notarization"
echo "=============================================="
echo ""

# Check if credentials are stored
CRED_NAME="AC_PASSWORD"
if ! xcrun notarytool history --keychain-profile "$CRED_NAME" &>/dev/null 2>&1; then
    echo -e "${YELLOW}Notarization credentials not found.${NC}"
    echo ""
    echo "Let's set them up now."
    echo ""
    echo "You'll need:"
    echo "  1. Your Apple ID email"
    echo "  2. An app-specific password from https://appleid.apple.com/account/manage"
    echo ""
    read -p "Enter your Apple ID email: " APPLE_ID
    echo ""
    echo "Now enter your app-specific password (format: xxxx-xxxx-xxxx-xxxx)"
    read -s -p "App-specific password: " APP_PASSWORD
    echo ""
    echo ""

    echo "Storing credentials in Keychain..."
    xcrun notarytool store-credentials "$CRED_NAME" \
        --apple-id "$APPLE_ID" \
        --team-id "$TEAM_ID" \
        --password "$APP_PASSWORD"

    echo -e "${GREEN}Credentials stored successfully!${NC}"
    echo ""
fi

# Step 1: Build the plugin
echo "Step 1: Building Release version (VST3)..."
cd "$PROJECT_DIR"
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
echo -e "${GREEN}Build complete.${NC}"
echo ""

# Verify Universal Binary architecture
echo "Checking binary architecture..."
ARCH_CHECK=$(file "$VST3_PATH/Contents/MacOS/Unravel")
echo "  $ARCH_CHECK"
if [[ "$ARCH_CHECK" == *"arm64"* ]] && [[ "$ARCH_CHECK" == *"x86_64"* ]]; then
    echo -e "${GREEN}  Universal Binary: OK${NC}"
else
    echo -e "${YELLOW}  WARNING: Not a Universal Binary - Soundminer compatibility may be affected${NC}"
fi
echo ""

# Step 2: Sign the plugin
echo "Step 2: Signing with Developer ID..."

echo "  Signing VST3..."
codesign --force --deep --options runtime \
    --sign "$DEVELOPER_ID" \
    --timestamp \
    "$VST3_PATH"

# Verify signature
echo "Verifying signature..."
codesign --verify --verbose=2 "$VST3_PATH"
echo -e "${GREEN}Signing complete.${NC}"
echo ""

# Step 3: Create ZIP for notarization
echo "Step 3: Creating ZIP archive for notarization..."
ZIP_PATH="$BUILD_DIR/Unravel-macOS.zip"
rm -f "$ZIP_PATH"

ditto -c -k --keepParent "$VST3_PATH" "$ZIP_PATH"

echo -e "${GREEN}ZIP created: $ZIP_PATH${NC}"
echo ""

# Step 4: Submit for notarization
echo "Step 4: Submitting for notarization..."
echo "This may take a few minutes..."
SUBMIT_OUTPUT=$(xcrun notarytool submit "$ZIP_PATH" \
    --keychain-profile "$CRED_NAME" \
    --wait 2>&1)

echo "$SUBMIT_OUTPUT"

# Check if notarization succeeded
if echo "$SUBMIT_OUTPUT" | grep -q "status: Accepted"; then
    echo -e "${GREEN}Notarization successful!${NC}"
    echo ""

    # Step 5: Staple the ticket
    echo "Step 5: Stapling notarization ticket..."
    xcrun stapler staple "$VST3_PATH"
    echo -e "${GREEN}Stapling complete.${NC}"
    echo ""

    # Step 6: Install to user plugins folder
    echo "Step 6: Installing to user plugins folder..."
    rm -rf "$INSTALLED_VST3"
    cp -R "$VST3_PATH" "$INSTALLED_VST3"
    echo -e "${GREEN}  VST3 installed to: $INSTALLED_VST3${NC}"
    echo ""

    # Final verification
    echo "=============================================="
    echo "  Final Verification"
    echo "=============================================="
    echo ""
    echo "Checking Gatekeeper assessment..."
    spctl --assess --type open --context context:primary-signature -v "$INSTALLED_VST3" 2>&1 || true
    echo ""
    echo "Checking notarization status..."
    stapler validate "$INSTALLED_VST3"
    echo ""
    echo -e "${GREEN}=============================================="
    echo "  SUCCESS! Plugin is signed and notarized."
    echo "==============================================${NC}"
    echo ""
    echo "Installed plugin:"
    echo "  VST3: $INSTALLED_VST3"
    echo ""
    echo "Distribution ZIP:"
    echo "  $ZIP_PATH"

else
    echo -e "${RED}Notarization failed!${NC}"
    echo ""
    echo "Check the log for details:"
    SUBMISSION_ID=$(echo "$SUBMIT_OUTPUT" | grep -o 'id: [a-f0-9-]*' | head -1 | cut -d' ' -f2)
    if [ -n "$SUBMISSION_ID" ]; then
        xcrun notarytool log "$SUBMISSION_ID" --keychain-profile "$CRED_NAME"
    fi
    exit 1
fi
