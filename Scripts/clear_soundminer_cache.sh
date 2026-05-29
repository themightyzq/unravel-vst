#!/bin/bash
# Clear Soundminer plugin cache to force a fresh scan
# This is safe - Soundminer will rebuild the cache on next launch.
#
# Handles both Soundminer V6 and V7 — clears whichever caches are
# present so users on either version (or mid-migration) get a clean
# rescan without having to edit this script.

set -u

SUPPORT_ROOT="$HOME/Library/Application Support"
VERSIONS=("SoundminerV7" "SoundminerV6")

cleared=0
echo "=== Soundminer Plugin Cache Cleaner ==="
echo ""

for ver in "${VERSIONS[@]}"; do
    db="$SUPPORT_ROOT/$ver/plugins.sqlite"
    if [ -f "$db" ]; then
        echo "Found cache: $db"
        echo "Backing up to ${db}.backup"
        cp "$db" "${db}.backup"
        rm "$db"
        echo "  removed."
        echo ""
        cleared=$((cleared + 1))
    fi
done

if [ "$cleared" -gt 0 ]; then
    echo "Cleared $cleared cache(s)."
    echo "Next steps:"
    echo "  1. Launch Soundminer"
    echo "  2. Open the DSP Rack (Cmd+D)"
    echo "  3. Soundminer will automatically rescan all plugins"
    echo "  4. Confirm Unravel appears under VST3"
    echo ""
    echo "Restore from backup if needed:"
    for ver in "${VERSIONS[@]}"; do
        backup="$SUPPORT_ROOT/$ver/plugins.sqlite.backup"
        if [ -f "$backup" ]; then
            db="${backup%.backup}"
            echo "  cp \"$backup\" \"$db\""
        fi
    done
else
    echo "No Soundminer plugins.sqlite found under either"
    echo "  $SUPPORT_ROOT/SoundminerV6"
    echo "  $SUPPORT_ROOT/SoundminerV7"
    echo "Soundminer will scan plugins on next DSP Rack launch anyway."
fi
