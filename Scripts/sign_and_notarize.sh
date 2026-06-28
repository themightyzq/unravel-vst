#!/bin/bash

# =============================================================================
# Unravel Plugin Code Signing and Notarization Script
# =============================================================================
#
# This script signs and notarizes all three Unravel macOS formats
# (VST3 + AU .component + Standalone .app).
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

# Plugin paths — sign/notarize all three shipped formats. The README points
# users at the VST3, the AU .component, and the Standalone .app, so all three
# must be signed or first-launch Gatekeeper rejects the unsigned ones.
VST3_PATH="$BUILD_DIR/Unravel_artefacts/Release/VST3/Unravel.vst3"
AU_PATH="$BUILD_DIR/Unravel_artefacts/Release/AU/Unravel.component"
# Standalone path is overridden via RUNTIME_OUTPUT_DIRECTORY in CMakeLists.txt.
APP_PATH="$BUILD_DIR/bin/Standalone/Unravel.app"
BUNDLES=("$VST3_PATH" "$AU_PATH" "$APP_PATH")

# Install paths
INSTALLED_VST3="$HOME/Library/Audio/Plug-Ins/VST3/Unravel.vst3"
INSTALLED_AU="$HOME/Library/Audio/Plug-Ins/Components/Unravel.component"

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
echo "Step 1: Building Release version (VST3 + AU + Standalone)..."
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

for bundle in "${BUNDLES[@]}"; do
    if [ ! -e "$bundle" ]; then
        echo -e "${RED}  Missing bundle: $bundle${NC}"
        echo "  Build did not produce it — aborting."
        exit 1
    fi
    echo "  Signing $(basename "$bundle")..."
    codesign --force --deep --options runtime \
        --sign "$DEVELOPER_ID" \
        --timestamp \
        "$bundle"
    echo "  Verifying $(basename "$bundle")..."
    codesign --verify --verbose=2 "$bundle"
done
echo -e "${GREEN}Signing complete (VST3 + AU + Standalone).${NC}"
echo ""

# Step 3: Create ZIP for notarization (all three bundles in one submission)
echo "Step 3: Creating ZIP archive for notarization..."
ZIP_PATH="$BUILD_DIR/Unravel-macOS.zip"
rm -f "$ZIP_PATH"

# ditto --keepParent only preserves one top dir, so stage the three bundles
# under a single folder and zip that — notarytool accepts a multi-bundle zip.
STAGE_DIR="$BUILD_DIR/notarize-stage"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
for bundle in "${BUNDLES[@]}"; do
    ditto "$bundle" "$STAGE_DIR/$(basename "$bundle")"
done
ditto -c -k --keepParent "$STAGE_DIR" "$ZIP_PATH"

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

    # Step 5: Staple the ticket (per bundle — stapler operates on one path)
    echo "Step 5: Stapling notarization ticket..."
    for bundle in "${BUNDLES[@]}"; do
        echo "  Stapling $(basename "$bundle")..."
        xcrun stapler staple "$bundle"
    done
    echo -e "${GREEN}Stapling complete.${NC}"
    echo ""

    # Step 6: Install plugins to user folders (Standalone .app left in build/)
    echo "Step 6: Installing to user plugin folders..."
    rm -rf "$INSTALLED_VST3"
    cp -R "$VST3_PATH" "$INSTALLED_VST3"
    echo -e "${GREEN}  VST3 installed to: $INSTALLED_VST3${NC}"
    mkdir -p "$(dirname "$INSTALLED_AU")"
    rm -rf "$INSTALLED_AU"
    cp -R "$AU_PATH" "$INSTALLED_AU"
    echo -e "${GREEN}  AU installed to:   $INSTALLED_AU${NC}"
    rm -rf "$STAGE_DIR"   # tidy the notarization staging copies
    echo ""

    # Final verification
    echo "=============================================="
    echo "  Final Verification"
    echo "=============================================="
    echo ""
    echo "Validating stapled tickets on all three bundles..."
    for bundle in "${BUNDLES[@]}"; do
        echo "  $(basename "$bundle"):"
        stapler validate "$bundle" || echo -e "${YELLOW}    staple validate failed${NC}"
    done
    echo ""
    echo "Gatekeeper assessment (installed VST3)..."
    spctl --assess --type open --context context:primary-signature -v "$INSTALLED_VST3" 2>&1 || true
    echo ""
    echo -e "${GREEN}=============================================="
    echo "  SUCCESS! All formats signed and notarized."
    echo "==============================================${NC}"
    echo ""
    echo "Installed plugins:"
    echo "  VST3: $INSTALLED_VST3"
    echo "  AU:   $INSTALLED_AU"
    echo "  Standalone .app signed in place: $APP_PATH"
    echo ""
    echo "Distribution ZIP (all three, notarized):"
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
