#!/bin/bash
# Clear Soundminer plugin cache to force a fresh scan
# This is safe - Soundminer will rebuild the cache on next launch

SOUNDMINER_SUPPORT="$HOME/Library/Application Support/SoundminerV6"
PLUGINS_DB="$SOUNDMINER_SUPPORT/plugins.sqlite"

echo "=== Soundminer Plugin Cache Cleaner ==="
echo ""

if [ -f "$PLUGINS_DB" ]; then
    echo "Current plugin database:"
    ls -la "$PLUGINS_DB"
    echo ""

    echo "Backing up current database..."
    cp "$PLUGINS_DB" "${PLUGINS_DB}.backup"

    echo "Removing plugin database to force rescan..."
    rm "$PLUGINS_DB"

    echo "Done! Now:"
    echo "  1. Launch Soundminer"
    echo "  2. Open the DSP Rack (cmd+D)"
    echo "  3. Soundminer will automatically rescan all plugins"
    echo "  4. Check if Unravel appears in the VST3 section"
    echo ""
    echo "If needed, restore backup with:"
    echo "  cp \"${PLUGINS_DB}.backup\" \"$PLUGINS_DB\""
else
    echo "No plugins.sqlite found at expected location."
    echo "Soundminer will scan plugins on next DSP Rack launch."
fi
